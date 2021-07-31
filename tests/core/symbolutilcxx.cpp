#include "symbolutilcxx.h"

namespace cppNamespace {

class CppClass {
public:
    static int cppFunction();
};

static const char *functionName = nullptr;
static int lineNumber = __LINE__;

int CppClass::cppFunction() {
    functionName = __FUNCTION__;
    int ret = 0;
    for (int i=0; i<100; ++i) ret += i;
    return ret;
}

} // namespace cppNamespace


void *getCppFunctionAddress() {
    cppNamespace::CppClass::cppFunction();
    return (void *)cppNamespace::CppClass::cppFunction;
}
const char *getCppFunctionFilename() {
    return __FILE__;
}
int getCppFunctionLineNumber() {
    return cppNamespace::lineNumber + 2;
}
const char *getCppFunctionName() {
    return cppNamespace::functionName;
}