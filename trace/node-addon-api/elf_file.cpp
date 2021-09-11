#include "elf_file.h"
#include "log.h"

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
}

Napi::Value ELFFile::GetValue(const Napi::CallbackInfo& info) {
  double num = this->value_;

  return Napi::Number::New(info.Env(), num);
}

Napi::Value ELFFile::info(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::Object obj = Napi::Object::New(env);
  auto arch = "arm";
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
