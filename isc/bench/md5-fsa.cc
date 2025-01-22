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
#include "utils/runtime.hh"
#include "utils/time.hh"

using namespace SimpleSSD::ISC;

struct TaskConfig {
  nvme_config_t nvme;
  const char *path;
  uint32_t slet_id;
  bool init_runtime;
} config;

static FlagOpt_t flags[] = {
    {"dry", &config.nvme.dry, "dry run (do not issue command)"},
    {"init", &config.init_runtime, "init runtime first"},
};
static KVOpt_t keys[] = {
    {"dev", &config.nvme.devfd, val2fd, 1, 0, "path to nvme device"},
    {"ns", &config.nvme.nsid, val2u32, 0, 0, "namespace id"},
    {"path", &config.path, val2str, 1, 0, "target path"},
    {"id", &config.slet_id, val2u32, 1, 0, "slet id"},
};
static size_t numFlags = sizeof(flags) / sizeof(flags[0]);
static size_t numKeys = sizeof(keys) / sizeof(keys[0]);

int main(int argc, const char *argv[]) {
  memset(&config, 0, sizeof(config));
  config.nvme.nsid = 1;
  setArgs(argc, argv, keys, numKeys, flags, numFlags);

  if (config.init_runtime) {
    if (initRuntime(config.nvme))
      exit(-__LINE__);
    pr("Runtime Init done");
  }

  // record start time
  uint64_t tickStart, tickEnd, tickDiff;
  struct timespec nowStart, nowEnd;
  map_m5_mem();
  tickStart = m5_get_tick(&nowStart);

  // set opt
  auto data = (void *)config.path;
  auto dlen = strlen(config.path);

  if (setOpt(config.slet_id, config.nvme, "path", data, dlen))
    exit(-__LINE__);
  pr("SetOpt done");

  if (startSlet(config.slet_id, config.nvme))
    exit(-__LINE__);
  pr("Start Slet done");

  // get slet result
  size_t resSize;
  if (getResultSize(config.slet_id, config.nvme, &resSize))
    exit(-__LINE__);
  pr("Get ResultSize done (%lu)", resSize);

  auto result = (char *)calloc(1, resSize);
  if (getResult(config.slet_id, config.nvme, result, resSize))
    exit(-__LINE__);
  pr("Get Result done");

  // record end time
  tickEnd = m5_get_tick(&nowEnd);
  tickDiff = tickEnd - tickStart;

  // print result and exec time
  union md5_t {
    uint8_t bytes[16];
    uint64_t dwords[2];
  };

  for (size_t iFile = 0; iFile < resSize / 16; ++iFile) {
    auto md5 = &((md5_t *)result)[iFile];
    // swap endian
    for (uint8_t i = 0, t; i < 8; ++i) {
      t = md5->bytes[i];
      md5->bytes[i] = md5->bytes[15 - i];
      md5->bytes[15 - i] = t;
    }
    printf("MD5 result: %016lx%016lx\n", md5->dwords[1], md5->dwords[0]);
  }

  printf("Simulation Time: %lu~%lu (%lu) ps\n", tickStart, tickEnd, tickDiff);
  printf("= %lu nsecs \n", nowEnd.tv_nsec - nowStart.tv_nsec);

  return 0;
}
