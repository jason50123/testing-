#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "argparse.hh"

#define PR_ERR_FMT "(%s() at %s:%d):: "
#define PR_ERR_ARGS __func__, __FILE__, __LINE__

#define pr(fmt, ...) fprintf(stdout, fmt "\n", ##__VA_ARGS__)
#define perr(fmt, ...)                                                         \
  fprintf(stderr, PR_ERR_FMT fmt "\n", PR_ERR_ARGS, ##__VA_ARGS__)
#define perrno(fmt, ...)                                                       \
  perr(fmt ": %d (%s)", ##__VA_ARGS__, errno, strerror(errno))

#define hintFlag(flag) pr("Set flag: '%s'", flag)
#define hintKey(key, val) pr("Set option: '%s'='%s'", key, val)

typedef int (*valHandler_t)(const char *s, void *d);

int val2str(const char *src, void *dst) {
  *(const char **)dst = src;
  return 0;
}

int val2u8(const char *src, void *dst) {
  *(uint8_t *)dst = atoi(src) & 0xFF;
  return 0;
}

int val2u32(const char *src, void *dst) {
  *(uint32_t *)dst = atoi(src) & 0xFFFFFFFF;
  return 0;
}

int val2fd(const char *src, void *dst) {
  auto fd = (int *)dst;
  if (*fd) {
    perr("Already opended");
    return -1;
  }

  if ((*fd = open(src, O_RDWR, 0)) <= 0) {
    perrno("failed to open device");
    return -1;
  }
  return 0;
}

void setArgs(int argc, const char *argv[], KVOpt keys[], size_t numKeys,
             FlagOpt_t flags[], size_t numFlags) {
  for (int iArg = 1; iArg < argc; ++iArg) {
    auto curr = argv[iArg];

    if (curr[0] != '-') {
      perr("Expect to be '--key value' or '-flag', but got '%s'", curr);
      goto usage;
    }

    if (curr[1] == '-') {
      auto key = &curr[2];
      if (iArg + 1 >= argc) {
        perr("No value for key '%s'", key);
        goto usage;
      }

      auto notFound = true;
      auto val = argv[iArg + 1];
      for (size_t iKey = 0; notFound && iKey < numKeys; ++iKey) {
        if (!strcmp(key, keys[iKey].key)) {
          hintKey(key, val);
          if (keys[iKey].handler(val, keys[iKey].val))
            goto usage;

          keys[iKey].updated = true;
          ++iArg;
          notFound = false;
        }
      }
      if (notFound) {
        perr("Unknown key: '%s'", key);
        goto usage;
      }
    }
    else {
      auto flag = &curr[1];
      auto notFound = true;
      for (size_t iFlag = 0; notFound && iFlag < numFlags; ++iFlag) {
        if (!strcmp(flag, flags[iFlag].key)) {
          hintFlag(flags[iFlag].key);
          *flags[iFlag].val = true;

          notFound = false;
        }
      }
      if (notFound) {
        perr("Unknown flag: '%s'", flag);
        goto usage;
      }
    }
  }

  for (size_t i = 0; i < numKeys; ++i) {
    if (keys[i].required && !keys[i].updated) {
      pr("option '--%s' is required but not given", keys[i].key);
      exit(-1);
    }
  }
  return;

usage:
  pr("Usage: ./program [--key val | -flag]");
  pr("Keys:");
  for (size_t i = 0; i < numKeys; ++i)
    pr("\t--%s%s: %s", keys[i].key, keys[i].required ? " (required) " : "",
       keys[i].desc);

  pr("Flags:");
  for (size_t i = 0; i < numFlags; ++i)
    pr("\t-%s: %s", flags[i].key, flags[i].desc);

  exit(-1);
}

// Example Usage:
//
// int main(int argc, const char *argv[]) {
//   struct ProgramConfig {
//     uint32_t a;
//     int b;
//     uint8_t c;

//     bool t;
//   } config;
//   memset(&config, 0, sizeof(config));

//   FlagOpt_t flagMapping[] = {
//       {"t", &config.t, "test only"},
//   };
//   KVOpt_t keyMapping[] = {
//       {"a", &config.a, val2u32, 1, 0, "a"},
//       {"b", &config.b, val2fd, 1, 0, "path to b"},
//       {"c", &config.c, val2u8, 1, 0, "c"},
//   };

//   auto numFlags = sizeof(flagMapping) / sizeof(flagMapping[0]);
//   auto numKeys = sizeof(keyMapping) / sizeof(keyMapping[0]);
//   setArgs(argc, argv, keyMapping, numKeys, flagMapping, numFlags);
//   return 0;
// }
