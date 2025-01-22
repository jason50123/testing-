#ifndef __SIMPLESSD_ISC_BENCH_COMMON_HH__
#define __SIMPLESSD_ISC_BENCH_COMMON_HH__

#include "runtime.hh"

#include <vector>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <linux/fiemap.h>
#include <linux/fs.h>

#define ALIGN_UP(num, to) ((num + (to - 1)) & ~(to - 1))

#define ISC_KEY_PATH "path"
#define ISC_KEY_NUM_FILES "numfiles"
#define ISC_KEY_FILE_SIZES "filesizes"
#define ISC_KEY_EXTS "exts"

#define BLK_SIZE 4096
struct extent_t {
  size_t lba;  // lba of filesystem
  size_t pba;  // lba of SSD
  size_t len;

  extent_t() : lba(UINT64_MAX), pba(UINT64_MAX), len(UINT64_MAX) {}
  extent_t(struct fiemap_extent e)
      : lba(e.fe_logical / BLK_SIZE),
        pba(e.fe_physical / BLK_SIZE),
        len(e.fe_length / BLK_SIZE) {}
};

static inline int getExtents(const char *path, size_t *numFiles,
                             extent_t **exts, size_t **sizes, size_t *szExts) {
  int err = 0;
  const bool isdir = path[strlen(path) - 1] == '/';

  std::vector<extent_t> fileExts;
  std::vector<size_t> fileSizes;

  DIR *dir = nullptr;
  char pathFile[512] = {0};

  if (!isdir) {
    snprintf(pathFile, 511, "%s", path);
    goto readfile;
  }
  if ((dir = opendir(path)) == NULL) {
    perrno("opendir");
    goto out;
  }

  struct dirent *entry;
  while (dir && (entry = readdir(dir)) != NULL) {
    // get file name
    snprintf(pathFile, 511, "%s%s", path, entry->d_name);

  readfile:  // get read file data
#ifdef ISC_DEBUG
    pr("File[%s]", pathFile);
#endif
    int fd = open(pathFile, O_RDONLY);
    if (fd <= 0) {
      perrno("open fail");
      err = errno;
      goto out;
    }

    // get file size
    struct stat st;
    struct fiemap fm_meta, *fm = nullptr;
    size_t numExts;

    fstat(fd, &st);
    if (S_ISDIR(st.st_mode)) {
      goto close_file;
    }

    fileSizes.push_back(st.st_size);

    // get ext count to determine how many memory needed
    fm_meta = {.fm_start = 0,
               .fm_length = FIEMAP_MAX_OFFSET,
               .fm_flags = FIEMAP_FLAG_SYNC,
               .fm_mapped_extents = 0,
               .fm_extent_count = 0,
               .fm_reserved = 0,
               .fm_extents = {}};

    if (ioctl(fd, FS_IOC_FIEMAP, &fm_meta)) {
      perrno("first time FIEMAP failed");
      err = errno;
      goto close_file;
    }

    numExts = fm_meta.fm_mapped_extents * sizeof(struct fiemap_extent);
    fm = (struct fiemap *)calloc(1, sizeof(fm_meta) + numExts);
    if (!fm) {
      perrno("failed to allocate fm");
      err = errno;
      goto close_file;
    }

    // get extents
    *fm = fm_meta;
    fm->fm_extent_count = fm_meta.fm_mapped_extents;
    if (ioctl(fd, FS_IOC_FIEMAP, fm)) {
      free(fm);
      perrno("second time FIEMAP failed");
      err = errno;
      goto close_file;
    }

    // get child dents
    for (size_t i = 0; i < fm->fm_extent_count; ++i) {
      const auto e = fm->fm_extents[i];
      fileExts.push_back(extent_t(e));
    }
    fileExts.push_back(extent_t());
    free(fm);

  close_file:
    close(fd);
    if (err)
      break;
  }
  if (dir)
    closedir(dir);

out:
  *numFiles = fileSizes.size();

  *szExts = fileExts.size() * sizeof(extent_t);
  *exts = (extent_t *)malloc(*szExts);
  for (size_t i = 0; i < fileExts.size(); ++i)
    (*exts)[i] = fileExts[i];
  // memcpy(*exts, fileExts.data(), *szExts);

  size_t szSizes = *numFiles * sizeof(size_t);
  *sizes = (size_t *)malloc(szSizes);
  for (size_t i = 0; i < fileSizes.size(); ++i)
    (*sizes)[i] = fileSizes[i];
  // memcpy(*sizes, fileSizes.data(), szSizes);

  return err;
}

#ifdef ISC_DEBUG
static inline void xxd(const char *desc, void *data, size_t len, const char *) {
  // connect parent stdout to child
  pr("%s:", desc);

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

/* -------------------------------------------------------------------------- */
/*                                base64 utils                                */
/* -------------------------------------------------------------------------- */

typedef union {
  uint32_t in32;
  uint8_t in8[4];
} B64_t;

static const char B64MAP[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "abcdefghijklmnopqrstuvwxyz"
                             "0123456789+/";

// clang-format off
static const uint8_t B64UNMAP[] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 0 ~ 42: empty
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 19
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 29
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    62,                                                 // 43 ('+') = 62
    255, 255, 255,                                      // 44 ~ 46: empty
    63,                                                 // 47 ('/') = 63
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61,             // 48 ('0') ~ 57 ('9'): 52 ~ 61
    255, 255, 255, 255, 255, 255, 255,                  // 58 ~ 64: empty
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, // 65 ('A') ~ 90 ('Z')
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, // = 0 ~ 25
    255, 255, 255, 255, 255, 255,                       // 91 ~ 97: empty
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, // 98 ('a') ~ 122 ('z')
    39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, // = 26 ~ 51
};
// clang-format on

static inline char *b64Encode(const char *in, size_t isz, size_t *osz) {
  int trailing = isz % 3;
  int nPad = trailing ? 3 - trailing : 0;
  size_t groups = isz / 3 + !!trailing;

  // allocate output buffer
  *osz = (isz + nPad) / 3 * 4;
  char *out = (char *)calloc(1, *osz + 1);
  if (!out)
    return NULL;

  // convert every 3 in chars to 4 out chars
  B64_t input;
  for (size_t g = 0, io = 0, oo = 0; g < groups; ++g, io += 3, oo += 4) {
    input.in8[0] = io + 2 >= isz ? 0 : in[io + 2];  // fixme:
    input.in8[1] = io + 1 >= isz ? 0 : in[io + 1];  // fixme:
    input.in8[2] = in[io];
    input.in8[3] = 0;
    // in32 = ((u32)in[io] << 16) | ((u32)in[io + 1] << 8) | in[io + 2];

    out[oo + 0] = B64MAP[((input.in32 & 0xFC0000) >> 18) & 0x3F];
    out[oo + 1] = B64MAP[((input.in32 & 0x3F000) >> 12) & 0x3F];
    out[oo + 2] = B64MAP[((input.in32 & 0xFC0) >> 6) & 0x3F];
    out[oo + 3] = B64MAP[input.in32 & 0x3F];
  }

  // may need to pad '=' to output
  for (int iPad = 0; iPad < nPad; ++iPad)
    out[*osz - 1 - iPad] = '=';
  return out;
}

static inline void dumpReqDataB64(const char *data, size_t sz) {
  size_t osz;
  auto out = b64Encode(data, sz, &osz);
  pr("DATA (%lu bytes) '%s'", sz, out);
  free(out);
}
#else
#define xxd(...)
#define dumpReqDataB64(...)
#endif

#endif /* __SIMPLESSD_ISC_BENCH_COMMON_HH__ */