#pragma once

#include <memory>
#include <napi.h>

class ELF;
class ELFFile : public Napi::ObjectWrap<ELFFile> {
 public:
  static void Init(Napi::Env env, Napi::Object exports);
  ELFFile(const Napi::CallbackInfo& info);

 private:
  Napi::Value functions(const Napi::CallbackInfo& info);
  Napi::Value info(const Napi::CallbackInfo& info);
  Napi::Value Multiply(const Napi::CallbackInfo& info);

  double value_;
  std::unique_ptr<ELF> elf_;
};

