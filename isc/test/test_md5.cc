#include "gtest/gtest.h"

#include <sys/fcntl.h>
#include <ctime>
#include <vector>
using namespace std;

#include "runtime.hh"
#include "sims/configs.hh"
#include "sims/ftl.hh"

#include "fs/ext4/ext4.hh"
#include "slet/md5.hh"

#include "utils/debug.hh"

// debugfs
#include "e2fs_utils.hh"
static ext2_filsys fs;
static io_manager manager = unix_io_manager;

using namespace SimpleSSD::Utils;
using namespace SimpleSSD::ISC;
using namespace SimpleSSD::ISC::SIM;

#define ISC_APP_CLASS MD5APP

#undef NO_LOG_ID_CHECK
#define NO_LOG_ID_CHECK 1

ISC_STS_SLET_ID cntFSA = 0;
ISC_STS_SLET_ID cntAPP = 0;

typedef struct TestCase {
  const char *s;
  union digest_t {
    uint64_t dwords[2];
    uint32_t words[4];
    uint8_t bytes[16];
  } ans;
  uint32_t sz;
  char ansText[33];  // answer from external tool, which output bytes strings
} TestCase_t;

#define MD5(s, a, b, c, d)                                                     \
  {                                                                            \
    s, {.words = {d, c, b, a}}, 0, {                                           \
      0                                                                        \
    }                                                                          \
  }

#undef PR_SECTION
#define PR_SECTION X_Y(ISC_APP_CLASS, SimpleStrings)
TEST(SletTest, PR_SECTION) {
  // clang-format off
  TestCase_t tests[] = {
    MD5("", 0xd41d8cd9, 0x8f00b204, 0xe9800998, 0xecf8427e),
    MD5("a", 0x0cc175b9, 0xc0f1b6a8, 0x31c399e2, 0x69772661),
    MD5("abc", 0x90015098, 0x3cd24fb0, 0xd6963f7d, 0x28e17f72),
    MD5("message digest", 0xf96b697d, 0x7cb7938d, 0x525a2f31, 0xaaf161d0),
    MD5("abcdefghijklmnopqrstuvwxyz", 0xc3fcd3d7, 0x6192e400, 0x7dfb496c, 0xca67e13b),
    MD5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 0xd174ab98, 0xd277d9f5, 0xa5611c2c, 0x9f419d9f),
    MD5("12345678901234567890123456789012345678901234567890123456789012345678901234567890", 0x57edf4a2, 0x2be3c955, 0xac49da2e, 0x2107b67a),
  };
  // clang-format on

  /* ------------------------------------------------------------------------ */
  /*                           setup ISC environment                          */
  /* ------------------------------------------------------------------------ */

  auto slet = new ISC_APP_CLASS();
  for (auto &test : tests) {
    TestCase::digest_t out;

    slet->md5sum((uint8_t *)test.s, strlen(test.s), out.words);

    // swap endian
    for (uint8_t i = 0, t; i < 8; ++i) {
      t = out.bytes[i];
      out.bytes[i] = out.bytes[15 - i];
      out.bytes[15 - i] = t;
    }

    // verify
    pr("Testing '%s'", test.s);
    ASSERT_EQ(0, memcmp(test.ans.words, out.words, sizeof(out)))
        << [](uint32_t *ans, uint32_t *res) -> int {
      for (int i = 0; i < 3; i++) {
        if (ans[i] != res[i]) {
          perr("Expect %08x != %08x, At byte: ", ans[i], res[i]);
          return i;
        }
      }
      return -1;
    }(test.ans.words, out.words);

    pr("%016lx%016lx", out.dwords[1], out.dwords[0]);
  }
  delete slet;
}

// used for converting md5sum result to byte array
static inline uint8_t hex2bin(char hexChar) {
  if (isdigit(hexChar))
    return hexChar - '0';
  if (isxdigit(hexChar))
    return tolower(hexChar) - 'a' + 10;
  return -1;
}

#undef PR_SECTION
#define PR_SECTION X_Y(ISC_APP_CLASS, RandomBytes)
TEST(SletTest, PR_SECTION) {
  srand(time(0));

  auto slet = new ISC_APP_CLASS();
  for (int i = 0; i < 30; ++i) {
    TestCase test;

    // determine size and allocate buffer
    test.sz = (1 << i) + rand() % (1 << i);
    ASSERT_NE(nullptr, test.s = (char *)calloc(1, test.sz));

    // read from /dev/random, get 32 ascii (bytes) answer from /bin/md5sum
    int fd, szrd;
    ASSERT_GT(fd = open("/dev/random", O_RDONLY), 0);
    ASSERT_GT(szrd = read(fd, (void *)test.s, test.sz), 0) << strerror(errno);
    ASSERT_GE(test.sz, (size_t)szrd);

    test.ansText[32] = {0};
    pipe2md5(test.s, test.sz, test.ansText);

    // ascii hex string to byte array
    for (int i = 0, j = 0; i < 16; ++i, j += 2)
      test.ans.bytes[15 - i] =
          hex2bin(test.ansText[j]) << 4 | hex2bin(test.ansText[j + 1]);

    TestCase::digest_t out;
    timespec tsBeg, tsEnd;
    auto ts2s = [](timespec ts) -> double {
      return (ts).tv_sec + (ts).tv_nsec / 1e9;
    };

    clock_gettime(CLOCK_REALTIME, &tsBeg);
    slet->md5sum((uint8_t *)test.s, test.sz, out.words);
    clock_gettime(CLOCK_REALTIME, &tsEnd);

    // show speed
    double bps = test.sz / (ts2s(tsEnd) - ts2s(tsBeg));
    if (bps >= 1e9)
      pr("Md5sum %u Bytes, speed = %.2f GB/s", test.sz, bps / 1e9);
    else if (bps >= 1e6)
      pr("Md5sum %u Bytes, speed = %.2f MB/s", test.sz, bps / 1e6);
    else if (bps >= 1e3)
      pr("Md5sum %u Bytes, speed = %.2f KB/s", test.sz, bps / 1e3);
    else
      pr("Md5sum %u Bytes, speed = %.2f B/s", test.sz, bps);

    // swap endian
    for (uint8_t i = 0, t; i < 8; ++i) {
      t = out.bytes[i];
      out.bytes[i] = out.bytes[15 - i];
      out.bytes[15 - i] = t;
    }

    // verify
    ASSERT_EQ(0, memcmp(test.ans.words, out.words, sizeof(out)))
        << [](uint32_t *ans, uint32_t *res) -> int {
      for (int i = 0; i < 8; i++) {
        if (ans[i] != res[i]) {
          printf("Expect %08x != %08x, At word: \n", ans[i], res[i]);
          return i;
        }
      }
      return -1;
    }(test.ans.words, out.words);
    pr("%016lx%016lx", out.dwords[1], out.dwords[0]);

    free((char *)test.s);
  }
  delete slet;
}

#undef PR_SECTION
#define PR_SECTION ISC_APP_CLASS
TEST(SletTest, PR_SECTION) {
  // init disk
  auto disk = "/tmp/sss-isc-test.img";
  auto conf = "scripts/mke2fs.conf";
  auto szDisk = 2ULL << 30;

  TestCase test;

  initialize_ext2_error_table();
  mkext4(disk, conf, szDisk);

  // init test file
  test.sz = 1ULL << 30;
  test.s = (char *)calloc(1, test.sz);
  ASSERT_NE(test.s, nullptr);

  // random data from /dev/random
  auto fd = open("/dev/random", O_RDONLY);
  ASSERT_GE(fd, 0);
  auto szrd = read(fd, (char *)test.s, test.sz);
  ASSERT_GT(szrd, 0);
  ASSERT_LT(szrd, UINT32_MAX);
  ASSERT_EQ(test.sz, (uint32_t)szrd);
  ASSERT_EQ(0, close(fd));

  test.ansText[32] = {0};
  pipe2md5(test.s, test.sz, test.ansText);

  for (int i = 0, j = 0; i < 16; ++i, j += 2)
    test.ans.bytes[15 - i] =
        hex2bin(test.ansText[j]) << 4 | hex2bin(test.ansText[j + 1]);

  // create file
  auto path = strdup("/data");
  ASSERT_NE(path, nullptr);

  check0(ext2fs_open, disk, EXT2_FLAG_RW, 0, 0, manager, &fs);
  check0(ext2fs_read_block_bitmap, fs);
  check0(ext2fs_read_inode_bitmap, fs);
  newfile(fs, path + 1, test.s, test.sz, 2);
  check0(ext2fs_flush, fs);
  ext2fs_free(fs);

  /* ------------------------------------------------------------------------ */
  /*                           setup ISC environment                          */
  /* ------------------------------------------------------------------------ */

  FTL::setImage(disk);
  ASSERT_EQ(Runtime::addSlet<Ext4>(), ++cntFSA);
  auto id = Runtime::addSlet<ISC_APP_CLASS>();
  ASSERT_EQ(id, ++cntAPP);

  // run
  ASSERT_EQ(Runtime::setOpt(id, ISC_APP_CLASS::keyPath, path), ISC_STS_OK);
  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);

  auto resSize = (uint32_t *)Runtime::getOpt(id, ISC_APP_CLASS::keyResultSize);
  ASSERT_NE(resSize, nullptr);
  auto result = (uint8_t *)Runtime::getOpt(id, ISC_APP_CLASS::keyResult);
  ASSERT_NE(result, nullptr);

  // verify
  for (uint8_t i = 0, t; i < 8; ++i) {
    t = result[i];
    result[i] = result[15 - i];
    result[15 - i] = t;
  }
  ASSERT_EQ(sizeof(test.ans), *resSize);
  ASSERT_EQ(0, memcmp(result, test.ans.bytes, sizeof(test.ans)))
      << [](uint64_t *ans, uint64_t *res) -> const char * {
    printf("Expect: %016lx%016lx\n", ans[1], ans[0]);
    printf("Got: %016lx%016lx\n", res[1], res[0]);
    return "";
  }(test.ans.dwords, (uint64_t *)result);

  // done
  Runtime::destory();
  FTL::destory();
  free((char *)test.s);
}

#undef PR_SECTION
#define PR_SECTION X_Y(ISC_APP_CLASS, ManySmall)
TEST(SletTest, PR_SECTION) {
  // init disk
  auto disk = "/tmp/sss-isc-test.img";
  auto conf = "scripts/mke2fs.conf";
  auto szDisk = 2ULL << 30;

  initialize_ext2_error_table();
  mkext4(disk, conf, szDisk);

  // random data from /dev/random
  auto fd = open("/dev/random", O_RDONLY);
  ASSERT_GE(fd, 0);

  // init test files
  auto dirPath = strdup("/");
  size_t numFiles = 300;
  std::vector<TestCase> tests;

  for (size_t i = 0; i < numFiles; ++i) {
    TestCase test;

    test.sz = 4 << 10;
    test.s = (char *)calloc(1, test.sz);
    ASSERT_NE(test.s, nullptr);

    auto szrd = read(fd, (char *)test.s, test.sz);
    ASSERT_GT(szrd, 0);
    ASSERT_LT(szrd, UINT32_MAX);
    ASSERT_EQ(test.sz, (uint32_t)szrd);

    test.ansText[32] = {0};
    pipe2md5(test.s, test.sz, test.ansText);

    for (int i = 0, j = 0; i < 16; ++i, j += 2)
      test.ans.bytes[15 - i] =
          hex2bin(test.ansText[j]) << 4 | hex2bin(test.ansText[j + 1]);

    // create file
    auto path = std::to_string(i);
    ASSERT_NE(path.empty(), true);

    check0(ext2fs_open, disk, EXT2_FLAG_RW, 0, 0, manager, &fs);
    check0(ext2fs_read_block_bitmap, fs);
    check0(ext2fs_read_inode_bitmap, fs);
    newfile(fs, path.c_str(), test.s, test.sz, 2);
    check0(ext2fs_flush, fs);
    ext2fs_free(fs);

    tests.push_back(test);
  }

  ASSERT_EQ(0, close(fd));

  /* ------------------------------------------------------------------------ */
  /*                           setup ISC environment                          */
  /* ------------------------------------------------------------------------ */

  FTL::setImage(disk);
  ASSERT_EQ(Runtime::addSlet<Ext4>(), ++cntFSA);
  auto id = Runtime::addSlet<ISC_APP_CLASS>();
  ASSERT_EQ(id, ++cntAPP);

  // run
  ASSERT_EQ(Runtime::setOpt(id, ISC_APP_CLASS::keyPath, dirPath), ISC_STS_OK);
  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);

  auto resSize = (uint32_t *)Runtime::getOpt(id, ISC_APP_CLASS::keyResultSize);
  ASSERT_NE(resSize, nullptr);
  auto results = (uint8_t *)Runtime::getOpt(id, ISC_APP_CLASS::keyResult);
  ASSERT_NE(results, nullptr);

  ASSERT_EQ(16 * tests.size(), *resSize);

  // verify
  for (size_t iTest = 0, ofsResult = 0; iTest < tests.size(); ++iTest) {
    auto test = tests.at(iTest);
    auto result = &results[ofsResult];
    for (uint8_t i = 0, t; i < 8; ++i) {
      t = result[i];
      result[i] = result[15 - i];
      result[15 - i] = t;
    }
    ofsResult += 16;

    ASSERT_EQ(0, memcmp(result, test.ans.bytes, sizeof(test.ans)))
        << [iTest](uint64_t *ans, uint64_t *res) -> const char * {
      printf("File[%lu]:\n", iTest);
      printf("Expect: %016lx%016lx\n", ans[1], ans[0]);
      printf("Got: %016lx%016lx\n", res[1], res[0]);
      return "";
    }(test.ans.dwords, (uint64_t *)result);

    free((char *)test.s);
  }

  // done
  Runtime::destory();
  FTL::destory();
}

#undef PR_SECTION
#define PR_SECTION X_Y(ISC_APP_CLASS, NO_FSA)
TEST(SletTest, PR_SECTION) {
  const char *disk = "/tmp/sss-isc-test.img";
  const char *pathFile = "/test.txt";
  const size_t disklen = 4 << 20;
  const size_t dlen = 2 << 20;
  auto data = (char *)simple_random_newfile(pathFile, dlen, disklen, disk);

  ext2_ino_t diri = 2;
  const char *dirname = "/";

  struct Ext {
    size_t block;
    size_t slbn;
    size_t len;

    Ext() : block(UINT64_MAX), slbn(UINT64_MAX), len(UINT64_MAX) {}
    Ext(size_t log, size_t phy, size_t len) : block(log), slbn(phy), len(len) {}
  };

  struct data_t {
    size_t numFiles;
    size_t numExts;
    vector<size_t> sizes;
    vector<string> names;
    vector<vector<Ext>> fileExtLists;
  } d = {0, 0, {}, {}, {}};

  check0(ext2fs_open, disk, EXT2_FLAG_RW, 0, 0, manager, &fs);
  check0(ext2fs_namei, fs, 2, 2, dirname, &diri);
  check0(
      ext2fs_dir_iterate, fs, diri, 0, nullptr,
      [](ext2_dir_entry *de, int, int, char *, void *private_data) {
        auto d = (struct data_t *)private_data;

        auto de2 = (ext2_dir_entry_2 *)de;
        if (de2->file_type != 1 /* reg file */)
          return 0;

        // get extent info of this inode
        pr("Found file '%s' (ino=%d)", de2->name, de2->inode);

        ext2_inode inode;
        check0(ext2fs_read_inode, fs, de2->inode, &inode);

        ext2_extent_handle_t handle;
        ext2fs_extent e;
        ext2fs_extent_open(fs, de2->inode, &handle);

        int err;
        int op = EXT2_EXTENT_ROOT;

        vector<Ext> exts;
        while (1) {
          err = ext2fs_extent_get(handle, op, &e);
          if (err)
            break;
          op = EXT2_EXTENT_NEXT;
          exts.push_back(Ext(e.e_lblk, e.e_pblk, e.e_len));
        }
        exts.push_back(Ext());
        ext2fs_extent_free(handle);

        d->numFiles++;
        d->numExts += exts.size();
        d->sizes.push_back(inode.i_size);
        d->names.push_back(string(de2->name, de2->name_len));
        d->fileExtLists.push_back(exts);

        return 0;
      },
      &d);

  size_t *sizes = (size_t *)malloc(sizeof(size_t) * d.numFiles);
  Ext *exts = (Ext *)malloc(sizeof(Ext) * d.numExts);
  size_t *numFiles = (size_t *)malloc(sizeof(size_t));
  *numFiles = d.numFiles;

  for (size_t iFile = 0, iExt = 0; iFile < d.numFiles; ++iFile) {
    pr("File[%lu] '%s' extents (%lu bytes)", iFile, d.names[iFile].c_str(),
       d.sizes[iFile]);

    sizes[iFile] = d.sizes[iFile];
    for (size_t i = 0; i < d.fileExtLists[iFile].size(); ++i, ++iExt) {
      auto e = exts[iExt] = d.fileExtLists[iFile][i];
      if (exts[iExt].block == UINT64_MAX)
        continue;

      auto elbn = e.slbn + e.len - 1;
      pr("\t(%lu): %lu,%lu-%lu(+%lu)", i, e.block, e.slbn, elbn, e.len);
    }
  }
  check0(ext2fs_close, fs);
  ext2fs_free(fs);

  // setup env for verification
  SIM::FTL::setImage(disk);
  ASSERT_EQ(Runtime::addSlet<Ext4>(), ++cntFSA);

  auto id = Runtime::addSlet<ISC_APP_CLASS>();
  ASSERT_EQ(id, ++cntAPP);

  // run slet and verify
  char *path = path = strdup(pathFile);

  ASSERT_NE(path, nullptr);
  ASSERT_NE(ISC_APP_CLASS::keyPath, nullptr);

  ASSERT_EQ(Runtime::setOpt(id, ISC_APP_CLASS::keyPath, path), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, ISC_APP_CLASS::keyNumFiles, numFiles),
            ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, ISC_APP_CLASS::keyFileSizes, sizes),
            ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, ISC_APP_CLASS::keyExts, exts), ISC_STS_OK);

  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);
  auto res =
      (ISC_APP_CLASS::result_t *)Runtime::getOpt(id, ISC_APP_CLASS::keyResult);
  auto pResSz = (size_t *)Runtime::getOpt(id, ISC_APP_CLASS::keyResultSize);
  ASSERT_NE(res, nullptr);
  ASSERT_NE(pResSz, nullptr);
  ASSERT_EQ(sizeof(ISC_APP_CLASS::result_t), *pResSz);

  TestCase test;
  test.s = data;
  test.sz = dlen;
  test.ansText[32] = {0};
  pipe2md5(test.s, test.sz, test.ansText);
  for (int i = 0, j = 0; i < 16; ++i, j += 2)
    test.ans.bytes[15 - i] =
        hex2bin(test.ansText[j]) << 4 | hex2bin(test.ansText[j + 1]);

  for (uint8_t i = 0, t; i < 8; ++i) {
    t = res->data[i];
    res->data[i] = res->data[15 - i];
    res->data[15 - i] = t;
  }

  ASSERT_EQ(memcmp(res->data, test.ans.bytes, *pResSz), 0);

  free(data);
  Runtime::destory();
  SIM::FTL::destory();
}
