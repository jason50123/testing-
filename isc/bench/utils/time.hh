#include <cassert>
#include <cstdint>
#include <ctime>

static inline uint64_t ts2ns(struct timespec *ts) {
  return 1000000000ULL * ts->tv_sec + ts->tv_nsec;
}

#ifndef NO_M5
#include "m5/m5_mmap.h"
#include "m5/m5ops.h"
#else
void map_m5_mem() {}

uint64_t m5_get_tick(struct timespec *ts) {
  uint64_t tick;
  assert(0 == clock_gettime(CLOCK_REALTIME, ts));

  tick = ts2ns(ts);
  tick *= 1000;
  return tick;
}
#endif

