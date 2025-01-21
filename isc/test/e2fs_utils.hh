#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef NO_GTEST
#include "gtest/gtest.h"
#else
#include <assert.h>
#define ASSERT_EQ(a, b) (assert((a) == (b)))
#define EXPECT_EQ(a, b) (assert((a) == (b)))
#define ASSERT_NE(a, b) (assert((a) != (b)))
#define EXPECT_NE(a, b) (assert((a) != (b)))
#endif

#include <ext2fs/ext2fs.h>
#include "debugfs/e2p_uuid.hh"
#include "utils/debug.hh"

// gtest wrappers for debugfs APIs
#define check0(func, ...)                                                      \
  do {                                                                         \
    auto func##_err = func(__VA_ARGS__);                                       \
    if (func##_err) {                                                          \
      com_err(#func, func##_err, nullptr);                                     \
      EXPECT_EQ(func##_err, 0);                                                \
    }                                                                          \
  } while (0)

#define check1(func, ...)                                                      \
  do {                                                                         \
    auto func##_err = func(__VA_ARGS__);                                       \
    if (!func##_err) {                                                         \
      com_err(#func, func##_err, nullptr);                                     \
      ASSERT_NE(func##_err, 0);                                                \
    }                                                                          \
  } while (0)

#undef NO_LOG_ID_CHECK
#define NO_LOG_ID_CHECK 1

static inline void mkext4(const char *disk, const char *conf, size_t bytes) {
#undef PR_SECTION
#define PR_SECTION mkext4
  int fd;
  if (!(fd = open(disk, O_CREAT | O_RDWR, 0666))) {
    perr("failed to open target disk '%s':", disk);
    exit(__LINE__);
  }
  if (ftruncate(fd, bytes) == -1) {
    perr("failed to ftruncate:");
    close(fd);
    exit(__LINE__);
  }
  close(fd);

  if (conf) {
    if (!(fd = open(conf, O_RDONLY, 0))) {
      perr("config file '%s' not found", conf);
      exit(__LINE__);
    }
    close(fd);
  }

  // make filesystem
  pid_t pid = fork();
  if (pid < 0) {
    perr("failed to fork");
    exit(__LINE__);
  }

  if (pid == 0) {
    if (conf && setenv("MKE2FS_CONFIG", conf, 1) == -1) {
      perr("failed to set MKE2FS_CONFIG");
      exit(__LINE__);
    }
    execl("/usr/sbin/mkfs.ext4", "mkfs.ext4", "-F", disk, nullptr);
    exit(!!"not expected to be here");
  }

  int status;
  if (waitpid(pid, &status, 0) == -1) {
    perr("waitpid failed");
    exit(__LINE__);
  }
  if (WIFEXITED(status)) {
    pr("mkext4 done");
    return;
  }
  pr("mkext4 exit with: %d", WEXITSTATUS(status));
  exit(__LINE__);
}

static inline void newfile(ext2_filsys fs, const char *name, const char *data,
                           size_t dlen, ext2_ino_t dir) {
#undef PR_SECTION
#define PR_SECTION newfile
  const auto BLOCKSIZE = fs->blocksize;

  // create file
  ext2_ino_t newi;
  struct ext2_inode inode;
  check0(ext2fs_new_inode, fs, dir, 010644, nullptr, &newi);
  pr("got free inode: %d", newi);
  check0(ext2fs_link, fs, dir, name, newi, EXT2_FT_REG_FILE);

  check0(ext2fs_test_inode_bitmap2, fs->inode_map, newi);
  ext2fs_inode_alloc_stats2(fs, newi, 1, 0);
  memset(&inode, 0, sizeof(inode));
  inode.i_mode = LINUX_S_IFREG | 0644;
  inode.i_size = dlen;
  inode.i_links_count = 1;
  inode.i_blocks = (dlen + BLOCKSIZE - 1) / BLOCKSIZE;
  inode.i_flags |= EXT4_EXTENTS_FL;
  check0(ext2fs_write_new_inode, fs, newi, &inode);
  pr("write new inode for '%s'", name);

  // write data
  ext2_file_t file;
  uint32_t written;
  check0(ext2fs_file_open, fs, newi, EXT2_FILE_WRITE, &file);
  check0(ext2fs_file_write, file, data, dlen, &written);
  ASSERT_EQ(written, dlen);
  check0(ext2fs_file_flush, file);
  check0(ext2fs_file_close, file);
  pr("data written");

  // verify
  uint32_t got;
  char *buf = new char[dlen];
  check0(ext2fs_file_open, fs, newi, 0, &file);
  check0(ext2fs_file_read, file, buf, dlen, &got);
  ASSERT_EQ(dlen, got);
  ASSERT_EQ(memcmp(data, buf, dlen), 0);
  check0(ext2fs_file_close, file);
  delete[] buf;
}

static inline void newdir(ext2_filsys fs, ext2_ino_t dir, const char *name,
                          ext2_ino_t *newi) {
#undef PR_SECTION
#define PR_SECTION newdir
  ASSERT_NE(newi, nullptr);
retry:
  int err = ext2fs_mkdir(fs, dir, 0, name);
  if (err == EXT2_ET_DIR_NO_SPACE) {
    check0(ext2fs_expand_dir, fs, dir);
    goto retry;
  }
  else if (err) {
    com_err("ext2fs_mkdir:", err, nullptr);
    exit(-1);
  }

  check0(ext2fs_namei, fs, 2, dir, name, newi);
  pr("New Directory '%s' (ino=%u) Created", name, *newi);
}

static inline void simple_newfile(const char *file, const void *data,
                                  ssize_t dlen, ssize_t diskSize,
                                  const char *disk = "/tmp/sss-isc-test.img",
                                  const char *conf = "scripts/mke2fs.conf") {
  initialize_ext2_error_table();
  mkext4(disk, conf, diskSize);

  ext2_filsys fs;
  io_manager manager = unix_io_manager;
  check0(ext2fs_open, disk, EXT2_FLAG_RW, 0, 0, manager, &fs);
  check0(ext2fs_read_block_bitmap, fs);
  check0(ext2fs_read_inode_bitmap, fs);
  newfile(fs, file + 1, (char *)data, dlen, 2);
  check0(ext2fs_flush, fs);
  ext2fs_free(fs);
}

static inline void *simple_random_newfile(
    const char *file, ssize_t dlen, ssize_t diskSize,
    const char *disk = "/tmp/sss-isc-test.img",
    const char *conf = "scripts/mke2fs.conf") {
  void *data = malloc(dlen);
  int fd = open("/dev/urandom", O_RDONLY);
  EXPECT_GE(fd, 0);
  EXPECT_EQ(dlen, read(fd, data, dlen));
  close(fd);

  simple_newfile(file, data, dlen, diskSize, disk, conf);
  return data;
}

#undef PR_SECTION