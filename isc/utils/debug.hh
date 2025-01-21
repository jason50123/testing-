#ifndef __SIMPLESSD_ISC_UTILS_LOG_HH__
#define __SIMPLESSD_ISC_UTILS_LOG_HH__

#include <unistd.h>

#ifdef ISC_TEST
#include <cassert>
#include <cerrno>
#include <cstdio>   // for printf
#include <cstring>  // for for strerror, memcpy
#else
#include "util/simplessd.hh"
#endif

// 2-level to expand macro:
// https://gcc.gnu.org/onlinedocs/gcc-13.2.0/cpp/Stringizing.html
#define _str(s) #s
#define str(s) _str(s)

#define _X_Y(x, y) x##_##y
#define X_Y(x, y) _X_Y(x, y)

// no need to use -Wno-unused-value
static inline void unused_values(...) {}

constexpr bool const_strncmp(const char *a, const char *b, int n) {
  return  //
      (n <= 0)                       ? false
      : ((*a) && (*b) && (*a == *b)) ? const_strncmp(a + 1, b + 1, n - 1)
                                     : true;
}

namespace SimpleSSD {
namespace Utils {

#define DPR_ERR_BEG_COLOR "\e[37;41;1m"
#define DPR_ERR_END_COLOR "\e[0m"

void do_pipe2xxd(const char *, size_t, const char *, void *, size_t,
                 const char *);
#define pipe2xxd(...) do_pipe2xxd(__func__, __LINE__, ##__VA_ARGS__)

void do_pipe2md5(const void *, size_t, char[32]);
#define pipe2md5(dat, dlen, out)                                               \
  {                                                                            \
    pr("Call pipe2md5 from %s:%d", __func__, __LINE__);                        \
    do_pipe2md5(dat, dlen, out);                                               \
  }

void bt(int count = -1, int skip = 0, bool noBuiltin = true);

// Statically LOG_ID checking is enabled by default. Redefine this macro to 1 to
// disable the checking
#define NO_LOG_ID_CHECK 0

#ifdef ISC_TEST
#ifndef ISC_TEST_NO_DPR
#define debugprint(id, fmt, ...)                                               \
  do {                                                                         \
    /*  use `define NO_LOG_ID_CHECK 1` to disable the checking */              \
    static_assert(NO_LOG_ID_CHECK || !const_strncmp(str(id), "LOG_ISC", 7),    \
                  "Unexpected LOG_ID prefix");                                 \
    printf("%-30s @ %20s ::\t" fmt "\n", __FILE__ ":" str(__LINE__), str(id),  \
           ##__VA_ARGS__);                                                     \
  } while (0)
#else
#define debugprint(id, fmt, ...)                                               \
  do {                                                                         \
    /*  use `define NO_LOG_ID_CHECK 1` to disable the checking */              \
    static_assert(NO_LOG_ID_CHECK || !const_strncmp(str(id), "LOG_ISC", 7),    \
                  "Unexpected LOG_ID prefix");                                 \
    unused_values(fmt, ##__VA_ARGS__);                                         \
  } while (0)  // will be evaluated
#endif

#define panic(fmt, ...)                                                        \
  do {                                                                         \
    printf(DPR_ERR_BEG_COLOR "!!PANIC@%s\t" fmt DPR_ERR_END_COLOR "\n",        \
           __PRETTY_FUNCTION__, ##__VA_ARGS__);                                \
    sleep(5);                                                                  \
  } while (1)
#endif

#undef PR_SECTION
#define pr(fmt, ...) debugprint(PR_SECTION, fmt, ##__VA_ARGS__)
#define perr(fmt, ...)                                                         \
  pr(DPR_ERR_BEG_COLOR fmt ": %s(%d)" DPR_ERR_END_COLOR, ##__VA_ARGS__,        \
     strerror(errno), errno)

}  // namespace Utils
}  // namespace SimpleSSD

#endif  // __SIMPLESSD_ISC_UTILS_LOG_HH__