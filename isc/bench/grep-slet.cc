#include "utils/argparse.hh"
#include "utils/common.hh"
#include "utils/time.hh"


#include <string>
using std::string;

using namespace SimpleSSD::ISC;
struct TaskConfig {
  nvme_config_t nvme;
  char *mount_point;
  char *path, *pattern;
  uint32_t slet_id;
  bool init_runtime;
} cfg;
#define setSletOpt(cfg, k, v, vsz)                                             \
  {                                                                            \
    if (setOpt(cfg.slet_id, cfg.nvme, k, v, vsz))                              \
      exit(-__LINE__);                                                         \
    pr("SetOpt done");                                                         \
  }

struct Time_t {
  uint64_t tckBeg, tckEnd, tckDiff;
  struct timespec tsBeg, tsEnd;

  uint64_t tckCompBeg, tckCompEnd, tckComp;
  struct timespec tsCompBeg, tsCompEnd;
  uint64_t tckSleep;
  struct timespec tsSleepBeg, tsSleepEnd;

  static inline double ns2sec(uint64_t tcks) { return tcks / 1.0e9; }
  static inline double ps2sec(uint64_t tcks) { return tcks / 1.0e12; }

  static double diffTs(struct timespec &s, struct timespec &e) {
    return (e.tv_sec - s.tv_sec) + ns2sec(e.tv_nsec - s.tv_nsec);
  }
} sim;
uint64_t tickStart, tickEnd, tickDiff;
struct timespec nowStart, nowEnd;

struct result_t {
  size_t len;
  char line[];
};

static FlagOpt_t flags[] = {
    {"dry", &cfg.nvme.dry, "dry run (do not issue command)"},
    {"init", &cfg.init_runtime, "init runtime first"},
};
static KVOpt_t keys[] = {
    {"dev", &cfg.nvme.devfd, val2fd, 1, 0, "path to nvme device"},
    {"ns", &cfg.nvme.nsid, val2u32, 0, 0, "namespace id"},
    {"path", &cfg.path, val2str, 1, 0, "target path"},
    {"pattern", &cfg.pattern, val2str, 1, 0, "target pattern"},
    {"mountpoint", &cfg.mount_point, val2str, 1, 0, "filesystem mount point"},
    {"id", &cfg.slet_id, val2u32, 1, 0, "slet id"},
};
static size_t numFlags = sizeof(flags) / sizeof(flags[0]);
static size_t numKeys = sizeof(keys) / sizeof(keys[0]);

int main(int argc, const char *argv[]) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.nvme.nsid = 1;
  setArgs(argc, argv, keys, numKeys, flags, numFlags);

#ifndef NO_M5
  if (cfg.init_runtime) {
    if (initRuntime(cfg.nvme))
      exit(-__LINE__);
    pr("Runtime Init done");

  }
#endif

  // start main tasks
  map_m5_mem();
  sim.tckBeg = m5_get_tick(&sim.tsBeg);

  sim.tckCompBeg = m5_get_tick(&sim.tsCompBeg);
  assert(0 == clock_gettime(CLOCK_THREAD_CPUTIME_ID, &sim.tsSleepBeg));

  // prepare file extents
  string localpath = string(cfg.mount_point) + cfg.path;

  size_t szExts;
  size_t *nfiles = (size_t *)malloc(sizeof(size_t));
  extent_t *exts;
  size_t *sizes;
  assert(0 == getExtents(localpath.c_str(), nfiles, &exts, &sizes, &szExts));

  assert(0 == clock_gettime(CLOCK_THREAD_CPUTIME_ID, &sim.tsSleepEnd));
  sim.tckCompEnd = m5_get_tick(&sim.tsCompEnd);
  sim.tckComp += sim.tckCompEnd - sim.tckCompBeg;

#ifdef ISC_DEBUG
  pr("There are %lu files (szExt = %lu)", *nfiles, szExts);
  for (size_t iFile = 0, iExt = 0; iFile < *nfiles; ++iFile, ++iExt) {
    pr("File [%lu] (%lu bytes)", iFile, sizes[iFile]);
    for (int idx = 0; exts[iExt].pba != UINT64_MAX; ++iExt, ++idx) {
      const auto e = exts[iExt];
      pr("(%d) %lu -> %lu (+%lu)", idx, e.lba, e.pba, e.len);
    }
  }
#endif

  // set opt
  setSletOpt(cfg, ISC_KEY_PATH, cfg.path, strlen(cfg.path));
  setSletOpt(cfg, "pattern", cfg.pattern, strlen(cfg.pattern));
  setSletOpt(cfg, ISC_KEY_NUM_FILES, nfiles, sizeof(size_t));
  setSletOpt(cfg, ISC_KEY_EXTS, exts, szExts);
  setSletOpt(cfg, ISC_KEY_FILE_SIZES, sizes, *nfiles * sizeof(size_t));

#ifdef NO_M5
  // record end time
  sim.tckEnd = m5_get_tick(&sim.tsEnd);
  sim.tckDiff = sim.tckEnd - sim.tckBeg;

  free(nfiles);
  free(exts);
  free(sizes);
#else
  // trigger the slet
  if (startSlet(cfg.slet_id, cfg.nvme))
    exit(-__LINE__);
  pr("Start Slet done");

  // get slet result
  size_t resSize;
  if (getResultSize(cfg.slet_id, cfg.nvme, &resSize))
    exit(-__LINE__);
  pr("Get ResultSize done (%lu)", resSize);

  auto res = (char *)calloc(1, resSize + 1);
  if (getResult(cfg.slet_id, cfg.nvme, res, resSize))
    exit(-__LINE__);
  pr("Get Result done");

  // record end time
  sim.tckEnd = m5_get_tick(&sim.tsEnd);
  sim.tckDiff = sim.tckEnd - sim.tckBeg;

  // print result and exec time
  for (size_t ofs = 0, i = 0; ofs < resSize; ++i) {
    auto r = (result_t *)(res + ofs);
    ofs += sizeof(size_t) + ALIGN_UP(r->len, sizeof(size_t));

    pr("Res[%lu]: (%lu) '%s'", i, r->len, r->line);
  }
#endif

  sim.tckSleep =
      sim.tckComp - 1e3 * (ts2ns(&sim.tsSleepEnd) - ts2ns(&sim.tsSleepBeg));

  pr("Simulation Time: %lu~%lu (%lu) ps", sim.tckBeg, sim.tckEnd, sim.tckDiff);
  pr("= %0.9lf s", Time_t::diffTs(sim.tsBeg, sim.tsEnd));
  pr("Compute Time: %lu ps (%0.9lf s)", sim.tckComp,
     Time_t::ps2sec(sim.tckComp));
  pr("Sleep Time: %lu ps (%0.9lf s)", sim.tckSleep,
     Time_t::ps2sec(sim.tckSleep));
  return 0;
}
