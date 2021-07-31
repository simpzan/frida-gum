#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void *getCppFunctionAddress();
const char *getCppFunctionName();
const char *getCppFunctionFilename();
int getCppFunctionLineNumber();

#ifdef __cplusplus
}
#endif