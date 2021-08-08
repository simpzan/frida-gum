#include "elf/elf++.hh"
#include "dwarf/dwarf++.hh"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <memory>
#include <string>
#include <node.h>
#include <cxxabi.h>

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


class SourceLineReader {
  elf::elf *ef = nullptr;
  dwarf::dwarf *dw = nullptr;
  std::vector<dwarf::compilation_unit> cus;
public:
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
  }
  ~SourceLineReader() {
    cus.clear();
    if (ef) delete ef;
    if (dw) delete dw;
  }
  std::string srcline(const char *addr) {
    dwarf::taddr pc = stoll(addr, nullptr, 0);
    // INFO("%s %p", addr, (void *)pc);
    for (auto &cu: cus) {
      auto root = cu.root();
      auto range = die_pc_range(root);
      auto found = range.contains(pc);
      if (found) {
          auto &lt = cu.get_line_table();
          auto it = lt.find_address(pc);
          return it == lt.end() ? "" : it->get_description();
      }
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

  v8::String::Utf8Value str1(isolate, args[0]);
  std::string res = srclineReader.srcline(*str1);

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
void Init(Local<Object> exports) {
  NODE_SET_METHOD(exports, "demangle", demangle);
  NODE_SET_METHOD(exports, "srcline", srcline);
  NODE_SET_METHOD(exports, "startDwarf", startDwarf);
  NODE_SET_METHOD(exports, "stopDwarf", stopDwarf);
}

NODE_MODULE(addon, Init)
