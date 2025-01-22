#ifndef __SIMPLESSD_ISC_BENCH_UTILS_ARGPARSE_HH__
#define __SIMPLESSD_ISC_BENCH_UTILS_ARGPARSE_HH__

#include <unistd.h>

typedef int (*valHandler_t)(const char *s, void *d);

extern int val2str(const char *src, void *dst);
extern int val2u8(const char *src, void *dst);
extern int val2u32(const char *src, void *dst);
extern int val2fd(const char *src, void *dst);

typedef struct KVOpt {
  const char *key;
  void *val;
  valHandler_t handler;
  bool required;
  bool updated;
  const char *desc;
} KVOpt_t;

typedef struct FlagOpt {
  const char *key;
  bool *val;
  const char *desc;
} FlagOpt_t;

extern void setArgs(int argc, const char *argv[], KVOpt keys[], size_t numKeys,
                    FlagOpt_t flags[], size_t numFlags);

#endif /* __SIMPLESSD_ISC_BENCH_UTILS_ARGPARSE_HH__ */