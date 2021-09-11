#include <string>
#include <map>
#include <vector>
#include <inttypes.h>
#include <cxxabi.h>
#include "elf_file.h"
#include "log.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "elf/elf++.hh"
#include "dwarf/dwarf++.hh"
#include "utils.h"

using namespace std;


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

struct FunctionSymbol {
  uint32_t address;
  uint32_t size;
  string name;
};
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

class ELF {
 public:
  static unique_ptr<ELF> create(const char *file) {
    unique_fd fd(open(file, O_RDONLY));
    if (fd.get() == -1) {
      ERRNO("open('%s', readonly)", file);
      return nullptr;
    }

    auto ef = make_unique<elf::elf>(elf::create_mmap_loader(fd.get()));
    auto dw = make_unique<dwarf::dwarf>(dwarf::elf::create_loader(*ef));
    if (!(ef && dw)) {
      LOGE("ef %p, dw %p", ef.get(), dw.get());
      return nullptr;
    }
    auto ret = make_unique<ELF>();
    ret->path_ = file;
    ret->fd_ = move(fd);
    ret->arch_ = (Arch)ef->get_hdr().machine;
    ret->ef_ = move(ef);
    ret->dw_ = move(dw);
    return ret;
  }
  ~ELF() {}
  enum class Arch {
    arm32 = 40,
    arm64 = 183,
  };
  Arch arch() const { return arch_; }
  vector<uint8_t> buildId() const { return getBuildId(*ef_); }
  uint64_t vaddr() const { return getVirtualAddress(*ef_); }
  std::vector<FunctionSymbol> functionSymbols() {
    auto result = getFunctionSymbols(*ef_, elf::sht::symtab);
    if (result.empty()) result = getFunctionSymbols(*ef_, elf::sht::dynsym);
    return result;
  }


 private:
  string path_;
  unique_fd fd_;
  unique_ptr<elf::elf> ef_;
  unique_ptr<dwarf::dwarf> dw_;
  Arch arch_;
  friend class DebugInfo;
};

string archString(ELF::Arch arch) {
  switch (arch) {
  case ELF::Arch::arm32: return "arm32";
  case ELF::Arch::arm64: return "arm64";
  default: return "unknown";
  }
}

static std::string demangle(const char *mangled_name) {
  int status = 0;
  auto realname = abi::__cxa_demangle(mangled_name, 0, 0, &status);
  if (status != 0) return mangled_name;
  std::string res = realname;
  free(realname);
  return res;
}
Napi::Value demangleCppName(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  auto name = info[0].As<Napi::String>().Utf8Value();
  auto demangled = demangle(name.c_str());
  // LOGI("name %s -> %s", name.Utf8Value().c_str(), demangled.c_str());
  return Napi::String::New(env, demangled);
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
void dump_die(const dwarf::die &node, bool show = false) {
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
int decl_line(const dwarf::die &die) {
  auto line = die.resolve(dwarf::DW_AT::decl_line).as_uconstant();
  return line;
}
class DebugInfo {
  void loadDieR(const dwarf::die &die) {
    using namespace dwarf;
    for (auto &child : die) loadDieR(child);
    if (die.tag != DW_TAG::subprogram) return;
    if (!die.has(DW_AT::low_pc)) return;

    auto addr = at_low_pc(die);
    if (addr == 0) {
      LOGI("low_pc is 0");
      dump_die(die);
      return;
    }

    auto found = dies[addr];
    if (!found.valid()) {
      dies[addr] = die;
    } else if (!sameDie(found, die)) {
      conflictedDies.push_back(die);
    }
  }
 public:
  DebugInfo(const ELF &elf): elf_(elf) {
    auto cus = elf.dw_->compilation_units();
    for (auto &cu: cus) loadDieR(cu.root());
    LOGI("indexed %d dies, conflicts %d", (int)dies.size(), (int)conflictedDies.size());
  }
  const dwarf::die die(uint64_t addr) const {
    auto itr = dies.find(addr);
    if (itr != dies.end()) return itr->second;
    return dwarf::die();
  }
 private:
  const ELF &elf_;
  std::map<uint64_t, dwarf::die> dies;
  vector<dwarf::die> conflictedDies;
};
class DebugInfoWrap : public Napi::ObjectWrap<DebugInfoWrap> {
 public:
  static void Init(Napi::Env env, Napi::Object exports) {
    auto methods = {
      InstanceMethod("srcline", &DebugInfoWrap::srcline),
      InstanceMethod("release", &DebugInfoWrap::release),
    };
    exports.Set("DebugInfoWrap", DefineClass(env, "DebugInfoWrap", methods));
  }
  DebugInfoWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<DebugInfoWrap>(info) {
    auto obj = info[0].As<Napi::Object>();
    ELFWrap* ef = Napi::ObjectWrap<ELFWrap>::Unwrap(obj);
    debugInfo_ = make_unique<DebugInfo>(*ef->elf_);
  }
 private:
  unique_ptr<DebugInfo> debugInfo_;
  Napi::Value srcline(const Napi::CallbackInfo& info) {
    uint64_t addr = info[0].As<Napi::Number>().Int64Value();
    auto die = debugInfo_->die(addr);
    if (!die.valid()) die = debugInfo_->die(addr - 1);

    Napi::Env env = info.Env();
    Napi::Object obj = Napi::Object::New(env);
    if (!die.valid()) return obj;

    auto src = decl_file(die);
    auto line = decl_line(die);
    obj.Set(Napi::String::New(env, "src"), Napi::String::New(env, src));
    obj.Set(Napi::String::New(env, "line"), Napi::Number::New(env, line));
    return obj;
  }
  Napi::Value release(const Napi::CallbackInfo& info) {
    TRACE();
    debugInfo_.reset();
    Napi::Env env = info.Env();
    return env.Null();
  }
};

void ELFWrap::Init(Napi::Env env, Napi::Object exports) {
  auto methods = {
    InstanceMethod("info", &ELFWrap::info),
    InstanceMethod("functions", &ELFWrap::functions),
    // InstanceMethod("release", &ELFWrap::release),
  };
  Napi::Function func = DefineClass(env, "ELFWrap", methods);
  exports.Set("ELFWrap", func);
  exports.Set(Napi::String::New(env, "demangleCppName"), Napi::Function::New(env, demangleCppName));
  DebugInfoWrap::Init(env, exports);
}

ELFWrap::ELFWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<ELFWrap>(info) {
  TRACE();
  Napi::Env env = info.Env();

  int length = info.Length();

  if (length <= 0 || !info[0].IsString()) {
    Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
    return;
  }

  Napi::String path = info[0].As<Napi::String>();
  LOGI("path %s", path.Utf8Value().c_str());
  elf_ = ELF::create(path.Utf8Value().c_str());
}

Napi::Value ELFWrap::functions(const Napi::CallbackInfo& info) {
  auto functions = elf_->functionSymbols();
  int count = functions.size();
  LOGI("count %d", count);

  Napi::Env env = info.Env();
  auto result = Napi::Array::New(env, count);
  for (int i = 0; i < count; i++) {
    auto &fn = functions[i];
    Napi::Object obj = Napi::Object::New(env);
    obj.Set(Napi::String::New(env, "name"), Napi::String::New(env, fn.name));
    obj.Set(Napi::String::New(env, "address"), Napi::Number::New(env, fn.address));
    obj.Set(Napi::String::New(env, "size"), Napi::Number::New(env, fn.size));
    result.Set(i, obj);
  }
  return result;
}

Napi::Value ELFWrap::info(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::Object obj = Napi::Object::New(env);
  auto arch = archString(elf_->arch());
  auto buildid = bytes_to_hex_string(elf_->buildId());
  auto vaddr = elf_->vaddr();
  obj.Set(Napi::String::New(env, "arch"), Napi::String::New(env, arch));
  obj.Set(Napi::String::New(env, "buildid"), Napi::String::New(env, buildid));
  obj.Set(Napi::String::New(env, "vaddr"), Napi::Number::New(env, vaddr));
  return obj;
}

