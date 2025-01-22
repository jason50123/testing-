#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils/argparse.hh"
#include "utils/m5/m5_mmap.h"
#include "utils/m5/m5ops.h"
#include "utils/runtime.hh"

using namespace SimpleSSD::ISC;

struct TaskConfig {
  nvme_config_t nvme;
  const char *dirPath;
  uint32_t slet_id;
  bool init_runtime;
} config;

static FlagOpt_t flags[] = {
    {"dry", &config.nvme.dry, "dry run (do not issue command)"},
    {"init", &config.init_runtime, "init runtime first"},
};
static KVOpt_t keys[] = {
    {"dev", &config.nvme.devfd, val2fd, 1, 0, "path to nvme device"},
    {"ns", &config.nvme.nsid, val2u32, 1, 0, "namespace id"},
    {"dir", &config.dirPath, val2str, 1, 0, "target directory path"},
    {"id", &config.slet_id, val2u32, 1, 0, "the slet id"},
};
static size_t numFlags = sizeof(flags) / sizeof(flags[0]);
static size_t numKeys = sizeof(keys) / sizeof(keys[0]);

void statdir(const char *path);

int main(int argc, const char *argv[]) {
  memset(&config, 0, sizeof(config));
  setArgs(argc, argv, keys, numKeys, flags, numFlags);

  if (config.init_runtime) {
    if (!initRuntime(config.nvme))
      pr("Runtime Init done");
    else
      exit(-1);
  }

#ifndef NO_M5
  uint64_t tickStart, tickEnd, tickDiff;
  struct timespec nowStart, nowEnd;
  map_m5_mem();
  tickStart = m5_get_tick(&nowStart);
#endif

  // set opt
  auto data = (void *)config.dirPath;
  auto dlen = strlen(config.dirPath);

  if (!setOpt(config.slet_id, config.nvme, "path", data, dlen))
    pr("SetOpt done");
  else
    exit(-1);

  if (!startSlet(config.slet_id, config.nvme))
    pr("Start Slet done");
  else
    exit(-1);

  // get slet result
  size_t resSize;
  if (!getResultSize(config.slet_id, config.nvme, &resSize))
    pr("Get ResultSize done (%lu)", resSize);
  else
    exit(-1);

  auto result = (char *)calloc(1, resSize);
  if (!getResult(config.slet_id, config.nvme, result, resSize))
    pr("Get Result done");
  else
    exit(-1);

#ifndef NO_M5
  tickEnd = m5_get_tick(&nowEnd);
  tickDiff = tickEnd - tickStart;
  printf("Simulation Time: %lu~%lu (%lu) ps\n", tickStart, tickEnd, tickDiff);
  printf("= %lu nsecs \n", nowEnd.tv_nsec - nowStart.tv_nsec);
#endif

  typedef struct {
    uint32_t mtime;
    uint32_t size;
    uint32_t mode;
    char name[256];
  } private_t;

  pr("%-15s|%-10s|%-10s|%s", "Mod Time", "Bytes", "Perm", "File");
  for (size_t ofs = 0; ofs < resSize;) {
    auto d = (private_t *)(result + ofs);
    pr("%-15u|%-10u|%-10o|%s", d->mtime, d->size, d->mode, d->name);
    ofs += sizeof(private_t);
  }

  return 0;
}
