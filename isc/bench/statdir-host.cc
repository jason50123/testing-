#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>
using namespace std;

#include "utils/argparse.hh"
#include "utils/time.hh"

struct TaskConfig {
  const char *dirPath;
  uint32_t slet_id;
  bool init_runtime;
} config;

static FlagOpt_t flags[] = {};
static KVOpt_t keys[] = {
    {"dir", &config.dirPath, val2str, 1, 0, "target directory path"},
};
static size_t numFlags = sizeof(flags) / sizeof(flags[0]);
static size_t numKeys = sizeof(keys) / sizeof(keys[0]);

struct result_t {
  ssize_t st_mtim, st_size;
  mode_t st_mode;
  std::string d_name;
};

void statdir(const char *, uint64_t &, vector<result_t> &);
int main(int argc, const char *argv[]) {
  memset(&config, 0, sizeof(config));
  setArgs(argc, argv, keys, numKeys, flags, numFlags);

  vector<result_t> result;

  uint64_t tickStart, tickEnd, tickDiff, tickComp = 0;
  struct timespec nowStart, nowEnd;
  map_m5_mem();
  tickStart = m5_get_tick(&nowStart);

  statdir(config.dirPath, tickComp, result);

  tickEnd = m5_get_tick(&nowEnd);
  tickDiff = tickEnd - tickStart;

  // print result after stop timer
  printf("%-15s|%-10s|%-10s|%s\n", "Mod Time", "Bytes", "Perm", "File");
  for (auto &res : result) {
    printf("%-15ld|%-10ld|%-10o|%-s\n", res.st_mtim, res.st_size, res.st_mode,
           res.d_name.c_str());
  }

  printf("Simulation Time: %lu~%lu (%lu) ps\n", tickStart, tickEnd, tickDiff);
  printf("= %lu nsecs \n", ts2ns(&nowEnd) - ts2ns(&nowStart));
  printf("Compute Time: %lu ps\n", tickComp);

  return 0;
}

/* -------------------------------------------------------------------------- */
/*                               implementations                              */
/* -------------------------------------------------------------------------- */

void statdir(const char *path, uint64_t &tickComp, vector<result_t> &result) {
  DIR *dir;
  struct dirent *entry;
  struct stat finfo;

  uint64_t tickStart, tickEnd;
  struct timespec nowStart, nowEnd;

  if ((dir = opendir(path)) == NULL) {
    perror("opendir");
    return;
  }

  tickComp = 0;
  while ((entry = readdir(dir)) != NULL) {
    if (fstatat(dirfd(dir), entry->d_name, &finfo, 0) == -1) {
      printf("try: fstatat('%s') (ino=%lu)\n", entry->d_name, entry->d_ino);
      perror("fstatat");
      exit(-1);
    }

    tickStart = m5_get_tick(&nowStart);
    result.push_back(
        {finfo.st_mtime, finfo.st_size, finfo.st_mode, entry->d_name});
    tickEnd = m5_get_tick(&nowEnd);
    tickComp += tickEnd - tickStart;
  }

  closedir(dir);
}
