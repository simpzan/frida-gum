#include "elf_debug_info.h"

#include <map>
#include <vector>
#include <inttypes.h>
#include <cxxabi.h>
#include "log.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

unique_ptr<ELF> ELF::create(const char *file) {
  unique_fd fd(open(file, O_RDONLY));
  if (fd.get() == -1) {
    ERRNO("open('%s', readonly)", file);
    return nullptr;
  }
  auto ef = make_unique<elf::elf>(elf::create_mmap_loader(fd.release()));
  auto dw = make_unique<dwarf::dwarf>(dwarf::elf::create_loader(*ef));
  if (!(ef && dw)) {
    LOGE("ef %p, dw %p", ef.get(), dw.get());
    return nullptr;
  }
  auto ret = make_unique<ELF>();
  ret->path_ = file;
  ret->arch_ = (Arch)ef->get_hdr().machine;
  ret->ef_ = move(ef);
  ret->dw_ = move(dw);
  return ret;
}

typedef struct {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    uint8_t data[];
} ElfNoteSection_t;
#ifndef NT_GNU_BUILD_ID
# define NT_GNU_BUILD_ID 3
#endif
std::vector<uint8_t> get_build_id(const elf::section &section) {
  ElfNoteSection_t *nhdr = (ElfNoteSection_t *)section.data();
  if (nhdr->type != NT_GNU_BUILD_ID) return {};
  if (strcmp((char *)nhdr->data, "GNU")) return {};
  const uint8_t *bytes = nhdr->data + nhdr->namesz;
  std::vector<uint8_t> id(bytes, bytes + nhdr->descsz);
  return id;
}
std::vector<uint8_t> getBuildId(const elf::elf &f) {
  std::vector<uint8_t> id;
  for (auto &sec : f.sections()) {
    if (sec.get_name() == ".note.gnu.build-id") {
      id = get_build_id(sec);
      break;
    }
  }
  return id;
}
std::vector<uint8_t> ELF::buildId() const {
  return getBuildId(*ef_);
}

uint64_t getVirtualAddress(const elf::elf &ef) {
  using namespace elf;
  for (auto &seg : ef.segments()) {
    auto &hdr = seg.get_hdr();
    if (hdr.type != pt::load) continue;
    if ((hdr.flags & pf::x) != pf::x) continue;
    return hdr.vaddr;
  }
  return -1;
}
uint64_t ELF::vaddr() const { return getVirtualAddress(*ef_); }

std::vector<FunctionSymbol> getFunctionSymbols(const elf::elf &f, elf::sht type) {
  std::vector<FunctionSymbol> functions;
  for (auto &sec : f.sections()) {
    if (sec.get_hdr().type != type) continue;
    for (auto sym : sec.as_symtab()) {
      auto &d = sym.get_data();
      if (d.type() == elf::stt::func) {
        functions.push_back({d.value, d.size, sym.get_name()});
      }
    }
  }
  return functions;
}
std::vector<FunctionSymbol> ELF::functionSymbols() {
  auto result = getFunctionSymbols(*ef_, elf::sht::symtab);
  if (result.empty()) result = getFunctionSymbols(*ef_, elf::sht::dynsym);
  return result;
}

std::string demangle(const char *mangled_name) {
  int status = 0;
  auto realname = abi::__cxa_demangle(mangled_name, 0, 0, &status);
  if (status != 0) return mangled_name;
  std::string res = realname;
  free(realname);
  return res;
}
string getName(const dwarf::die &die) {
  auto value = die.resolve(dwarf::DW_AT::linkage_name);
  if (!value.valid()) value = die.resolve(dwarf::DW_AT::name);
  if (!value.valid()) return "";
  return demangle(value.as_string().c_str());
}
bool sameDie(const dwarf::die &die1, const dwarf::die &die2) {
  auto name1 = getName(die1);
  auto name2 = getName(die2);
  auto result = name1 == name2;
  // if (!result) LOGI("%s %s %d", name1.c_str(), name2.c_str(), result);
  return result;
}
void dumpDie(const dwarf::die &node, bool show = false) {
  LOGD("<%" PRIx64 "> %s", node.get_section_offset(), to_string(node.tag).c_str());
  auto attributes = node.attributes();
  int i = 0, total = attributes.size();
  for (auto &attr : attributes) {
    auto key = to_string(attr.first);
    auto value = to_string(attr.second);
    LOGD("%d/%d %s %s", ++i, total, key.c_str(), value.c_str());
  }
}
string decl_file(const dwarf::die &die) {
  using namespace dwarf;
  auto &cu = (compilation_unit &) die.get_unit();
  auto decl_file_value = die.resolve(DW_AT::decl_file);
  if (decl_file_value.valid()) {
    auto decl_file = decl_file_value.as_uconstant();
    if (decl_file == 0) return "";

    auto lt = cu.get_line_table();
    auto file = lt.get_file(decl_file);
    return file->path;
  }

  if (die.has(DW_AT::artificial) && die[DW_AT::artificial].as_flag()) {
    return to_string(cu.root()[DW_AT::name]);
  }
  return "";
}

void DebugInfo::loadDieR(const dwarf::die &die) {
  using namespace dwarf;
  for (auto &child : die) loadDieR(child);
  if (die.tag != DW_TAG::subprogram) return;
  if (!die.has(DW_AT::low_pc)) return;

  auto addr = at_low_pc(die);
  if (addr == 0) {
    LOGI("low_pc is 0");
    dumpDie(die);
    return;
  }

  auto found = dies[addr];
  if (!found.valid()) {
    dies[addr] = die;
  } else if (!sameDie(found, die)) {
    conflictedDies.push_back(die);
  }
}
DebugInfo::DebugInfo(const ELF &elf): elf_(elf), cus_(elf.dw_->compilation_units()) {
  for (auto &cu: cus_) loadDieR(cu.root());
  LOGI("indexed %d dies, conflicts %d", (int)dies.size(), (int)conflictedDies.size());
}
const dwarf::die DebugInfo::die(uint64_t addr) const {
  auto itr = dies.find(addr);
  if (itr != dies.end()) return itr->second;
  return dwarf::die();
}