#include "e2fs_utils.hh"

#include "sims/ftl.hh"

#include "runtime.hh"
#include "types.hh"

#include "fs/ext4/ext4.hh"
#include "slet/grep.hh"

using namespace SimpleSSD::ISC;

#define ALIGN_UP(num, to) ((num + (to - 1)) & ~(to - 1))

// static class member (nextSletId) won't be reset within same test file
ISC_STS_SLET_ID cntFSA = 0;
ISC_STS_SLET_ID cntAPP = 0;

static ext2_filsys fs;
static io_manager manager = unix_io_manager;

#undef PR_SECTION
#define PR_SECTION GrepAPP
TEST(SletTest, PR_SECTION) {
  auto disk = "/tmp/sss-isc-test.img";

  initialize_ext2_error_table();
  // initialize the disk for testing
  const char *conf = "scripts/mke2fs.conf";
  mkext4(disk, conf, (4 << 20));

  // create test data
  const char *pathTestFile = "/test.txt";
  const char *dataTestFile = "line0\nline1\nline2\nthis is line3\nline4";
  const auto dlen = strlen(dataTestFile);

  check0(ext2fs_open, disk, EXT2_FLAG_RW, 0, 0, manager, &fs);
  check0(ext2fs_read_block_bitmap, fs);
  check0(ext2fs_read_inode_bitmap, fs);
  newfile(fs, pathTestFile + 1, dataTestFile, dlen, 2);
  check0(ext2fs_flush, fs);
  ext2fs_free(fs);

  // setup env for verification
  SIM::FTL::setImage(disk);
  ASSERT_EQ(Runtime::addSlet<Ext4>(), ++cntFSA);

  auto id = Runtime::addSlet<GrepAPP>();
  ASSERT_EQ(id, ++cntAPP);

  // run slet and verify
  char *path = strdup(pathTestFile);
  {
    ASSERT_NE(path, nullptr);
    auto pat1 = strdup("ne3");
    ASSERT_NE(pat1, nullptr);
    ASSERT_NE(GrepAPP::keyPath, nullptr);
    ASSERT_NE(GrepAPP::keyPatt, nullptr);
    ASSERT_EQ(Runtime::setOpt(id, GrepAPP::keyPath, path), ISC_STS_OK);
    ASSERT_EQ(Runtime::setOpt(id, GrepAPP::keyPatt, pat1), ISC_STS_OK);

    ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);
    auto result1 = (char *)Runtime::getOpt(id, GrepAPP::keyResult);
    auto pResSz1 = (size_t *)Runtime::getOpt(id, GrepAPP::keyResultSize);
    ASSERT_NE(result1, nullptr);
    ASSERT_NE(pResSz1, nullptr);

    auto ans1 = "this is line3";
    auto len1 = strlen(ans1);
    ASSERT_EQ(sizeof(size_t) + ALIGN_UP(len1, sizeof(size_t)), *pResSz1);
    ASSERT_EQ(true, !memcmp(result1, &len1, sizeof(size_t)));
    ASSERT_EQ(true, !memcmp(result1 + sizeof(size_t), ans1, len1));
  }

  // run slet and verify
  {
    auto pat2 = strdup("e0");
    ASSERT_NE(pat2, nullptr);
    ASSERT_EQ(Runtime::setOpt(id, GrepAPP::keyPatt, pat2), ISC_STS_OK);
    ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);

    auto result2 = (char *)Runtime::getOpt(id, GrepAPP::keyResult);
    auto pResSz2 = (size_t *)Runtime::getOpt(id, GrepAPP::keyResultSize);
    ASSERT_NE(result2, nullptr);
    ASSERT_NE(pResSz2, nullptr);

    auto ans2 = "line0";
    auto len2 = strlen(ans2);
    ASSERT_EQ(sizeof(size_t) + ALIGN_UP(len2, sizeof(size_t)), *pResSz2);
    ASSERT_EQ(true, !memcmp(result2, &len2, sizeof(size_t)));
    ASSERT_EQ(true, !memcmp(result2 + sizeof(size_t), ans2, len2));
  }

  Runtime::destory();
  SIM::FTL::destory();
}

#undef PR_SECTION
#define PR_SECTION X_Y(GrepAPP, ManySmall)
TEST(SletTest, PR_SECTION) {
  // init disk
  auto disk = "/tmp/sss-isc-test.img";
  auto conf = "scripts/mke2fs.conf";
  auto szDisk = 2ULL << 30;

  initialize_ext2_error_table();
  mkext4(disk, conf, szDisk);

  // init test files
  auto dirPath = strdup("/");
  const size_t numFiles = 300;
  const size_t szFile = 4 << 10;

  std::vector<GrepAPP::result_t> tests(numFiles, {NULL, 0});

  char *pattern = strdup("target");
  const char *srcLine = "\nyoooo target line yoooo\n";
  const char *tgtLine = srcLine + 1;
  const size_t srcLen = strlen(srcLine);
  const size_t tgtLen = srcLen - 2 /* newlines */;

  for (size_t i = 0; i < numFiles; ++i) {
    auto dat = (char *)calloc(1, szFile);
    ASSERT_NE(dat, nullptr);

    // put random string at file end
    tests[i].len = tgtLen;
    tests[i].line = (char *)tgtLine;
    memcpy(&dat[szFile - srcLen], srcLine, srcLen);

    // create file
    auto path = std::to_string(i);
    ASSERT_NE(path.empty(), true);

    check0(ext2fs_open, disk, EXT2_FLAG_RW, 0, 0, manager, &fs);
    check0(ext2fs_read_block_bitmap, fs);
    check0(ext2fs_read_inode_bitmap, fs);
    newfile(fs, path.c_str(), dat, szFile, 2);
    check0(ext2fs_flush, fs);
    ext2fs_free(fs);

    free(dat);
  }

  /* ------------------------------------------------------------------------ */
  /*                           setup ISC environment                          */
  /* ------------------------------------------------------------------------ */

  SIM::FTL::setImage(disk);
  ASSERT_EQ(Runtime::addSlet<Ext4>(), ++cntFSA);

  auto id = Runtime::addSlet<GrepAPP>();
  ASSERT_EQ(id, ++cntAPP);

  // run slet and verify
  char *path = dirPath;
  ASSERT_NE(dirPath, nullptr);
  ASSERT_NE(pattern, nullptr);
  ASSERT_NE(GrepAPP::keyPath, nullptr);
  ASSERT_NE(GrepAPP::keyPatt, nullptr);
  ASSERT_EQ(Runtime::setOpt(id, GrepAPP::keyPath, path), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, GrepAPP::keyPatt, pattern), ISC_STS_OK);

  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);
  auto result = (char *)Runtime::getOpt(id, GrepAPP::keyResult);
  auto pResSz = (size_t *)Runtime::getOpt(id, GrepAPP::keyResultSize);
  ASSERT_NE(result, nullptr);
  ASSERT_NE(pResSz, nullptr);

  ASSERT_EQ(*pResSz,
            numFiles * (sizeof(size_t) + ALIGN_UP(tgtLen, sizeof(size_t))));

  size_t ofsResult = 0;
  for (size_t i = 0; i < numFiles; ++i) {
    auto res = &result[ofsResult];
    auto &ans = tests[i];

    ASSERT_EQ(true, !memcmp(res, &ans.len, sizeof(size_t)));
    ASSERT_EQ(true, !memcmp(res + sizeof(size_t), ans.line, ans.len));
  }

  Runtime::destory();
  SIM::FTL::destory();
}

#undef NO_LOG_ID_CHECK
#define NO_LOG_ID_CHECK 1

#undef PR_SECTION
#define PR_SECTION X_Y(GrepAPP, NO_FSA)
TEST(SletTest, PR_SECTION) {
  const char *disk = "/tmp/sss-isc-test.img";
  const char *pathFile = "/test.txt";
  const size_t disklen = 4 << 20;
  const size_t dlen = 2 << 20;

  // generate data
  char *pattern = strdup("target");
  const char *srcLine = "\nyoooo target line yoooo\n";
  const char *tgtLine = srcLine + 1;
  const size_t srcLen = strlen(srcLine);
  const size_t tgtLen = srcLen - 2;

  auto data = (char *)calloc(1, dlen);
  memcpy(&data[dlen - srcLen], srcLine, srcLen);
  simple_newfile(pathFile, data, dlen, disklen, disk);

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
    std::vector<size_t> sizes;
    std::vector<std::string> names;
    std::vector<std::vector<Ext>> fileExtLists;
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

        std::vector<Ext> exts;
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
        d->names.push_back(std::string(de2->name, de2->name_len));
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

  auto id = Runtime::addSlet<GrepAPP>();
  ASSERT_EQ(id, ++cntAPP);

  // run slet and verify
  char *path = path = strdup(pathFile);

  ASSERT_NE(path, nullptr);
  ASSERT_NE(GrepAPP::keyPath, nullptr);

  ASSERT_EQ(Runtime::setOpt(id, GrepAPP::keyPatt, pattern), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, GrepAPP::keyPath, path), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, GrepAPP::keyNumFiles, numFiles), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, GrepAPP::keyFileSizes, sizes), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, GrepAPP::keyExts, exts), ISC_STS_OK);

  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);
  auto res = (char *)Runtime::getOpt(id, GrepAPP::keyResult);
  auto pResSz = (size_t *)Runtime::getOpt(id, GrepAPP::keyResultSize);
  ASSERT_NE(res, nullptr);
  ASSERT_NE(pResSz, nullptr);
  ASSERT_EQ(sizeof(size_t) + ALIGN_UP(tgtLen, sizeof(size_t)), *pResSz);

  ASSERT_EQ(memcmp(res, &tgtLen, sizeof(size_t)), 0);
  ASSERT_EQ(memcmp(res + sizeof(size_t), tgtLine, tgtLen), 0);

  free(data);
  Runtime::destory();
  SIM::FTL::destory();
}
