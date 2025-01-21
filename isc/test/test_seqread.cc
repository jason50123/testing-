#include "e2fs_utils.hh"

#include "sims/ftl.hh"

#include "runtime.hh"
#include "types.hh"

#include "fs/ext4/ext4.hh"
#include "slet/seqread.hh"

using namespace SimpleSSD::ISC;
using namespace SimpleSSD::ISC::SIM;

// static class member (nextSletId) won't be reset within same test file
static ISC_STS_SLET_ID cnt = 0;

static ext2_filsys fs;
static io_manager manager = unix_io_manager;

#define PR_SECTION SeqReadAPP
TEST(SletTest, PR_SECTION) {
  initialize_ext2_error_table();

  /// prepare disk and test data

  auto disk = "/tmp/" str(PR_SECTION) ".img";
  auto conf = "scripts/mke2fs.conf";
  mkext4(disk, conf, (2UL << 30));

  auto path = "/largefile", name = path + 1;
  auto dlen = (1 << 30);
  auto data = (char *)calloc(1, dlen);
  ASSERT_NE(data, nullptr);

  // generate random data
  int fd = open("/dev/urandom", O_RDONLY, 0);
  ASSERT_GT(fd, 0);
  int rd = read(fd, data, dlen);
  ASSERT_EQ(rd, dlen);
  // sprintf(data, "this is samlpe data string");
  close(fd);

  // write data to disk
  check0(ext2fs_open, disk, EXT2_FLAG_RW, 0, 0, manager, &fs);
  check0(ext2fs_read_block_bitmap, fs);
  check0(ext2fs_read_inode_bitmap, fs);
  newfile(fs, name, data, dlen, 2);
  check0(ext2fs_flush, fs);

  /// main test codes

  // setup env for verification
  SIM::FTL::setImage(disk);
  ASSERT_EQ(Runtime::addSlet<Ext4>(), 1);
  auto id = Runtime::addSlet<PR_SECTION>();
  ASSERT_EQ(id, ++cnt);

  // run and verify
  ASSERT_EQ(Runtime::setOpt(id, SeqReadAPP::keyPath, strdup(path)), ISC_STS_OK);
  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);

  auto result = (char *)Runtime::getOpt(id, SeqReadAPP::keyResult);
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(memcmp(result, data, dlen), 0);

  free(data);
  Runtime::destory();
  SIM::FTL::destory();
}