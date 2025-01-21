#include <cstdlib>
#include <ctime>

#include "e2fs_utils.hh"

#include "sims/ftl.hh"

#include "runtime.hh"
#include "types.hh"

#include "fs/ext4/ext4.hh"
#include "slet/randread.hh"

static ext2_filsys fs;
static io_manager manager = unix_io_manager;

using namespace SimpleSSD::ISC;
using namespace SimpleSSD::ISC::SIM;

// static class member (nextSletId) won't be reset within same test file
static ISC_STS_SLET_ID cnt = 0;

#define PR_SECTION RandReadAPP
TEST(SletTest, PR_SECTION) {
  /* ------------------------------------------------------------------------ */
  /*                            initialize workload                           */
  /* ------------------------------------------------------------------------ */
  auto work = (RandReadAPP::Work *)malloc(sizeof(RandReadAPP::Work));
  *work = {
      .szFile = 1 << 30,
      .szEachRead = 128,
      .szTotal = 1024 << 20,
      .numIO = 0, /* make compiler quiet */
  };
  ASSERT_GE(work->szFile, work->szEachRead);
  ASSERT_GE(work->szTotal, work->szEachRead);
  work->numIO = work->szTotal / work->szEachRead;

  // generate random offsets and set as option
  srand(time(0));
  size_t *offsets = (size_t *)calloc(sizeof(size_t), work->numIO);
  for (size_t i = 0; i < work->numIO; ++i)
    offsets[i] = rand() % (work->szFile - work->szEachRead);

  /* ------------------------------------------------------------------------ */
  /*                        prepare disk and test data                        */
  /* ------------------------------------------------------------------------ */
  initialize_ext2_error_table();

  auto disk = "/tmp/sss-isc-test.img";
  auto conf = "scripts/mke2fs.conf";
  mkext4(disk, conf, std::max(4UL << 20, 2 * work->szFile));

  auto path = "/largefile", name = path + 1;
  auto flen = work->szFile;
  auto fdata = (char *)calloc(1, flen);
  ASSERT_NE(fdata, nullptr);

  // generate random data
  int fd = open("/dev/urandom", O_RDONLY, 0);
  ASSERT_GT(fd, 0);
  int rd = read(fd, fdata, flen);
  ASSERT_EQ(rd, (int)flen);
  close(fd);
  // for (size_t i = 0; i < flen; ++i)
  //   fdata[i] = 'a' + i % 26;
  // fdata[flen - 1] = 0;

  // write data to disk
  check0(ext2fs_open, disk, EXT2_FLAG_RW, 0, 0, manager, &fs);
  check0(ext2fs_read_block_bitmap, fs);
  check0(ext2fs_read_inode_bitmap, fs);
  newfile(fs, name, fdata, flen, 2);
  check0(ext2fs_flush, fs);

  /* ------------------------------------------------------------------------ */
  /*                              main test codes                             */
  /* ------------------------------------------------------------------------ */

  // setup env for verification
  SIM::FTL::setImage(disk);
  ASSERT_EQ(Runtime::addSlet<Ext4>(), 1);
  auto id = Runtime::addSlet<PR_SECTION>();
  ASSERT_EQ(id, ++cnt);

  // run and verify
  ASSERT_EQ(Runtime::setOpt(id, PR_SECTION::keyConf, work), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, PR_SECTION::keyOffsets, offsets), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, PR_SECTION::keyPath, strdup(path)), ISC_STS_OK);
  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_OK);

  auto result = (char *)Runtime::getOpt(id, PR_SECTION::keyResult);
  ASSERT_NE(result, nullptr);

  // prepare answer
  auto ans = (char *)malloc(work->szTotal);
  ASSERT_NE(ans, nullptr);
  for (size_t i = 0, ofs = 0; i < work->numIO; ++i, ofs += work->szEachRead)
    memcpy(&ans[ofs], &fdata[offsets[i]], work->szEachRead);

  // fast compare
  if (memcmp(result, ans, work->szTotal)) {
    // find different char
    printf("Wrong answer, find location...\n");
    for (size_t i = 0; i < work->numIO; ++i) {
      auto ofs = i * work->szEachRead;
      if (!memcmp(&result[ofs], &ans[ofs], work->szEachRead))
        continue;
      for (size_t j = 0; j < work->szEachRead; ++j)
        ASSERT_EQ((int)result[ofs + j], (int)ans[ofs + j])
            << printf("at char[%lu+%lu]", ofs, j)
            << printf("(offsets[%lu]=%lu)\n", i, offsets[i]);
    }
  }

  free(ans);
  free(fdata);
  Runtime::destory();
  SIM::FTL::destory();
}