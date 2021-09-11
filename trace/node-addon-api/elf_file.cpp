#include <string>
#include <map>
#include <vector>
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
    int fd = open(file, O_RDONLY);
    if (fd < 0) {
      ERRNO("open('%s', readonly)", file);
      return nullptr;
    }

    auto ef = make_unique<elf::elf>(elf::create_mmap_loader(fd));
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
  unique_ptr<elf::elf> ef_;
  unique_ptr<dwarf::dwarf> dw_;
  Arch arch_;
};

string archString(ELF::Arch arch) {
  switch (arch) {
  case ELF::Arch::arm32: return "arm32";
  case ELF::Arch::arm64: return "arm64";
  default: return "unknown";
  }
}


static std::string demangle(const char *mangled_name, bool quiet = true) {
  int status = 0;
  const char *realname = abi::__cxa_demangle(mangled_name, 0, 0, &status);
  std::string res;
  switch (status) {
  case 0:
    res = realname;
    break;
  case -1:
    LOGE("FAIL: failed to allocate memory while demangling %s", mangled_name);
    break;
  case -2:
    // printf("FAIL: %s is not a valid name under the C++ ABI mangling rules\n",
    //        mangled_name);
    res = mangled_name;
    break;
  default:
    LOGE("FAIL: some other unexpected error: %d", status);
    break;
  }
  free((void *)realname);
  return res;
}
Napi::Value demangleCppName(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::String name = info[0].As<Napi::String>();
  auto demangled = demangle(name.Utf8Value().c_str());
  LOGI("name %s -> %s", name.Utf8Value().c_str(), demangled.c_str());
  return Napi::String::New(env, demangled);
}


class DebugInfo : public Napi::ObjectWrap<DebugInfo> {
 public:
  static void Init(Napi::Env env, Napi::Object exports) {
    auto methods = {
      InstanceMethod("srcline", &DebugInfo::srcline),
      InstanceMethod("release", &DebugInfo::release),
    };
    exports.Set("DebugInfo", DefineClass(env, "DebugInfo", methods));
  }
  DebugInfo(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<DebugInfo>(info) {
    auto obj = info[0].As<Napi::Object>();
    ELFFile* ef = Napi::ObjectWrap<ELFFile>::Unwrap(obj);
    LOGI("elf %p", ef);
  }
 private:
  Napi::Value srcline(const Napi::CallbackInfo& info) {
    uint64_t addr = info[0].As<Napi::Number>().Int64Value();
    LOGI("addr %lx", addr);

    Napi::Env env = info.Env();
    Napi::Object obj = Napi::Object::New(env);
    auto src = "test.cpp";
    auto line = 23;
    obj.Set(Napi::String::New(env, "src"), Napi::String::New(env, src));
    obj.Set(Napi::String::New(env, "line"), Napi::Number::New(env, line));
    return obj;
  }
  Napi::Value release(const Napi::CallbackInfo& info) {
    TRACE();
    Napi::Env env = info.Env();
    return env.Null();
  }
};

void ELFFile::Init(Napi::Env env, Napi::Object exports) {
  auto methods = {
    InstanceMethod("info", &ELFFile::info),
    InstanceMethod("functions", &ELFFile::functions),
    // InstanceMethod("release", &ELFFile::release),
  };
  Napi::Function func = DefineClass(env, "ELFFile", methods);
  exports.Set("ELFFile", func);
  exports.Set(Napi::String::New(env, "demangleCppName"), Napi::Function::New(env, demangleCppName));
  DebugInfo::Init(env, exports);
}

ELFFile::ELFFile(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<ELFFile>(info) {
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

Napi::Value ELFFile::functions(const Napi::CallbackInfo& info) {
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

Napi::Value ELFFile::info(const Napi::CallbackInfo& info) {
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

