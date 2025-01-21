#include "gtest/gtest.h"

// ISC headers
#include "fs/ext4/ext4.hh"
#include "runtime.hh"
#include "sims/ftl.hh"

#include "sims/configs.hh"
#include "slet/listdir.hh"

// debugfs
#include "e2fs_utils.hh"

static ext2_filsys fs;
static io_manager manager = unix_io_manager;

using namespace SimpleSSD::Utils;
using namespace SimpleSSD::ISC;
using namespace SimpleSSD::ISC::SIM;

static inline void mkdirs_nxm(const char *disk, ext2_ino_t diri, int n, int m) {
  check0(ext2fs_open, disk, EXT2_FLAG_RW, 0, 0, manager, &fs);
  check0(ext2fs_read_block_bitmap, fs);
  check0(ext2fs_read_inode_bitmap, fs);

  char name[50] = {0};
  ext2_ino_t newi = 0;
  for (int i = 0; i < n; ++i) {
    snprintf(name, 50, "d%d", i);
    newdir(fs, diri, name, &newi);
    EXPECT_NE(newi, 0U);

    for (int j = 0; j < m; ++j) {
      snprintf(name, 50, "d%df%d", i, j);
      newfile(fs, name, name, strlen(name), newi);
    }
  }
  check0(ext2fs_flush, fs);
  ext2fs_free(fs);
}

static ISC_STS_SLET_ID cntAPP = 0, cntFSA = 0;

#undef ISC_APP_CLASS
#undef ISC_FSA_CLASS
#define ISC_APP_CLASS ListdirAPP
#define ISC_FSA_CLASS Ext4
#undef PR_SECTION
#define PR_SECTION X_Y(ISC_FSA_CLASS, ISC_APP_CLASS)
TEST(SletTest, PR_SECTION) {
  /* ------------------------------------------------------------------------ */
  /*                        prepare disk and test data                        */
  /* ------------------------------------------------------------------------ */
  initialize_ext2_error_table();

  auto disk = "/tmp/sss-isc-test.img";
  auto conf = "scripts/mke2fs.conf";
  auto szDisk = 128 << 20;
  ext2_ino_t diri = 2;
  auto dirpath = "/";
  mkext4(disk, conf, szDisk);

  // create many dirs
  mkdirs_nxm(disk, diri, 1000, 10);

  /* ------------------------------------------------------------------------ */
  /*                              main test codes                             */
  /* ------------------------------------------------------------------------ */

  // setup env for verification
  SIM::FTL::setImage(disk);
  ASSERT_EQ(Runtime::addSlet<ISC_FSA_CLASS>(), ++cntFSA);
  auto id = Runtime::addSlet<ISC_APP_CLASS>();
  ASSERT_EQ(id, ++cntAPP);

  // run
  ASSERT_EQ(Runtime::setOpt(id, ISC_APP_CLASS::keyPath, strdup(dirpath)),
            ISC_STS_OK);
  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);
  auto result = (char *)Runtime::getOpt(id, ISC_APP_CLASS::keyResult);
  auto resSize = (uint64_t *)Runtime::getOpt(id, ISC_APP_CLASS::keyResultSize);
  ASSERT_NE(result, nullptr);
  ASSERT_NE(resSize, nullptr);

  // verify
  struct Data {
    size_t ofs;
    char *res;
    size_t resSize;
  } data = {0, result, *resSize};

  check0(ext2fs_open, disk, 0, 0, 0, manager, &fs);
  check0(ext2fs_namei, fs, 2, 2, dirpath, &diri);

  check0(
      ext2fs_dir_iterate, fs, diri, 0, nullptr,
      [](ext2_dir_entry *_e, int ofs, int, char *, void *data) {
        auto d = (Data *)data;
        auto e = (ext2_dir_entry_2 *)_e;

        char name[256] = {0};
        snprintf(name, std::min(256, e->name_len + 1), "%s", e->name);
        pr("checking dirent '%s'(%d) (ofs: 0x%x)", name, e->inode, ofs);

        // compare file name
        char xxdExArg[256] = {0};
        snprintf(xxdExArg, 256, "-s %lu", d->ofs);

        auto res_name = ((ext2_dir_entry_2 *)&d->res[d->ofs])->name;
        EXPECT_EQ(memcmp(e, &d->res[d->ofs], e->rec_len), 0)
            << (pipe2xxd("answer:", e, e->rec_len, NULL),
                " vs " /* evert not last params will still be evaluated */)
            << (pipe2xxd("result:", d->res, d->resSize - d->ofs, xxdExArg), "")
            << printf("At offset %lu\n", d->ofs)
            << printf("Expect '%s', but got '%s'\n", e->name, res_name);

        // skip this dentry and 1 dummp tail dentry (checksum)
        d->ofs += ((ext2_dir_entry_2 *)&d->res[d->ofs])->rec_len;

        if (!((ext2_dir_entry_2 *)&d->res[d->ofs])->inode &&
            ((ext2_dir_entry_2 *)&d->res[d->ofs])->file_type == 0xde)
          d->ofs += ((ext2_dir_entry_2 *)&d->res[d->ofs])->rec_len;

        return 0;
      },
      &data);

  ext2fs_close(fs);
  Runtime::destory();
  FTL::destory();
}
