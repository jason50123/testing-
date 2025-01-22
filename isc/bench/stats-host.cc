#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <vector>
using std::vector;

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils/argparse.hh"
#include "utils/time.hh"

#define pr(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

struct TaskConfig {
  const char *path;
  char *in;
  bool mode64;
} config;

struct result32_t {
  int64_t sum;
  int32_t min;
  int32_t max;
  result32_t() : sum(0), min(0), max(0) {}
};

struct result64_t {
  uint64_t sum;
  int64_t min;
  int64_t max;
  result64_t() : sum(0), min(0), max(0) {}
};

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

static FlagOpt_t flags[] = {
    {"mode64", &config.mode64, "64bit mode"},
};
static KVOpt_t keys[] = {
    {"path", &config.path, val2str, 1, 0, "target path"},
};
static size_t numFlags = sizeof(flags) / sizeof(flags[0]);
static size_t numKeys = sizeof(keys) / sizeof(keys[0]);

int sum32(const int32_t *src, size_t cnt, result32_t *res) {
  for (size_t i = 0; i < cnt; ++i) {
    res->sum += src[i];
    res->max = std::max(res->max, src[i]);
    res->min = std::min(res->min, src[i]);
  }
  return 0;
}

#if defined(__clang__)
__attribute__((no_sanitize("unsigned-integer-overflow")))
#endif
int sum64(const int64_t *src, size_t cnt, result64_t *res) {
  for (size_t i = 0; i < cnt; ++i) {
    res->sum += src[i];
    res->max = std::max(res->max, src[i]);
    res->min = std::min(res->min, src[i]);
  }
  return 0;
}

int main(int argc, const char *argv[]) {
  memset(&config, 0, sizeof(config));
  setArgs(argc, argv, keys, numKeys, flags, numFlags);
  bool isdir = config.path[strlen(config.path) - 1] == '/';

  // record start time
  int err = 0, fd;
  map_m5_mem();
  sim.tckBeg = m5_get_tick(&sim.tsBeg);

  vector<result32_t> results_32;
  vector<result64_t> results_64;
  char pathFile[512] = {0};

  // iterate dir
  DIR *dir = nullptr;
  if (!isdir) {
    snprintf(pathFile, 511, "%s", config.path);
    goto readfile;
  }
  if ((dir = opendir(config.path)) == NULL) {
    perror("opendir");
    return 0;
  }

  struct dirent *entry;
  struct stat st;
  while (dir && (entry = readdir(dir)) != NULL) {
    // get file name
    snprintf(pathFile, 511, "%s%s", config.path, entry->d_name);

    // get read file data
  readfile:
    result32_t result32;
    result64_t result64;

    fd = open(pathFile, O_RDONLY);
    if (fd <= 0) {
      perror("open fail");
      err = errno;
      goto out;
    }

    // get file size
    fstat(fd, &st);
    if (S_ISDIR(st.st_mode))
      goto close_file;

    config.in = (char *)calloc(1, st.st_size);
    if (!config.in) {
      perror("calloc fail");
      err = errno;
      goto close_file;
    }

    // read file data
    if (0 > read(fd, config.in, st.st_size)) {
      perror("read fail");
      err = errno;
      goto close_file;
    }

    sim.tckCompBeg = m5_get_tick(&sim.tsCompBeg);
    assert(0 == clock_gettime(CLOCK_THREAD_CPUTIME_ID, &sim.tsSleepBeg));
    if (config.mode64)
      sum64((int64_t *)config.in, st.st_size / sizeof(int64_t), &result64);
    else
      sum32((int32_t *)config.in, st.st_size / sizeof(int32_t), &result32);
    assert(0 == clock_gettime(CLOCK_THREAD_CPUTIME_ID, &sim.tsSleepEnd));
    sim.tckCompEnd = m5_get_tick(&sim.tsCompEnd);

    sim.tckComp += sim.tckCompEnd - sim.tckCompBeg;
    sim.tckSleep =
        sim.tckComp - 1e3 * (ts2ns(&sim.tsSleepEnd) - ts2ns(&sim.tsSleepBeg));

    if (config.mode64)
      results_64.push_back(result64);
    else
      results_32.push_back(result32);

    free(config.in);
  close_file:
    close(fd);
  }
  if (dir)
    closedir(dir);

out:
  // record end time
  sim.tckEnd = m5_get_tick(&sim.tsEnd);
  sim.tckDiff = sim.tckEnd - sim.tckBeg;

  // print result and exec time
  int iRes = 0;

  if (config.mode64) {
    for (auto &res : results_64)
      pr("[%d] Sum,Min,Max=%lu,%ld,%ld", iRes++, res.sum, res.min, res.max);
    results_64.clear();
  }
  else {
    for (auto &res : results_32)
      pr("[%d] Sum,Min,Max=%ld,%d,%d", iRes++, res.sum, res.min, res.max);
    results_32.clear();
  }

  pr("Simulation Time: %lu~%lu (%lu) ps", sim.tckBeg, sim.tckEnd, sim.tckDiff);
  pr("= %0.9lf s", Time_t::diffTs(sim.tsBeg, sim.tsEnd));
  pr("Compute Time: %lu ps (%0.9lf s)", sim.tckComp,
     Time_t::ps2sec(sim.tckComp));
  pr("Sleep Time: %lu ps (%0.9lf s)", sim.tckSleep,
     Time_t::ps2sec(sim.tckSleep));

  return err;
}
