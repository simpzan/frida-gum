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

std::string bytes_to_hex_string(const std::vector<uint8_t> &input) {
  static const char characters[] = "0123456789abcdef";
  // Zeroes out the buffer unnecessarily, can't be avoided for std::string.
  std::string ret(input.size() * 2, 0);
  // Hack... Against the rules but avoids copying the whole buffer.
  char *buf = const_cast<char *>(ret.data());
  for (const auto &oneInputByte : input) {
    *buf++ = characters[oneInputByte >> 4];
    *buf++ = characters[oneInputByte & 0x0F];
  }
  return ret;
}

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
std::vector<uint8_t> getBuidId(const elf::elf &f) {
  std::vector<uint8_t> id;
  for (auto &sec : f.sections()) {
    if (sec.get_name() == ".note.gnu.build-id") {
      id = get_build_id(sec);
      break;
    }
  }
  return id;
}


std::map<uint64_t, std::string> getFunctionAddress(const elf::elf &f, elf::sht type) {
  std::map<uint64_t, std::string> functions;
  for (auto &sec : f.sections()) {
    if (sec.get_hdr().type != type) continue;
    for (auto sym : sec.as_symtab()) {
      auto &d = sym.get_data();
      if (d.type() == elf::stt::func && d.size > 0) functions[d.value] = sym.get_name();
    }
  }
  return functions;
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

std::string getName(const dwarf::die &die) {
  std::string name0;
  auto linkage_name = die.resolve(dwarf::DW_AT::linkage_name);
  if (linkage_name.valid()) name0 = linkage_name.as_string();
  else {
    auto name_die = die.resolve(dwarf::DW_AT::name);
    if (name_die.valid()) name0 = name_die.as_string();
  }
  return demangle(name0.c_str());
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

    addr = getSymbolAddr(addr);
    if (addr == 0) {
      dump_die(die);
      return;
    }

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
    isArm32 = ef->get_hdr().machine == EM_ARM;
    INFO("isArm32 %d", isArm32);
    functionSymbols = getFunctionAddress(*ef, elf::sht::symtab);
    for (auto &cu: cus) loadDieR(cu.root());
    functionSymbols.clear();
  }
  std::map<uint64_t, std::string> functionSymbols;
  uint64_t getSymbolAddr(uint64_t addr) {
    if (!isArm32) return addr;
    auto itr = functionSymbols.find(addr);
    if (itr != functionSymbols.end()) return addr;
    addr += 1;
    itr = functionSymbols.find(addr);
    if (itr != functionSymbols.end()) return addr;
    ERROR("invalid addr %lx", addr);
    return 0;
  }
  enum elfmachine {
    EM_AARCH64  = 183,
    EM_ARM      = 40,
  };
  bool isArm32 = false;
  elf::elf *ef = nullptr;
  dwarf::dwarf *dw = nullptr;
  std::vector<dwarf::compilation_unit> cus;
  uint64_t vaddr = 0;
public:
  std::map<uint64_t, dwarf::die> functions;
  uint64_t getVirtualAddress() { return vaddr; }
  std::map<uint64_t, std::string> getSymbolTable(elf::sht type) {
    return getFunctionAddress(*ef, type);
  }

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
    vaddr = ::getVirtualAddress(*ef);
    INFO("vaddr %lx, %d", vaddr, (int)functions.size());
  }
  ~SourceLineReader() {
    cus.clear();
    if (ef) delete ef;
    if (dw) delete dw;
  }
  std::string getBuidId() {
    auto bytes = ::getBuidId(*ef);
    auto id = bytes_to_hex_string(bytes);
    INFO("build id %s", id.c_str());
    return id;
  }
  std::pair<std::string, int> srcline(const char *addr) {
    using namespace dwarf;
    taddr pc = stoll(addr, nullptr, 0);
    auto die = functions[pc];
    if (!die.valid()) return {};

    auto &cu = (compilation_unit &) die.get_unit();

    auto decl_file_value = die.resolve(DW_AT::decl_file);
    if (decl_file_value.valid()) {
      auto decl_file = decl_file_value.as_uconstant();
      auto lt = cu.get_line_table();
      if (decl_file == 0) return {};

      auto file = lt.get_file(decl_file);
      auto line = die.resolve(DW_AT::decl_line).as_uconstant();
      // return file->path;
      return {file->path, line};
    }

    if (die.has(DW_AT::artificial) && die[DW_AT::artificial].as_flag()) {
      return {to_string(cu.root()[DW_AT::name]), -1};
    }
    return {};
  }
};
SourceLineReader srclineReader;

void srcline(const FunctionCallbackInfo<Value> &args) {
  Isolate *isolate = args.GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();

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

  std::pair<std::string, int> res;
  try {
    v8::String::Utf8Value str1(isolate, args[0]);
    res = srclineReader.srcline(*str1);
  } catch(const std::exception& e) {
    std::cerr << e.what() << '\n';
  }

  Local<Object> obj = Object::New(isolate);
  auto file = String::NewFromUtf8(isolate, res.first.c_str()).ToLocalChecked();
  auto line = Number::New(isolate, res.second);
  obj->Set(context, String::NewFromUtf8(isolate, "file").ToLocalChecked(), file).FromJust();
  obj->Set(context, String::NewFromUtf8(isolate, "line").ToLocalChecked(), line).FromJust();
  args.GetReturnValue().Set(obj);
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
void getBuidId(const FunctionCallbackInfo<Value> &args) {
  Isolate* isolate = args.GetIsolate();
  auto id = srclineReader.getBuidId();
  auto idValue = String::NewFromUtf8(isolate, id.c_str()).ToLocalChecked();
  args.GetReturnValue().Set(idValue);
}
void getFunctions(const FunctionCallbackInfo<Value> &args) {
  Isolate* isolate = args.GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();
  Local<Array> myArray = Array::New(isolate);
  int i = 0;
  for (auto &entry: srclineReader.functions) {
    Local<Object> obj = Object::New(isolate);
    Local<Number> addr = Number::New(isolate, entry.first);
    auto die = entry.second;
    obj->Set(context, String::NewFromUtf8(isolate, "addr").ToLocalChecked(), addr).FromJust();
    auto name = String::NewFromUtf8(isolate, getName(die).c_str()).ToLocalChecked();
    obj->Set(context, String::NewFromUtf8(isolate, "name").ToLocalChecked(), name).FromJust();
    Local<Number> size = Number::New(isolate, at_high_pc(die) - at_low_pc(die));
    obj->Set(context, String::NewFromUtf8(isolate, "size").ToLocalChecked(), size).FromJust();
    myArray->Set(context, i, obj).FromJust();
    ++i;
  }
  args.GetReturnValue().Set(myArray);
}
void getSymbolTable(const FunctionCallbackInfo<Value> &args) {
  Isolate* isolate = args.GetIsolate();
  int value = args[0].As<Number>()->Value();
  auto type = value ? elf::sht::dynsym : elf::sht::symtab;
  Local<Context> context = isolate->GetCurrentContext();
  Local<Array> myArray = Array::New(isolate);
  int i = 0;
  for (auto &entry: srclineReader.getSymbolTable(type)) {
    Local<Object> obj = Object::New(isolate);
    Local<Number> addr = Number::New(isolate, entry.first);
    obj->Set(context, String::NewFromUtf8(isolate, "addr").ToLocalChecked(), addr).FromJust();
    auto name = String::NewFromUtf8(isolate, entry.second.c_str()).ToLocalChecked();
    obj->Set(context, String::NewFromUtf8(isolate, "name").ToLocalChecked(), name).FromJust();
    myArray->Set(context, i, obj).FromJust();
    ++i;
  }
  args.GetReturnValue().Set(myArray);
}
void Init(v8::Local<v8::Object> exports, v8::Local<v8::Value>, void*) {
  NODE_SET_METHOD(exports, "demangle", demangle);
  NODE_SET_METHOD(exports, "srcline", srcline);
  NODE_SET_METHOD(exports, "startDwarf", startDwarf);
  NODE_SET_METHOD(exports, "stopDwarf", stopDwarf);
  NODE_SET_METHOD(exports, "getVirtualAddress", getVirtualAddress);
  NODE_SET_METHOD(exports, "getBuidId", getBuidId);
  NODE_SET_METHOD(exports, "getFunctions", getFunctions);
  NODE_SET_METHOD(exports, "getSymbolTable", getSymbolTable);
}

NODE_MODULE(addon, Init)
