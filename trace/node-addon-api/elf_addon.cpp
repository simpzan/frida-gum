#include <memory>
#include <napi.h>
#include "elf_debug_info.h"
#include "log.h"
using namespace std;

class ELFWrap : public Napi::ObjectWrap<ELFWrap> {
 public:
  static void Init(Napi::Env env, Napi::Object exports);
  ELFWrap(const Napi::CallbackInfo& info);

 private:
  Napi::Value functions(const Napi::CallbackInfo& info);
  Napi::Value info(const Napi::CallbackInfo& info);
  Napi::Value release(const Napi::CallbackInfo& info) {
    elf_.reset();
    return info.Env().Null();
  }
  std::unique_ptr<ELF> elf_;
  friend class DebugInfoWrap;
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
    // TRACE();
    debugInfo_.reset();
    Napi::Env env = info.Env();
    return env.Null();
  }
};

void ELFWrap::Init(Napi::Env env, Napi::Object exports) {
  auto methods = {
    InstanceMethod("info", &ELFWrap::info),
    InstanceMethod("functions", &ELFWrap::functions),
    InstanceMethod("release", &ELFWrap::release),
  };
  Napi::Function func = DefineClass(env, "ELFWrap", methods);
  exports.Set("ELFWrap", func);
}
ELFWrap::ELFWrap(const Napi::CallbackInfo& info): Napi::ObjectWrap<ELFWrap>(info) {
  Napi::Env env = info.Env();
  if (info.Length() <= 0 || !info[0].IsString()) {
    Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
    return;
  }

  Napi::String path = info[0].As<Napi::String>();
  // LOGI("path %s", path.Utf8Value().c_str());
  elf_ = ELF::create(path.Utf8Value().c_str());
  if (!elf_) Napi::TypeError::New(env, "failed to read the file").ThrowAsJavaScriptException();
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
  auto buildId = bytes_to_hex_string(elf_->buildId());
  auto vaddr = elf_->vaddr();
  obj.Set(Napi::String::New(env, "arch"), Napi::String::New(env, arch));
  obj.Set(Napi::String::New(env, "buildId"), Napi::String::New(env, buildId));
  obj.Set(Napi::String::New(env, "vaddr"), Napi::Number::New(env, vaddr));
  return obj;
}

Napi::Value demangleCppName(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  auto name = info[0].As<Napi::String>().Utf8Value();
  auto demangled = demangle(name.c_str());
  // LOGI("name %s -> %s", name.Utf8Value().c_str(), demangled.c_str());
  return Napi::String::New(env, demangled);
}

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  ELFWrap::Init(env, exports);
  DebugInfoWrap::Init(env, exports);
  exports.Set(Napi::String::New(env, "demangleCppName"), Napi::Function::New(env, demangleCppName));
  return exports;
}
NODE_API_MODULE(addon, InitAll)
