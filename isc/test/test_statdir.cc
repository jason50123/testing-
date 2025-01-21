#include "gtest/gtest.h"

// ISC headers
#include "fs/ext4/ext4.hh"
#include "runtime.hh"
#include "sims/ftl.hh"

#include "slet/statdir.hh"

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
#define ISC_APP_CLASS StatdirAPP
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
  typedef struct {
    uint32_t mtime;
    uint32_t size;
    uint32_t mode;
    char name[256];
  } private_t;

  struct Data {
    size_t i;
    private_t *res;
    size_t resSize;
  } data = {0, (private_t *)result, *resSize};

  check0(ext2fs_open, disk, 0, 0, 0, manager, &fs);
  check0(ext2fs_namei, fs, 2, 2, dirpath, &diri);

  pr("%-15s|%-10s|%-10s|%s", "Mod Time", "Bytes", "Perm", "File");
  check0(
      ext2fs_dir_iterate, fs, diri, 0, nullptr,
      [](ext2_dir_entry *_e, int ofs, int, char *, void *data) {
        auto d [[maybe_unused]] = (Data *)data;
        auto e = (ext2_dir_entry_2 *)_e;

        char name[256] = {0};
        memcpy(name, e->name, std::min((uint8_t)255, e->name_len));
        pr("checking dirent '%s'(%d) (ofs: 0x%x)", name, e->inode, ofs);

        // compare file name
        EXPECT_EQ(0, strcmp(d->res[d->i].name, name))
            << printf("At ino[%u]\n", e->inode)
            << printf("Expected name '%s', but got '%s'\n", name,
                      d->res[d->i].name);

        // compare inode data
        ext2_inode inode;
        ext2fs_read_inode(fs, e->inode, &inode);

        EXPECT_EQ(inode.i_mtime, d->res[d->i].mtime);
        EXPECT_EQ(inode.i_mode, d->res[d->i].mode);
        EXPECT_EQ(inode.i_size, d->res[d->i].size);

        auto di = d->res[d->i];
        pr("%-15u|%-10u|%-10o|%s", di.mtime, di.size, di.mode, di.name);

        ++d->i;
        return 0;
      },
      &data);

  ext2fs_close(fs);

  Runtime::destory();
  FTL::destory();
}
