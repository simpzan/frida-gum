#include "elf/elf++.hh"
#include "dwarf/dwarf++.hh"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <memory>
#include <iostream>
#include <string>
#include <map>
#include <node.h>
#include <cxxabi.h>
#include "log.h"

using namespace v8;
using namespace std;

static std::string demangle(const char *mangled_name, bool quiet = true) {
  int status = 0;
  const char *realname = abi::__cxa_demangle(mangled_name, 0, 0, &status);
  std::string res;
  switch (status) {
  case 0:
    // if (quiet) {
    //   puts(realname);
    // } else {
    //   printf("%s  %s\n", realname, mangled_name);
    // }
    res = realname;
    break;
  case -1:
    printf("FAIL: failed to allocate memory while demangling %s\n",
           mangled_name);
    break;
  case -2:
    // printf("FAIL: %s is not a valid name under the C++ ABI mangling rules\n",
    //        mangled_name);
    res = mangled_name;
    break;
  default:
    printf("FAIL: some other unexpected error: %d\n", status);
    break;
  }
  free((void *)realname);
  return res;
}

void demangle(const FunctionCallbackInfo<Value> &args) {
  Isolate *isolate = args.GetIsolate();

  // Check the number of arguments passed.
  if (args.Length() != 1) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong number of arguments")
            .ToLocalChecked()));
    return;
  }

  // Check the argument types
  if (!args[0]->IsString()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong arguments").ToLocalChecked()));
    return;
  }

  v8::String::Utf8Value str1(isolate, args[0]);
  std::string res = demangle(*str1);

  args.GetReturnValue().Set(
      String::NewFromUtf8(isolate, res.c_str()).ToLocalChecked());
}


bool find_pc(const dwarf::die &d, dwarf::taddr pc, vector<dwarf::die> *stack) {
  using namespace dwarf;

  // Scan children first to find most specific DIE
  bool found = false;
  for (auto &child : d) {
          if ((found = find_pc(child, pc, stack)))
                  break;
  }
  switch (d.tag) {
  case DW_TAG::subprogram:
  // case DW_TAG::inlined_subroutine:
          try {
                  if (found || die_pc_range(d).contains(pc)) {
                          found = true;
                          stack->push_back(d);
                  }
          } catch (out_of_range &e) {
          } catch (value_type_mismatch &e) {
          }
          break;
  default:
          break;
  }
  return found;
}

void dump_die(const dwarf::die &node, bool show = false) {
  printf("<%" PRIx64 "> %s\n",
          node.get_section_offset(),
          to_string(node.tag).c_str());
  auto attributes = node.attributes();
  int i = 0, total = attributes.size();
  for (auto &attr : attributes) {
    auto key = to_string(attr.first);
    auto value = to_string(attr.second);
    INFO("%d/%d %s %s", ++i, total, key.c_str(), value.c_str());
  }
}


typedef struct {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    uint8_t data[];
} ElfNoteSection_t;
#ifndef NT_GNU_BUILD_ID
#define NT_GNU_BUILD_ID 3
#endif

std::vector<uint8_t> get_build_id(const elf::section &section) {
  ElfNoteSection_t *nhdr = (ElfNoteSection_t *)section.data();
  if (nhdr->type != NT_GNU_BUILD_ID) return {};
  if (strcmp((char *)nhdr->data, "GNU")) return {};
  const uint8_t *bytes = nhdr->data + nhdr->namesz;
  std::vector<uint8_t> id(bytes, bytes + nhdr->descsz);
  return id;
}
void getBuidId(const elf::elf &f) {
  std::vector<uint8_t> id;
  for (auto &sec : f.sections()) {
    if (sec.get_name() == ".note.gnu.build-id") {
      id = get_build_id(sec);
      break;
    }
  }
  for (auto byte: id) printf("%02x", byte);
  printf("\n");
}

int64_t getVirtualAddress(const elf::elf &ef) {
  using namespace elf;
  for (auto &seg : ef.segments()) {
    auto &hdr = seg.get_hdr();
    if (hdr.type != pt::load) continue;
    if ((hdr.flags & pf::x) != pf::x) continue;
    return hdr.vaddr;
  }
  return -1;
}

bool sameDie(const dwarf::die &die1, const dwarf::die &die2) {
  auto name1 = die1.resolve(dwarf::DW_AT::linkage_name).as_string();
  auto name2 = die2.resolve(dwarf::DW_AT::linkage_name).as_string();
  name1 = demangle(name1.c_str());
  name2 = demangle(name2.c_str());
  auto result = name1 == name2;
  if (!result) INFO("%s %s %d", name1.c_str(), name2.c_str(), result);
  return result;
}

class SourceLineReader {
  void loadDieR(const dwarf::die &die) {
    using namespace dwarf;
    for (auto &child : die) loadDieR(child);
    if (die.tag != DW_TAG::subprogram) return;
    if (!die.has(DW_AT::low_pc)) return;

    auto addr = at_low_pc(die);
    if (addr == 0) return;

    auto found = functions[addr];
    if (!found.valid()) {
      functions[addr] = die;
    } else if (!sameDie(found, die)) {
      INFO("merged function %lx", addr);
      dump_die(found);
      dump_die(die);
      INFO("\n\n");
    }
  }
  void loadFunctionInfo() {
    for (auto &cu: cus) loadDieR(cu.root());
  }
  std::map<uint64_t, dwarf::die> functions;
  elf::elf *ef = nullptr;
  dwarf::dwarf *dw = nullptr;
  std::vector<dwarf::compilation_unit> cus;
  uint64_t vaddr = 0;
public:
  uint64_t getVirtualAddress() { return vaddr; }
  void init(const char *file) {
    int fd = open(file, O_RDONLY);
    if (fd < 0) {
            fprintf(stderr, "%s: %s\n", file, strerror(errno));
            return;
    }

    ef = new elf::elf(elf::create_mmap_loader(fd));
    dw = new dwarf::dwarf(dwarf::elf::create_loader(*ef));
    cus = dw->compilation_units();
    // INFO("%d", (int)cus.size());
    loadFunctionInfo();
    getBuidId(*ef);
    vaddr = ::getVirtualAddress(*ef);
    INFO("vaddr %lx", vaddr);
  }
  ~SourceLineReader() {
    cus.clear();
    if (ef) delete ef;
    if (dw) delete dw;
  }
  std::string srcline(const char *addr) {
    using namespace dwarf;
    dwarf::taddr pc = stoll(addr, nullptr, 0);
    auto die = functions[pc];
    if (!die.valid()) return "";

    auto &cu = (compilation_unit &) die.get_unit();

    auto decl_file_value = die.resolve(dwarf::DW_AT::decl_file);
    if (decl_file_value.valid()) {
      auto decl_file = decl_file_value.as_uconstant();
      auto lt = cu.get_line_table();
      if (decl_file == 0) return "";

      auto file = lt.get_file(decl_file);
      auto line = die.resolve(dwarf::DW_AT::decl_line).as_uconstant();
      return file->path + ":" + std::to_string(line);
    }

    if (die.has(dwarf::DW_AT::artificial) && die[dwarf::DW_AT::artificial].as_flag()) {
      return to_string(cu.root()[dwarf::DW_AT::name]);
    }
    return "";
  }
};
SourceLineReader srclineReader;

void srcline(const FunctionCallbackInfo<Value> &args) {
  Isolate *isolate = args.GetIsolate();

  // Check the number of arguments passed.
  if (args.Length() != 1) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong number of arguments")
            .ToLocalChecked()));
    return;
  }

  // Check the argument types
  if (!args[0]->IsString()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong arguments").ToLocalChecked()));
    return;
  }

  std::string res;
  try {
    v8::String::Utf8Value str1(isolate, args[0]);
    res = srclineReader.srcline(*str1);
  } catch(const std::exception& e) {
    std::cerr << e.what() << '\n';
  }

  args.GetReturnValue().Set(
      String::NewFromUtf8(isolate, res.c_str()).ToLocalChecked());
}
void startDwarf(const FunctionCallbackInfo<Value> &args) {
  Isolate *isolate = args.GetIsolate();
  if (args.Length() != 1) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong number of arguments")
            .ToLocalChecked()));
    return;
  }

  if (!args[0]->IsString()) {
    isolate->ThrowException(Exception::TypeError(
        String::NewFromUtf8(isolate, "Wrong arguments").ToLocalChecked()));
    return;
  }

  v8::String::Utf8Value str1(isolate, args[0]);
  std::string res = std::string(*str1);
  srclineReader.init(res.c_str());

  args.GetReturnValue().Set(
      String::NewFromUtf8(isolate, res.c_str()).ToLocalChecked());
}
void stopDwarf(const FunctionCallbackInfo<Value> &args) {

}
void getVirtualAddress(const FunctionCallbackInfo<Value> &args) {
  Isolate* isolate = args.GetIsolate();
  Local<Number> num = Number::New(isolate, srclineReader.getVirtualAddress());
  args.GetReturnValue().Set(num);
}
void Init(v8::Local<v8::Object> exports, v8::Local<v8::Value>, void*) {
  NODE_SET_METHOD(exports, "demangle", demangle);
  NODE_SET_METHOD(exports, "srcline", srcline);
  NODE_SET_METHOD(exports, "startDwarf", startDwarf);
  NODE_SET_METHOD(exports, "stopDwarf", stopDwarf);
  NODE_SET_METHOD(exports, "getVirtualAddress", getVirtualAddress);
}

NODE_MODULE(addon, Init)
