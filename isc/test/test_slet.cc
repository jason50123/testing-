#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "gtest/gtest.h"

#include "runtime.hh"
#include "types.hh"

using namespace SimpleSSD::ISC;

class GrepSingleFile : public GenericAPP {
 public:
  static const char *keyPath;
  static const char *keyPatt;

  GrepSingleFile() { this->opt.name = strdup(str(GrepSingleFile)); }

  ISC_STS builtin_startup(_SIM_PARAMS) override {
    ISC_STS res = ISC_STS_FAIL;

    EXPECT_NE(GrepSingleFile::keyPath, nullptr);
    EXPECT_NE(GrepSingleFile::keyPatt, nullptr);

    auto path = (char *)this->getOpt(GrepSingleFile::keyPath);
    auto pattern = (char *)this->getOpt(GrepSingleFile::keyPatt);
    EXPECT_NE(path, nullptr);
    EXPECT_NE(pattern, nullptr);

    int fd = open(path, O_RDONLY, 0);
    EXPECT_GE(fd, 0);
    struct stat st;
    EXPECT_EQ(fstat(fd, &st), 0);

    char *buffer = (char *)calloc(1, st.st_size + 1);
    EXPECT_NE(buffer, nullptr);
    EXPECT_EQ(read(fd, buffer, st.st_size), st.st_size);
    close(fd);

    char *at = strstr(buffer, pattern);
    if (at)
      res = at - buffer;

    free(buffer);
    return res;
  }
};
template ISC_STS_SLET_ID Runtime::addSlet<GrepSingleFile>();
const char *GrepSingleFile::keyPath = "path";
const char *GrepSingleFile::keyPatt = "pattern";

#define PR_SECTION GrepTest
TEST(SletTest, PR_SECTION) {
  auto id = Runtime::addSlet<GrepSingleFile>();
  ASSERT_EQ(id, 1);

  // create test data
  const char *pathTestFile = "build/sample-data-" str(PR_SECTION);
  const char *dataTestFile = "this file contains pattern 1";
  const size_t dlen = strlen(dataTestFile);

  int fd = open(pathTestFile, O_CREAT | O_WRONLY, S_IRWXU | S_IRWXG);
  ASSERT_GE(fd, 0);
  ASSERT_EQ((size_t)write(fd, dataTestFile, dlen), dlen);
  close(fd);

  // setup input data
  char *path = strdup(pathTestFile);
  ASSERT_NE(path, nullptr);
  char *pat = strdup("pattern 1");
  ASSERT_NE(pat, nullptr);

  ASSERT_NE(GrepSingleFile::keyPath, nullptr);
  ASSERT_NE(GrepSingleFile::keyPatt, nullptr);
  ASSERT_EQ(Runtime::setOpt(id, GrepSingleFile::keyPath, path), ISC_STS_OK);
  ASSERT_EQ(Runtime::setOpt(id, GrepSingleFile::keyPatt, pat), ISC_STS_OK);

  // run slet
  auto ans = strstr(dataTestFile, pat) - dataTestFile;
  ASSERT_EQ(Runtime::startSlet(id), ans);

  // setup another pattern
  pat = strdup("patten 2");
  ASSERT_NE(pat, nullptr);
  ASSERT_EQ(Runtime::setOpt(id, GrepSingleFile::keyPatt, pat), ISC_STS_OK);

  // run slet
  ASSERT_EQ(Runtime::startSlet(id), ISC_STS_FAIL);

  Runtime::destory();
}