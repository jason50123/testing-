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

struct TaskConfig {
  const char *path;
  uint8_t *in;
  uint32_t slet_id;
  bool init_runtime;
} config;

static FlagOpt_t flags[] = {};
static KVOpt_t keys[] = {
    {"path", &config.path, val2str, 1, 0, "target path"},
};
static size_t numFlags = sizeof(flags) / sizeof(flags[0]);
static size_t numKeys = sizeof(keys) / sizeof(keys[0]);

void md5sum(uint8_t *in, uint32_t inSz, uint32_t out[4]);

int main(int argc, const char *argv[]) {
  memset(&config, 0, sizeof(config));
  setArgs(argc, argv, keys, numKeys, flags, numFlags);
  bool isdir = config.path[strlen(config.path) - 1] == '/';

  // record start time
  int err = 0, fd;
  uint64_t tickStart, tickEnd, tickDiff, tickComp = 0;
  struct timespec nowStart, nowEnd;
  map_m5_mem();
  tickStart = m5_get_tick(&nowStart);

  union md5_t {
    uint8_t bytes[16];
    uint32_t words[4];
    uint64_t dwords[2];
  };
  vector<md5_t> results;
  char pathFile[512] = {0};

  // iterate dir
  DIR *dir = nullptr;
  if (!isdir) {
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
    md5_t result;
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

    config.in = (uint8_t *)calloc(1, st.st_size);
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

    {
      uint64_t tickCompStart, tickCompEnd;
      struct timespec compStart, compEnd;

      tickCompStart = m5_get_tick(&compStart);
      md5sum(config.in, st.st_size, result.words);
      results.push_back(result);
      tickCompEnd = m5_get_tick(&compEnd);
      tickComp += tickCompEnd - tickCompStart;
    }

    free(config.in);
  close_file:
    close(fd);
  }
  if (dir)
    closedir(dir);

out:
  // record end time
  tickEnd = m5_get_tick(&nowEnd);
  tickDiff = tickEnd - tickStart;

  // print result and exec time
  for (size_t iFile = 0; iFile < results.size(); ++iFile) {
    auto md5 = results.at(iFile);

    // swap endian
    for (uint8_t i = 0, t; i < 8; ++i) {
      t = md5.bytes[i];
      md5.bytes[i] = md5.bytes[15 - i];
      md5.bytes[15 - i] = t;
    }
    printf("MD5 result: %016lx%016lx\n", md5.dwords[1], md5.dwords[0]);
  }
  results.clear();

  printf("Simulation Time: %lu~%lu (%lu) ps\n", tickStart, tickEnd, tickDiff);
  printf("= %lu nsecs \n", nowEnd.tv_nsec - nowStart.tv_nsec);
  printf("Compute Time: %lu ps\n", tickComp);

  return err;
}

/* -------------------------------------------------------------------------- */
/*                             md5 implementations                            */
/* -------------------------------------------------------------------------- */

typedef struct {
  uint32_t state[4];  /* state (ABCD) */
  uint32_t count[2];  /* number of bits, modulo 2^64 (lsb first) */
  uint8_t buffer[64]; /* input buffer */
} MD5_CTX;

static uint8_t PADDING[64] = {
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static constexpr uint32_t K[] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

#define XX(X, a, b, c, d, x, s, ac)                                            \
  {                                                                            \
    (a) += X((b), (c), (d)) + (x) + (uint32_t)(ac);                            \
    (a) = ROTATE_LEFT((a), (s));                                               \
    (a) += (b);                                                                \
  }
#define FF(...) XX(F, __VA_ARGS__)
#define GG(...) XX(G, __VA_ARGS__)
#define HH(...) XX(H, __VA_ARGS__)
#define II(...) XX(I, __VA_ARGS__)

// clang-format won't break after attribute, use this to make do that
#if defined(__clang__)
#define NO_SANS(...) __attribute__((no_sanitize(__VA_ARGS__)))
#if __clang_major__ > 12
#define NO_UINT_SANS NO_SANS("unsigned-shift-base", "unsigned-integer-overflow")
#else
#define NO_UINT_SANS NO_SANS("unsigned-integer-overflow")
#endif
#else
#define NO_SANS(...)
#define NO_UINT_SANS
#endif

static void MD5Transform(uint32_t[4], uint8_t[64]);
static void MD5Update(MD5_CTX *, uint8_t *, uint32_t);
static void __attribute__((noinline)) Encode(uint8_t *, uint32_t *, uint32_t);

#define MD5_TASK_SUM CPU::ISC__TASK1
#define MD5_TASK_TRANSFORM CPU::ISC__TASK2
#define MD5_TASK_UPDATE CPU::ISC__TASK3
#define MD5_TASK_ENCODE CPU::ISC__TASK4

/* -------------------------------------------------------------------------- */
/*                            major function impls                            */
/* -------------------------------------------------------------------------- */

void md5sum(uint8_t *in, uint32_t inSz, uint32_t out[4]) {
  MD5_CTX md5Ctx;

  // MD5Init
  md5Ctx.count[0] = md5Ctx.count[1] = 0;
  md5Ctx.state[0] = 0x67452301;
  md5Ctx.state[1] = 0xefcdab89;
  md5Ctx.state[2] = 0x98badcfe;
  md5Ctx.state[3] = 0x10325476;

  MD5Update(&md5Ctx, in, inSz);

  // MD5Final
  uint8_t bits[8];
  Encode(bits, md5Ctx.count, 8);

  uint32_t index = (uint32_t)((md5Ctx.count[0] >> 3) & 0x3f);
  uint32_t padLen = (index < 56) ? (56 - index) : (120 - index);
  MD5Update(&md5Ctx, PADDING, padLen);
  MD5Update(&md5Ctx, bits, 8);
  Encode((uint8_t *)out, md5Ctx.state, 16);
}

/* -------------------------------------------------------------------------- */
/*                             internal functions                             */
/* -------------------------------------------------------------------------- */

NO_UINT_SANS
static void MD5Transform(uint32_t state[4], uint8_t block[64]) {
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];

  for (int i = 0, j = 0; j < 64; i++, j += 4) {
    *((uint8_t *)(&x[i]) + 0) = (uint32_t)block[j + 0];
    *((uint8_t *)(&x[i]) + 1) = (uint32_t)block[j + 1];
    *((uint8_t *)(&x[i]) + 2) = (uint32_t)block[j + 2];
    *((uint8_t *)(&x[i]) + 3) = (uint32_t)block[j + 3];
  }

  uint8_t step = 0;

  /* Round 1 */
  FF(a, b, c, d, x[0], 7, K[step++]);   /* 1 */
  FF(d, a, b, c, x[1], 12, K[step++]);  /* 2 */
  FF(c, d, a, b, x[2], 17, K[step++]);  /* 3 */
  FF(b, c, d, a, x[3], 22, K[step++]);  /* 4 */
  FF(a, b, c, d, x[4], 7, K[step++]);   /* 5 */
  FF(d, a, b, c, x[5], 12, K[step++]);  /* 6 */
  FF(c, d, a, b, x[6], 17, K[step++]);  /* 7 */
  FF(b, c, d, a, x[7], 22, K[step++]);  /* 8 */
  FF(a, b, c, d, x[8], 7, K[step++]);   /* 9 */
  FF(d, a, b, c, x[9], 12, K[step++]);  /* 10 */
  FF(c, d, a, b, x[10], 17, K[step++]); /* 11 */
  FF(b, c, d, a, x[11], 22, K[step++]); /* 12 */
  FF(a, b, c, d, x[12], 7, K[step++]);  /* 13 */
  FF(d, a, b, c, x[13], 12, K[step++]); /* 14 */
  FF(c, d, a, b, x[14], 17, K[step++]); /* 15 */
  FF(b, c, d, a, x[15], 22, K[step++]); /* 16 */

  /* Round 2 */
  GG(a, b, c, d, x[1], 5, K[step++]);   /* 17 */
  GG(d, a, b, c, x[6], 9, K[step++]);   /* 18 */
  GG(c, d, a, b, x[11], 14, K[step++]); /* 19 */
  GG(b, c, d, a, x[0], 20, K[step++]);  /* 20 */
  GG(a, b, c, d, x[5], 5, K[step++]);   /* 21 */
  GG(d, a, b, c, x[10], 9, K[step++]);  /* 22 */
  GG(c, d, a, b, x[15], 14, K[step++]); /* 23 */
  GG(b, c, d, a, x[4], 20, K[step++]);  /* 24 */
  GG(a, b, c, d, x[9], 5, K[step++]);   /* 25 */
  GG(d, a, b, c, x[14], 9, K[step++]);  /* 26 */
  GG(c, d, a, b, x[3], 14, K[step++]);  /* 27 */
  GG(b, c, d, a, x[8], 20, K[step++]);  /* 28 */
  GG(a, b, c, d, x[13], 5, K[step++]);  /* 29 */
  GG(d, a, b, c, x[2], 9, K[step++]);   /* 30 */
  GG(c, d, a, b, x[7], 14, K[step++]);  /* 31 */
  GG(b, c, d, a, x[12], 20, K[step++]); /* 32 */

  /* Round 3 */
  HH(a, b, c, d, x[5], 4, K[step++]);   /* 33 */
  HH(d, a, b, c, x[8], 11, K[step++]);  /* 34 */
  HH(c, d, a, b, x[11], 16, K[step++]); /* 35 */
  HH(b, c, d, a, x[14], 23, K[step++]); /* 36 */
  HH(a, b, c, d, x[1], 4, K[step++]);   /* 37 */
  HH(d, a, b, c, x[4], 11, K[step++]);  /* 38 */
  HH(c, d, a, b, x[7], 16, K[step++]);  /* 39 */
  HH(b, c, d, a, x[10], 23, K[step++]); /* 40 */
  HH(a, b, c, d, x[13], 4, K[step++]);  /* 41 */
  HH(d, a, b, c, x[0], 11, K[step++]);  /* 42 */
  HH(c, d, a, b, x[3], 16, K[step++]);  /* 43 */
  HH(b, c, d, a, x[6], 23, K[step++]);  /* 44 */
  HH(a, b, c, d, x[9], 4, K[step++]);   /* 45 */
  HH(d, a, b, c, x[12], 11, K[step++]); /* 46 */
  HH(c, d, a, b, x[15], 16, K[step++]); /* 47 */
  HH(b, c, d, a, x[2], 23, K[step++]);  /* 48 */

  /* Round 4 */
  II(a, b, c, d, x[0], 6, K[step++]);   /* 49 */
  II(d, a, b, c, x[7], 10, K[step++]);  /* 50 */
  II(c, d, a, b, x[14], 15, K[step++]); /* 51 */
  II(b, c, d, a, x[5], 21, K[step++]);  /* 52 */
  II(a, b, c, d, x[12], 6, K[step++]);  /* 53 */
  II(d, a, b, c, x[3], 10, K[step++]);  /* 54 */
  II(c, d, a, b, x[10], 15, K[step++]); /* 55 */
  II(b, c, d, a, x[1], 21, K[step++]);  /* 56 */
  II(a, b, c, d, x[8], 6, K[step++]);   /* 57 */
  II(d, a, b, c, x[15], 10, K[step++]); /* 58 */
  II(c, d, a, b, x[6], 15, K[step++]);  /* 59 */
  II(b, c, d, a, x[13], 21, K[step++]); /* 60 */
  II(a, b, c, d, x[4], 6, K[step++]);   /* 61 */
  II(d, a, b, c, x[11], 10, K[step++]); /* 62 */
  II(c, d, a, b, x[2], 15, K[step++]);  /* 63 */
  II(b, c, d, a, x[9], 21, K[step++]);  /* 64 */

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;

  /* Zeroize sensitive information.
   */
  memset((uint8_t *)x, 0, sizeof(x));
}

NO_UINT_SANS
static void MD5Update(MD5_CTX *context, uint8_t *in, uint32_t inSz) {
  uint32_t i, index, partLen;

  /* Compute number of bytes mod 64 */
  index = (uint32_t)((context->count[0] >> 3) & 0x3F);

  /* Update number of bits */
  context->count[0] += ((uint32_t)inSz << 3);
  if (context->count[0] < ((uint32_t)inSz << 3))
    context->count[1]++;
  context->count[1] += ((uint32_t)inSz >> 29);
  partLen = 64 - index;

  /* Transform as many times as possible. */
  if (inSz >= partLen) {
    memcpy((uint8_t *)&context->buffer[index], (uint8_t *)in, partLen);
    MD5Transform(context->state, context->buffer);

    for (i = partLen; i + 63 < inSz; i += 64)
      MD5Transform(context->state, &in[i]);
    index = 0;
  }
  else
    i = 0;

  /* Buffer remaining input */
  memcpy((uint8_t *)&context->buffer[index], (uint8_t *)&in[i], inSz - i);
}

static void Encode(uint8_t *out, uint32_t *in, uint32_t len) {
  uint32_t i, j;

  for (i = 0, j = 0; j < len; i++, j += 4) {
    out[j] = (uint8_t)(in[i] & 0xff);
    out[j + 1] = (uint8_t)((in[i] >> 8) & 0xff);
    out[j + 2] = (uint8_t)((in[i] >> 16) & 0xff);
    out[j + 3] = (uint8_t)((in[i] >> 24) & 0xff);
  }
}