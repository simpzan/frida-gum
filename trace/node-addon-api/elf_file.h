#pragma once

#include <memory>
#include <napi.h>

class ELF;
class ELFWrap : public Napi::ObjectWrap<ELFWrap> {
 public:
  static void Init(Napi::Env env, Napi::Object exports);
  ELFWrap(const Napi::CallbackInfo& info);

 private:
  Napi::Value functions(const Napi::CallbackInfo& info);
  Napi::Value info(const Napi::CallbackInfo& info);

  std::unique_ptr<ELF> elf_;
};

