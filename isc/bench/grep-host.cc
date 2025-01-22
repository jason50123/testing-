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
  const char *pattern;
  char *in;
  uint32_t slet_id;
  bool init_runtime;
} config;

struct result_t {
  char *line;
  size_t len;
  result_t() : line(nullptr), len(0) {}
};

struct Time_t {
  uint64_t tckBeg, tckEnd, tckDiff;
  struct timespec tsBeg, tsEnd;

  uint64_t tckCompBeg, tckCompEnd, tckComp;
  struct timespec tsCompBeg, tsCompEnd;

  static inline double ns2sec(uint64_t tcks) { return tcks / 1.0e9; }
  static inline double ps2sec(uint64_t tcks) { return tcks / 1.0e12; }

  static double diffTs(struct timespec &s, struct timespec &e) {
    return (e.tv_sec - s.tv_sec) + ns2sec(e.tv_nsec - s.tv_nsec);
  }
} sim;

static FlagOpt_t flags[] = {};
static KVOpt_t keys[] = {
    {"path", &config.path, val2str, 1, 0, "target path"},
    {"pattern", &config.pattern, val2str, 1, 0, "target pattern"},
};
static size_t numFlags = sizeof(flags) / sizeof(flags[0]);
static size_t numKeys = sizeof(keys) / sizeof(keys[0]);

int strstr(const char *s, size_t slen, const char *t, size_t tlen);
int grep(const char *src, size_t slen, const char *pat, void *result);

int main(int argc, const char *argv[]) {
  memset(&config, 0, sizeof(config));
  setArgs(argc, argv, keys, numKeys, flags, numFlags);
  bool isdir = config.path[strlen(config.path) - 1] == '/';

  // record start time
  int err = 0, fd;
  map_m5_mem();
  sim.tckBeg = m5_get_tick(&sim.tsBeg);

  vector<result_t> results;
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
    result_t result;
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
    grep(config.in, st.st_size, config.pattern, &result);
    sim.tckCompEnd = m5_get_tick(&sim.tsCompEnd);

    sim.tckComp += sim.tckCompEnd - sim.tckCompBeg;

    results.push_back(result);

    if (err)
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
  for (size_t iFile = 0; iFile < results.size(); ++iFile) {
    auto result = results.at(iFile);
    pr("result line (%lu): '%s'", result.len, result.line);
  }
  for (auto &result : results)
    free(result.line);
  results.clear();

  pr("Simulation Time: %lu~%lu (%lu) ps", sim.tckBeg, sim.tckEnd, sim.tckDiff);
  pr("= %0.9lf s", Time_t::diffTs(sim.tsBeg, sim.tsEnd));
  pr("Compute Time: %lu ps (%0.9lf s)", sim.tckComp,
     Time_t::ps2sec(sim.tckComp));

  return err;
}

/* -------------------------------------------------------------------------- */
/*                           workload implementation                          */
/* -------------------------------------------------------------------------- */

char badChar[256] = {};

// LeetCode 28
int strstr(const uint8_t *s, size_t slen, const char *t, size_t tlen) {
  size_t shift = 0;
  while (shift <= (slen - tlen)) {
    int notmatch = tlen - 1;

    while (notmatch >= 0 && t[notmatch] == s[shift + notmatch])
      notmatch--;

    if (notmatch < 0)
      return shift;
    shift += std::max(1, notmatch - badChar[s[shift + notmatch]]);
  }
  return -1;
}

int grep(const char *src, size_t slen, const char *pat, void *result) {
  auto res = (result_t *)result;
  auto tlen = strlen(pat);

  if (!src || (slen < tlen)) {
    pr("ERROR! The source string is null or shorter than the pattern");
    res->line = nullptr;
    return -1;
  }

  memset(badChar, 0xff, 256);
  for (size_t i = 0; i < tlen; ++i)
    badChar[(uint8_t)pat[i]] = i;

  // get the first match pattern offset
  auto ofs = strstr((uint8_t *)src, slen, pat, strlen(pat));

  const char *line;
  for (line = &src[ofs], res->len = tlen; line > src && *(line - 1) != '\n';
       ++res->len, --line)
    ;  // find \n of prev line

  for (auto ch = &src[ofs + tlen]; *ch != '\n' && ch < &src[slen];
       ++ch, ++res->len)
    ;  // find \n of this line

  res->line = (char *)calloc(res->len + 1, 1);
  if (!res->line) {
    pr("calloc failed");
    return -1;
  }
  strncpy(res->line, line, res->len);
  return 0;
}