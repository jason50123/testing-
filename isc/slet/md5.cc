#include <cstdint>
#include <cstdlib>

#include <typeinfo>

#include "sims/configs.hh"
#include "sims/ftl.hh"

#include "fs/ext4/ext4.hh"
#include "runtime.hh"
#include "slet/md5.hh"
#include "utils/debug.hh"

namespace SimpleSSD {
namespace ISC {

using namespace SIM;

#define PR_SECTION LOG_ISC_SLET_MD5

#define ISC_APP_CLASS MD5APP

const char *ISC_APP_CLASS::keyNumFiles = "numfiles";
const char *ISC_APP_CLASS::keyFileSizes = "filesizes";
const char *ISC_APP_CLASS::keyExts = "exts";

const char *ISC_APP_CLASS::keyPath = "path";
const char *ISC_APP_CLASS::keyResult = ISC_KEY_RESULT;
const char *ISC_APP_CLASS::keyResultSize = ISC_KEY_RESULT_SIZE;

template ISC_STS Runtime::addSlet<MD5APP>(_SIM_PARAMS);

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
static void MD5Update(MD5_CTX *, uint8_t *, uint32_t _ADD_SIM_PARAMS);
static void __attribute__((noinline))
Encode(uint8_t *, uint32_t *, uint32_t _ADD_SIM_PARAMS);

#define MD5_TASK_SUM CPU::ISC__TASK1
#define MD5_TASK_TRANSFORM CPU::ISC__TASK2
#define MD5_TASK_UPDATE CPU::ISC__TASK3
#define MD5_TASK_ENCODE CPU::ISC__TASK4

/* -------------------------------------------------------------------------- */
/*                            major function impls                            */
/* -------------------------------------------------------------------------- */

ISC_APP_CLASS::ISC_APP_CLASS(_SIM_PARAMS) : GenericAPP() {
  this->opt.name = strdup(typeid(*this).name());
}

void ISC_APP_CLASS::md5sum(uint8_t *in, uint32_t inSz,
                           uint32_t out[4] _ADD_SIM_PARAMS) {
  MD5_CTX md5Ctx;

  // MD5Init
  md5Ctx.count[0] = md5Ctx.count[1] = 0;
  md5Ctx.state[0] = 0x67452301;
  md5Ctx.state[1] = 0xefcdab89;
  md5Ctx.state[2] = 0x98badcfe;
  md5Ctx.state[3] = 0x10325476;

  MD5Update(&md5Ctx, in, inSz _add_sim_params);

  // MD5Final
  uint8_t bits[8];
  Encode(bits, md5Ctx.count, 8 _add_sim_params);

  uint32_t index = (uint32_t)((md5Ctx.count[0] >> 3) & 0x3f);
  uint32_t padLen = (index < 56) ? (56 - index) : (120 - index);
  MD5Update(&md5Ctx, PADDING, padLen _add_sim_params);
  MD5Update(&md5Ctx, bits, 8 _add_sim_params);
  Encode((uint8_t *)out, md5Ctx.state, 16 _add_sim_params);

  simApplyLatency(CPU::ISC__SLET__MD5, MD5_TASK_SUM);
}

ISC_STS ISC_APP_CLASS::builtin_startup(_SIM_PARAMS) {
  auto doit = [this](_SIM_PARAMS) -> ISC_STS {
    auto sts = ISC_STS_OK;

    // get options
    GenericFSA::ExtList *fileExtLists = nullptr;
    auto exts = (GenericFSA::Ext *)this->getOpt(keyExts);
    auto numFiles = (size_t *)this->getOpt(keyNumFiles);
    auto fileSizes = (size_t *)this->getOpt(keyFileSizes);
    auto nofsa = exts && numFiles && fileSizes;

    auto path = (const char *)this->getOpt(keyPath);
    auto isdir = path[strlen(path) - 1] == '/';

    // allocate result size buffer
    auto bufOutSz = (size_t *)calloc(1, sizeof(size_t));
    if (!bufOutSz)
      return ISC_STS_FAIL;

    size_t iFile = 0, oBufDir = 0, szBufDir = 0;
    char pathFile[512] = {0};

    // determine output buffer size
    uint32_t *bufOut;
    char *bufDir = nullptr;
    Ext4::fake_dirent *dirent;

    if (isdir && !nofsa) {
      auto dirExtList = Runtime::getExts(path _add_sim_params);
      for (size_t ie = 0; ie < dirExtList.len; ++ie)
        szBufDir += dirExtList.exts[ie].len * BLK_SIZE;

      // read dir entries
      bufDir = (char *)calloc(1, szBufDir);
      if (!bufDir) {
        free(dirExtList.exts);
        sts = ISC_STS_FAIL;
        goto out_free_ressize;
      }
      for (size_t ie = 0, ofsBuf = 0; ie < dirExtList.len; ++ie) {
        auto ofsData = dirExtList.exts[ie].slbn * BLK_SIZE;
        auto szData = dirExtList.exts[ie].len * BLK_SIZE;
        SIM::FTL::read(&bufDir[ofsBuf], ofsData, szData _add_sim_params);
        ofsBuf += szData;
      }

      // check number of files
      for (oBufDir = 0; oBufDir < szBufDir; oBufDir += dirent->rec_len) {
        dirent = (Ext4::fake_dirent *)&bufDir[oBufDir];

        if (dirent->inode == 0) {
          // skip 1 dummy tail dent (checksum)
          if (dirent->file_type == 0xde) {
            pr("End of directory block (+%d)", dirent->rec_len);
            continue;
          }
          break;
        }

        if (dirent->file_type != 2 /* dir */) {
          char name[256] = {0};
          strncpy(name, &bufDir[oBufDir + sizeof(*dirent)], dirent->name_len);

          *bufOutSz += BYTES_PER_RESULT;
          pr("[+%lu]: '%s'", oBufDir, name);
        }
      }
      free(dirExtList.exts);
    }
    else if (!nofsa) {
      strncpy(pathFile, path, 255);
      *bufOutSz = BYTES_PER_RESULT;
    }
    else {
      // convert exts to ExtList
      fileExtLists =
          (GenericFSA::ExtList *)calloc(*numFiles, sizeof(GenericFSA::ExtList));

      for (size_t i = 0, ie = 0; i < *numFiles; ++i, ++ie) {
        fileExtLists[i].bytes = fileSizes[i];

        // exts is separated by 'block == UINT64_MAX'
        fileExtLists[i].exts = &exts[ie];
        // exts is separated by 'block == UINT64_MAX'
        fileExtLists[i].exts = &exts[ie];
        fileExtLists[i].len = 0;
        for (; exts[ie].block != UINT64_MAX; ++ie)
          fileExtLists[i].len++;
      }
      *bufOutSz = *numFiles * BYTES_PER_RESULT;
      if (!isdir)
        strncpy(pathFile, path, 255);
    }

    // allocate output buffer and start hashing
    bufOut = (uint32_t *)calloc(*bufOutSz + 1, sizeof(uint8_t));
    if (!bufOut) {
      sts = ISC_STS_FAIL;
      goto out_free_ressize;
    }
    if (!isdir)
      goto readfile;

    pr("Num files: %lu %s", *bufOutSz / BYTES_PER_RESULT, isdir ? "(dir)" : "");
    for (oBufDir = 0; iFile < (*bufOutSz / 16); ++iFile) {
      // find next file
      if (nofsa) {
        // we don't know the exact filename, give it a symbolic name
        snprintf(pathFile, 255, "%s[%lu]", path, iFile);
        goto readfile;
      }

      for (char name[256]; oBufDir < szBufDir; oBufDir += dirent->rec_len) {
        dirent = (Ext4::fake_dirent *)&bufDir[oBufDir];

        if (dirent->file_type == 2 /* dir */)
          continue;

        memset(name, 0, 256);
        strncpy(name, &bufDir[oBufDir + sizeof(*dirent)], dirent->name_len);

        pr("[+%lu]: Find file %s", oBufDir, name);
        snprintf(pathFile, 511, "%s%s", path, name);
        oBufDir += dirent->rec_len;
        break;
      }

      // read data of the path
    readfile:
      pr("File[%lu]: %s", iFile, pathFile);

      auto szBufFile = 0;
      GenericFSA::ExtList fileExtList;
      if (nofsa)
        fileExtList = fileExtLists[iFile];
      else
        fileExtList = Runtime::getExts(pathFile _add_sim_params);

      for (size_t ie = 0; ie < fileExtList.len; ++ie)
        szBufFile += fileExtList.exts[ie].len * BLK_SIZE;

      // read file data
      auto bufFile = (uint8_t *)calloc(1, szBufFile);
      if (!bufFile) {
        sts = ISC_STS_FAIL;
        goto out_free_fileExt;
      }
      for (size_t ie = 0, ofsBuf = 0; ie < fileExtList.len; ++ie) {
        auto ofsData = fileExtList.exts[ie].slbn * BLK_SIZE;
        auto szData = fileExtList.exts[ie].len * BLK_SIZE;
        SIM::FTL::read(&bufFile[ofsBuf], ofsData, szData _add_sim_params);
        ofsBuf += szData;
      }

      // do md5 on this file
      md5sum(bufFile, fileExtList.bytes, &bufOut[iFile * 4] _add_sim_params);

      free(bufFile);
    out_free_fileExt:
      if (!nofsa)
        free(fileExtList.exts);

      if (sts != ISC_STS_OK)
        goto out_free_result;
    }

    // set result
    sts = this->setOpt(keyResultSize, bufOutSz);
    if (sts == ISC_STS_OK)
      sts = this->setOpt(keyResult, bufOut);

  out_free_result:
    if (sts != ISC_STS_OK)
      free(bufOut);
    if (isdir && !nofsa)
      free(bufDir);
    if (nofsa)
      free(fileExtLists);
  out_free_ressize:
    if (sts != ISC_STS_OK)
      free(bufOutSz);
    return sts;
  };

  auto res = doit(_sim_params);
  simApplyLatency(CPU::ISC__SLET__MD5, CPU::ISC__START_SLET);
  return res;
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
static void MD5Update(MD5_CTX *context, uint8_t *in,
                      uint32_t inSz _ADD_SIM_PARAMS) {
  uint32_t i, j, index, partLen;

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

    for (i = partLen, j = 1; i + 63 < inSz; i += 64, j++)
      MD5Transform(context->state, &in[i]);
    simApplyManyLatency(CPU::ISC__SLET__MD5, MD5_TASK_TRANSFORM, j);

    index = 0;
  }
  else
    i = 0;

  /* Buffer remaining input */
  memcpy((uint8_t *)&context->buffer[index], (uint8_t *)&in[i], inSz - i);

  simApplyLatency(CPU::ISC__SLET__MD5, MD5_TASK_UPDATE);
}

static void Encode(uint8_t *out, uint32_t *in, uint32_t len _ADD_SIM_PARAMS) {
  uint32_t i, j;

  for (i = 0, j = 0; j < len; i++, j += 4) {
    out[j] = (uint8_t)(in[i] & 0xff);
    out[j + 1] = (uint8_t)((in[i] >> 8) & 0xff);
    out[j + 2] = (uint8_t)((in[i] >> 16) & 0xff);
    out[j + 3] = (uint8_t)((in[i] >> 24) & 0xff);
  }

  simApplyLatency(CPU::ISC__SLET__MD5, MD5_TASK_ENCODE);
}

}  // namespace ISC
}  // namespace SimpleSSD