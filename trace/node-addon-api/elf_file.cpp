#include <string>
#include "elf_file.h"
#include "log.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "elf/elf++.hh"
#include "dwarf/dwarf++.hh"

using namespace std;

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

static Napi::FunctionReference* constructor = nullptr;

void ELFFile::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func =
      DefineClass(env,
                  "ELFFile",
                  {InstanceMethod("info", &ELFFile::info),
                   InstanceMethod("value", &ELFFile::GetValue),
                   InstanceMethod("multiply", &ELFFile::Multiply)});

  constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  // env.SetInstanceData(constructor);

  exports.Set("ELFFile", func);
  TRACE();
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

Napi::Value ELFFile::GetValue(const Napi::CallbackInfo& info) {
  double num = this->value_;

  return Napi::Number::New(info.Env(), num);
}

Napi::Value ELFFile::info(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::Object obj = Napi::Object::New(env);
  auto arch = archString(elf_->arch());
  auto buildid = "this is a build-id";
  int vaddr = 0x1234;
  obj.Set(Napi::String::New(env, "arch"), Napi::String::New(env, arch));
  obj.Set(Napi::String::New(env, "buildid"), Napi::String::New(env, buildid));
  obj.Set(Napi::String::New(env, "vaddr"), Napi::Number::New(env, vaddr));
  return obj;
}

Napi::Value ELFFile::Multiply(const Napi::CallbackInfo& info) {
  Napi::Number multiple;
  if (info.Length() <= 0 || !info[0].IsNumber()) {
    multiple = Napi::Number::New(info.Env(), 1);
  } else {
    multiple = info[0].As<Napi::Number>();
  }

  Napi::Object obj = constructor->New(
      {Napi::Number::New(info.Env(), this->value_ * multiple.DoubleValue())});

  return obj;
}
