#pragma once

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static inline int getTimeString(char *buffer, int length) {
    time_t timer;
    time(&timer);
    struct tm tm_info;
    localtime_r(&timer, &tm_info);
    strftime(buffer, length, "%m-%d %H:%M:%S", &tm_info);
    return 0;
}

#if __APPLE__
#include <pthread.h>
static inline uint64_t gettid() {
    uint64_t tid = 0;
    pthread_threadid_np(NULL, &tid);
    return tid;
}
#endif

#define LOG_PRINT(level, format, args...) do { \
    char buffer[16] = {0}; getTimeString(buffer, 16); \
    printf("%s %s %d:%u %s:%d:%s " format "\n", buffer, level, getpid(), \
        (uint32_t)gettid(), (char *)__FILE__, __LINE__, __FUNCTION__, ##args); \
} while (0)
#define ERRNO(format, args...) LOG_PRINT("E", format ", errno %d %s", \
    ##args, errno, strerror(errno))
#define LOGE(format, args...) LOG_PRINT("E", format, ##args)
#define LOGW(format, args...) LOG_PRINT("W", format, ##args)
#define LOGI(format, args...) LOG_PRINT("I", format, ##args)
#define LOGD(format, args...) LOG_PRINT("D", format, ##args)
#define LOGV(format, args...) LOG_PRINT("V", format, ##args)

#define TRACE() LOGI("%s", __FUNCTION__)