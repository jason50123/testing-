#include "e2fs_utils.hh"

#include "sims/ftl.hh"

#include "runtime.hh"
#include "types.hh"

#include "fs/ext4/ext4.hh"
#include "slet/stats32.hh"
#include "slet/stats64.hh"

#include <vector>
using namespace std;

using namespace SimpleSSD::ISC;

// static class member (nextSletId) won't be reset within same test file
static ISC_STS_SLET_ID cntFSA = 0;
static ISC_STS_SLET_ID cntAPP = 0;

static ext2_filsys fs;
static io_manager manager = unix_io_manager;

#undef PR_SECTION
#define PR_SECTION Stats32APP
TEST(SletTest, PR_SECTION) {
  const char *disk = "/tmp/sss-isc-test.img";
  const char *pathFile = "/test.txt";
  const size_t disklen = 4 << 20;
  const size_t dlen = 2 << 20;
  auto data = (int32_t *)simple_random_newfile(pathFile, dlen, disklen, disk);

  // setup env for verification
  SIM::FTL::setImage(disk);
  ASSERT_EQ(Runtime::addSlet<Ext4>(), ++cntFSA);

  auto id = Runtime::addSlet<PR_SECTION>();
  ASSERT_EQ(id, ++cntAPP);

  // run slet and verify
  char *path = path = strdup(pathFile);

  ASSERT_NE(path, nullptr);
  ASSERT_NE(PR_SECTION::keyPath, nullptr);
  ASSERT_EQ(Runtime::setOpt(id, PR_SECTION::keyPath, path), ISC_STS_OK);

  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);
  auto res = (PR_SECTION::result_t *)Runtime::getOpt(id, PR_SECTION::keyResult);
  auto pResSz = (size_t *)Runtime::getOpt(id, PR_SECTION::keyResultSize);
  ASSERT_NE(res, nullptr);
  ASSERT_NE(pResSz, nullptr);

  PR_SECTION::result_t ans = {0, 0, 0};
  for (size_t i = 0; i < (dlen / sizeof(int32_t)); ++i) {
    ans.sum += data[i];
    ans.max = std::max(ans.max, data[i]);
    ans.min = std::min(ans.min, data[i]);
  }
  ASSERT_EQ(sizeof(PR_SECTION::result_t), *pResSz);
  ASSERT_EQ(ans.max, res->max);
  ASSERT_EQ(ans.min, res->min);
  ASSERT_EQ(ans.sum, res->sum);

  free(data);
  Runtime::destory();
  SIM::FTL::destory();
}

#define SLET_APP Stats32APP

#undef PR_SECTION
#define PR_SECTION X_Y(SLET_APP, NO_FSA)
TEST(SletTest, PR_SECTION) {
  const char *disk = "/tmp/sss-isc-test.img";
  const char *pathFile = "/test.txt";
  const size_t disklen = 4 << 20;
  const size_t dlen = 2 << 20;
  auto data = (int32_t *)simple_random_newfile(pathFile, dlen, disklen, disk);

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

  auto id = Runtime::addSlet<SLET_APP>();
  ASSERT_EQ(id, ++cntAPP);

  // run slet and verify
  char *path = path = strdup(pathFile);

  ASSERT_NE(path, nullptr);
  ASSERT_NE(SLET_APP::keyPath, nullptr);

  ASSERT_EQ(Runtime::setOpt(id, SLET_APP::keyPath, path), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, SLET_APP::keyNumFiles, numFiles), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, SLET_APP::keyFileSizes, sizes), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, SLET_APP::keyExts, exts), ISC_STS_OK);

  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);
  auto res = (SLET_APP::result_t *)Runtime::getOpt(id, SLET_APP::keyResult);
  auto pResSz = (size_t *)Runtime::getOpt(id, SLET_APP::keyResultSize);
  ASSERT_NE(res, nullptr);
  ASSERT_NE(pResSz, nullptr);

  SLET_APP::result_t ans = {0, 0, 0};
  for (size_t i = 0; i < (dlen / sizeof(int32_t)); ++i) {
    ans.sum += data[i];
    ans.max = std::max(ans.max, data[i]);
    ans.min = std::min(ans.min, data[i]);
  }
  ASSERT_EQ(sizeof(SLET_APP::result_t), *pResSz);
  ASSERT_EQ(ans.max, res->max);
  ASSERT_EQ(ans.min, res->min);
  ASSERT_EQ(ans.sum, res->sum);

  free(data);
  Runtime::destory();
  SIM::FTL::destory();
}

#undef PR_SECTION
#define PR_SECTION Stats64APP
TEST(SletTest, PR_SECTION) {
  const char *disk = "/tmp/sss-isc-test.img";
  const char *pathFile = "/test.txt";
  const size_t disklen = 4 << 20;
  const size_t dlen = 2 << 20;
  auto data = (int64_t *)simple_random_newfile(pathFile, dlen, disklen, disk);

  // setup env for verification
  SIM::FTL::setImage(disk);
  ASSERT_EQ(Runtime::addSlet<Ext4>(), ++cntFSA);

  auto id = Runtime::addSlet<PR_SECTION>();
  ASSERT_EQ(id, ++cntAPP);

  // run slet and verify
  char *path = path = strdup(pathFile);

  ASSERT_NE(path, nullptr);
  ASSERT_NE(PR_SECTION::keyPath, nullptr);
  ASSERT_EQ(Runtime::setOpt(id, PR_SECTION::keyPath, path), ISC_STS_OK);

  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);
  auto res = (PR_SECTION::result_t *)Runtime::getOpt(id, PR_SECTION::keyResult);
  auto pResSz = (size_t *)Runtime::getOpt(id, PR_SECTION::keyResultSize);
  ASSERT_NE(res, nullptr);
  ASSERT_NE(pResSz, nullptr);
  ASSERT_EQ(sizeof(PR_SECTION::result_t), *pResSz);

  PR_SECTION::result_t ans = {0, 0, 0};

  // make sanitizer quiet
  auto quiet_uadd = [](PR_SECTION::result_t &ans, int64_t *data) NO_UINT_SANS {
    for (size_t i = 0; i < (dlen / sizeof(int64_t)); ++i) {
      ans.sum += *(uint64_t *)&data[i];
      ans.max = std::max(ans.max, data[i]);
      ans.min = std::min(ans.min, data[i]);
    }
  };
  quiet_uadd(ans, data);
  ASSERT_EQ(ans.max, res->max);
  ASSERT_EQ(ans.min, res->min);
  ASSERT_EQ(ans.sum, res->sum);

  free(data);
  Runtime::destory();
  SIM::FTL::destory();
}

#undef SLET_APP
#define SLET_APP Stats64APP

#undef PR_SECTION
#define PR_SECTION X_Y(SLET_APP, NO_FSA)
TEST(SletTest, PR_SECTION) {
  const char *disk = "/tmp/sss-isc-test.img";
  const char *pathFile = "/test.txt";
  const size_t disklen = 4 << 20;
  const size_t dlen = 2 << 20;
  auto data = (int64_t *)simple_random_newfile(pathFile, dlen, disklen, disk);

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

  auto id = Runtime::addSlet<SLET_APP>();
  ASSERT_EQ(id, ++cntAPP);

  // run slet and verify
  char *path = path = strdup(pathFile);

  ASSERT_NE(path, nullptr);
  ASSERT_NE(SLET_APP::keyPath, nullptr);

  ASSERT_EQ(Runtime::setOpt(id, SLET_APP::keyPath, path), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, SLET_APP::keyNumFiles, numFiles), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, SLET_APP::keyFileSizes, sizes), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, SLET_APP::keyExts, exts), ISC_STS_OK);

  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);
  auto res = (SLET_APP::result_t *)Runtime::getOpt(id, SLET_APP::keyResult);
  auto pResSz = (size_t *)Runtime::getOpt(id, SLET_APP::keyResultSize);
  ASSERT_NE(res, nullptr);
  ASSERT_NE(pResSz, nullptr);
  ASSERT_EQ(sizeof(SLET_APP::result_t), *pResSz);

  SLET_APP::result_t ans = {0, 0, 0};
  auto quiet_uadd = [](SLET_APP::result_t &ans, int64_t *data) NO_UINT_SANS {
    for (size_t i = 0; i < (dlen / sizeof(int64_t)); ++i) {
      ans.sum += *(uint64_t *)&data[i];
      ans.max = std::max(ans.max, data[i]);
      ans.min = std::min(ans.min, data[i]);
    }
  };
  quiet_uadd(ans, data);
  ASSERT_EQ(ans.max, res->max);
  ASSERT_EQ(ans.min, res->min);
  ASSERT_EQ(ans.sum, res->sum);

  free(data);
  Runtime::destory();
  SIM::FTL::destory();
}