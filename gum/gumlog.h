
#include <stdio.h>
#include <unistd.h>
#include <libgen.h>

#define LOG_PRINT(level, format, args...) do { \
    char buffer[16] = {0}; \
    printf("%s %s %d:%u %s:%d " format "\n", buffer, level, getpid(), \
        (uint32_t)0, basename(__FILE__), __LINE__, ##args); \
} while (0)
#define ERRNO(format, args...) LOG_PRINT("E", format ", errno %d %s", \
    ##args, errno, strerror(errno))
#define ERROR(format, args...) LOG_PRINT("E", format, ##args)
#define WARN(format, args...) LOG_PRINT("W", format, ##args)
#define INFO(format, args...) LOG_PRINT("I", format, ##args)
#define DEBUG(format, args...) LOG_PRINT("D", format, ##args)
#define VERBOSE(format, args...) LOG_PRINT("V", format, ##args)
#define TRACE() DEBUG("%s", __FUNCTION__)
