#include <napi.h>
#include "myobject.h"
#include "elf_file.h"

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  MyObject::Init(env, exports);
  ELFFile::Init(env, exports);
  return exports;
}

NODE_API_MODULE(addon, InitAll)
