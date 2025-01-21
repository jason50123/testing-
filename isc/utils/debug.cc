#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/cpptrace.hpp"

#include "utils/debug.hh"

namespace SimpleSSD {
namespace Utils {

#define PR_SECTION LOG_ISC_UTIL

#ifndef ISC_TEST_NO_XXD
void do_pipe2xxd(const char *func, size_t line, const char *desc, void *data,
                 size_t len, const char *) {
  // connect parent stdout to child
  pr("%s (From %s:%lu)", desc, func, line);

  auto dumpLine = [](const void *src, size_t len, size_t &ofs, bool &skip) {
    union data_t {
      uint8_t bin[20];
      char str[20];
    } data = {.bin = {}};
    static_assert(sizeof(data_t) == 20, "data_t should be 16B + null term");

    // skip if all zero
    if (!memcmp(data.bin, src, 16)) {
      if (!skip)
        printf("%08lx: (all zero, skipped) ...\n", ofs);
      skip = true;
    }
    else {
      skip = false;
      memcpy(data.bin, src, len);

      printf("%08lx: ", ofs);
      for (size_t i = 0; i < 16; ++i) {
        printf("%02x ", data.bin[i]);
        if (!isprint(data.bin[i]))
          data.str[i] = '.';
      }
      printf("| %16s\n", data.str);
    }
    ofs += len;
  };

  bool skip = false;
  std::size_t ofs = (uintptr_t)data & 0xf;
  for (auto p = (uintptr_t)data, e = p + len; p < e; p += 16)
    dumpLine((void *)p, std::min(16UL, e - p), ofs, skip);
  pr("xxd done, total %lu bytes from %p\n", len, data);
}
#else
void do_pipe2xxd(const char *, size_t, const char *, void *, size_t,
                 const char *) {}
#endif

void do_pipe2md5(const void *data, size_t len, char out[32]) {
  const int READ_END = 0, WRITE_END = 1;

  // connect parent stdout to child, child stdin to parent
  int ret, sts, fdInput[2], fdOutput[2];
  assert(!pipe2(fdInput, 0));
  assert(!pipe2(fdOutput, 0));

  assert((ret = fork()) >= 0);
  if (!ret) {
    // child (read end) got 0, dump data
    close(fdOutput[READ_END]);
    close(fdInput[WRITE_END]);
    dup2(fdInput[READ_END], STDIN_FILENO);
    dup2(fdOutput[WRITE_END], STDOUT_FILENO);
    close(fdInput[READ_END]);
    close(fdOutput[WRITE_END]);

    execl("/usr/bin/md5sum", "md5sum", NULL);
    assert(0);
  }

  // parent get pid, write input to child, read sum from child
  close(fdInput[READ_END]);
  close(fdOutput[WRITE_END]);
  ret = write(fdInput[WRITE_END], data, len);
  assert((size_t)ret == len);

  // read result from child
  close(fdInput[WRITE_END]);
  assert(32 == read(fdOutput[READ_END], out, 32));
  close(fdOutput[READ_END]);

  waitpid(ret, &sts, 0);
}

#undef PR_SECTION
#define PR_SECTION LOG_ISC_UTIL
void bt(int count, int skip, bool noBuiltin) {
  auto frames = cpptrace::generate_trace().frames;
  for (size_t i = 1 + skip; i < frames.size() && count--; ++i) {
    const auto &f = frames[i];

    const std::string pfx = "/usr";
    if (noBuiltin && !f.filename.compare(0, pfx.length(), pfx))
      continue;

    pr("> %s at %s:%d", f.symbol.c_str(), f.filename.c_str(), f.line.value());
    if (f.symbol == "main")
      break;
  }
  pr("<< END");
}
#undef PR_SECTION

}  // namespace Utils
}  // namespace SimpleSSD