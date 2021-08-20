#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <mutex>
#include <fstream>
#include <sstream>
#include <memory>
#include <vector>
#include <gum/guminterceptor.h>
#include <gum/gumlog.h>

using namespace std;

std::string bytes_to_hex_string(const uint8_t *bytes, int length) {
  static const char characters[] = "0123456789abcdef";
  // Zeroes out the buffer unnecessarily, can't be avoided for std::string.
  std::string ret(length * 2, 0);
  // Hack... Against the rules but avoids copying the whole buffer.
  char *buf = const_cast<char *>(ret.data());
  for (int i=0; i<length; ++i) {
    uint8_t oneInputByte = bytes[i];
    *buf++ = characters[oneInputByte >> 4];
    *buf++ = characters[oneInputByte & 0x0F];
  }
  return ret;
}

extern "C" {

#include "build-id.h"
int getBuidId(const char *name, char *bytes, int length) {
  auto note = build_id_find_nhdr_by_name(name);
  if (!note) return 0;

  if (build_id_length(note) <= 0) return 0;

  auto data = build_id_data(note);
  int datalen = build_id_length(note);
  auto id = bytes_to_hex_string(data, datalen);
  strncpy(bytes, id.c_str(), length);
  INFO("build id %s", id.c_str());
  return min(length, datalen);
}

}

static inline std::string readFile(const char *filename) {
  std::ifstream ifs(filename);
  if (ifs.fail()) ERRNO("'%s' open failed", filename);
  std::stringstream sstr;
  sstr << ifs.rdbuf();
  return sstr.str();
}
template <typename... Args>
std::string formatString(const char *format, Args... args) {
    size_t size = snprintf(nullptr, 0, format, args...) + 1;  // Extra one byte for '\0'.
    std::unique_ptr<char[]> buf(new char[size]);
    snprintf(buf.get(), size, format, args...);
    return std::string(buf.get());
}
std::string getThreadName(uint32_t tid) {
  auto file = formatString("/proc/%u/task/%u/comm", (uint32_t)getpid(), tid);
  return readFile(file.c_str());
}

typedef void (*sendDataFn)(const uint8_t *bytes, int length, int mode);
sendDataFn _sendDataFn = NULL;

typedef struct Event_ {
  uint64_t fn;
  gint64 ts;
} Event;

std::mutex buffersMutex;
struct EventBuffer;
std::unordered_set<EventBuffer *> buffers;

struct EventBuffer {
  EventBuffer() {
    TRACE();
    events = (Event *)malloc(sizeof(Event) * count);
    std::unique_lock<std::mutex> lock(buffersMutex);
    buffers.insert(this);
    tid = gettid();
  }
  ~EventBuffer() {
    flushAll();
    std::unique_lock<std::mutex> lock(buffersMutex);
    auto itr = buffers.find(this);
    if (itr != buffers.end()) buffers.erase(itr);
    free(events); events = NULL;
    TRACE();
  }
  void write(gpointer fn, int64_t ts) {
    auto event = events + current;
    event->fn = GPOINTER_TO_SIZE(fn);
    event->ts = ts;
    ++current;
    if (current == count) flush();
    // INFO("%p %ld", fn, ts);
  }
  void flushAll() {
    flush();
    auto tname = getThreadName(tid);
    if (_sendDataFn) _sendDataFn((uint8_t *)tname.c_str(), (int)tname.size(), -tid);
  }
 private:
  void flush() {
    INFO("%d flushing %d events", tid, current);
    if (_sendDataFn && current) _sendDataFn((uint8_t *)events, sizeof(Event) * current, tid);
    current = 0;
  }
 private:
  Event *events = NULL;
  int count = 128;
  int current = 0;
  int tid = 0;
};

thread_local EventBuffer buffer;
void recordEvent(GumInvocationContext *ic, char ph) {
  gpointer fn = gum_invocation_context_get_listener_function_data(ic);
  gint64 ts = g_get_real_time();
  if (ph == 'E') ts *= -1;
  // INFO("%p %ld", fn, ts);
  buffer.write(fn, ts);
}
extern "C" void onEnter(GumInvocationContext * ic) { recordEvent(ic, 'B'); }
extern "C" void onLeave(GumInvocationContext * ic) { recordEvent(ic, 'E'); }
extern "C" void flushAll() {
  TRACE();
  std::unique_lock<std::mutex> lock(buffersMutex);
  for (auto buf: buffers) buf->flushAll();
}
__attribute__((constructor)) void init (void) {
  INFO ("init");
}
__attribute__((destructor)) void finalize (void) {
  INFO ("finalize");
}

