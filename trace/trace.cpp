#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <libgen.h>
#include <unordered_set>
#include <mutex>
#include <gum/guminterceptor.h>
#include <gum/gumlog.h>

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
    flush();
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
  std::unique_lock<std::mutex> lock(buffersMutex);
  for (auto buf: buffers) buf->flush();
}
__attribute__((constructor)) void init (void) {
  INFO ("init");
}
__attribute__((destructor)) void finalize (void) {
  INFO ("finalize");
}

