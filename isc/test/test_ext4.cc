#include "gtest/gtest.h"

// ISC headers
#include "fs/ext4/ext4.hh"
#include "sims/ftl.hh"
#include "utils/math.hh"

// debugfs
#include <queue>

#include "e2fs_utils.hh"

using namespace SimpleSSD::Utils;
using namespace SimpleSSD::ISC;
using namespace SimpleSSD::ISC::SIM;

static const char *DISKSET[] = {
    "test/bin/ext4.img",
    "test/bin/multi-extents.img",
    "test/bin/ext4-bigF.img",
    // "test/bin/fb-test.img", // htree enabled
    "test/bin/fb-rand-access.img",
    "test/bin/frag-test.img",
    "test/bin/fb-mkdirs.img",
};

static blk64_t sblk = 0;
static blk64_t bsz = 0;
static int flags = 0; /* EXT2_FLAG_* */
static io_manager manager = unix_io_manager;

static ext2_filsys fs;
static ext2_super_block *sb;

#undef NO_LOG_ID_CHECK
#define NO_LOG_ID_CHECK 1

#undef PR_SECTION
#define PR_SECTION SbTest
TEST(Ext4Test, PR_SECTION) {
  size_t iDisk = 0;
  for (auto disk : DISKSET) {
    pr("-------Testing disk[%lu]: %s", iDisk++, disk);
    // setup FSA
    FTL::setImage(disk);
    auto fsa = new Ext4();
    auto fsa_sb = fsa->sb;

    // setup debugfs
    check0(ext2fs_open, disk, flags, sblk, bsz, manager, &fs);
    sb = fs->super;

    // verify Ext4::getSuper()
    ASSERT_TRUE(!memcmp(sb, fsa_sb, sizeof(Ext4::super_block)));
    ASSERT_EQ(sb->s_magic, EXT2_SUPER_MAGIC);

    // inode, block summary
    auto firstBlk = sb->s_first_data_block;
    auto nrBlks = CAT3232U(sb->s_blocks_count_hi, sb->s_blocks_count);
    auto nrGrps = DIV64_CEIL(nrBlks - firstBlk, sb->s_blocks_per_group);

    auto szBlk = EXT2_BLOCK_SIZE(sb);
    auto szBG = sb->s_blocks_per_group * szBlk;
    auto nrBG = fs->group_desc_count;

    if (sb->s_inode_size != sizeof(ext2_inode))
      pr("WARNING!! inode size is :%u", sb->s_inode_size);
    ASSERT_LE(sb->s_free_inodes_count, sb->s_inodes_count);
    ASSERT_LE(sb->s_free_blocks_count, nrBlks);
    ASSERT_LE(nrBG * sizeof(ext4_group_desc), (size_t)szBlk);

    // dump FS info
    pr("UUID: %s", e2p_uuid2str(sb->s_uuid));
    pr("Inodes: %u/%u free", sb->s_free_inodes_count, sb->s_inodes_count);
    pr("Inode Size: %u", sb->s_inode_size);
    pr("Blocks: %u/%u free", sb->s_free_blocks_count, sb->s_blocks_count);
    pr("Block Size: %d", szBlk);
    pr("Block Group Size: %u", szBG);
    pr("Block Groups: %lu", nrGrps);
    pr("Flex Block Group Size: %u", 1 << sb->s_log_groups_per_flex);

    // add tests before this line
    sb = nullptr;
    ext2fs_close(fs);

    delete fsa;
    FTL::destory();
  }
}

#undef PR_SECTION
#define PR_SECTION GdTest
TEST(Ext4Test, PR_SECTION) {
  size_t iDisk = 0;
  for (auto disk : DISKSET) {
    pr("-------Testing disk[%lu]: %s", iDisk++, disk);
    // setup FSA
    FTL::setImage(disk);
    auto fsa = new Ext4();
    check0(ext2fs_open, disk, flags, sblk, bsz, manager, &fs);
    sb = fs->super;

    auto firstBlk = sb->s_first_data_block;
    auto nrBlks = CAT3232U(sb->s_blocks_count_hi, sb->s_blocks_count);
    auto nrGrps = DIV64_CEIL(nrBlks - firstBlk, sb->s_blocks_per_group);

    pr("Reserved GDT blocks: %u", sb->s_reserved_gdt_blocks);
    pr("Blocks per Group: %u", sb->s_blocks_per_group);
    pr("Inodes per Group: %u", sb->s_inodes_per_group);
    pr("Inode Blocks per Group: %u", fs->inode_blocks_per_group);

    for (dgrp_t i = 0; i < nrGrps; ++i) {
      auto gd = (ext4_group_desc *)ext2fs_group_desc(fs, fs->group_desc, i);
      ASSERT_TRUE(gd);

      auto fsa_gd = fsa->getGrpDesc(i);
      ASSERT_TRUE(fsa_gd);

      char desc[30] = {0};
      sprintf(desc, "Grp[%u] descriptor:", i);

      ASSERT_TRUE(!memcmp(gd, fsa_gd, sizeof(Ext4::group_desc)))
          << (pipe2xxd("Expect:", gd, sizeof(Ext4::group_desc), NULL), "")
          << (pipe2xxd("Got:", fsa_gd, sizeof(Ext4::group_desc), NULL), "");
      free(fsa_gd);

      auto bBMap = CAT3232U(gd->bg_block_bitmap_hi, gd->bg_block_bitmap);
      auto bIMap = CAT3232U(gd->bg_inode_bitmap_hi, gd->bg_inode_bitmap);
      auto bITab = CAT3232U(gd->bg_inode_table_hi, gd->bg_inode_table);
      auto blksIMap = DIV64_CEIL(sb->s_inodes_per_group, EXT2_BLOCK_SIZE(sb));
      auto blksITab = DIV64_CEIL(sb->s_inodes_per_group * sb->s_inode_size,
                                 EXT2_BLOCK_SIZE(sb));

      pr("bMap: Block[%lu](+%lu)", bBMap, blksIMap);
      pr("iMap: Block[%lu](+%lu)", bIMap, blksIMap);
      pr("iTab: Block[%lu](+%lu)", bITab, blksITab);

      ASSERT_EQ(bBMap, ext2fs_block_bitmap_loc(fs, i));
      ASSERT_EQ(bIMap, ext2fs_inode_bitmap_loc(fs, i));
      ASSERT_EQ(bITab, ext2fs_inode_table_loc(fs, i));
    }

    // add tests before this line
    sb = nullptr;
    ext2fs_close(fs);
    delete fsa;
    FTL::destory();
  }
}

#undef PR_SECTION
#define PR_SECTION IMapTest
TEST(Ext4Test, PR_SECTION) {
  size_t iDisk = 0;
  for (auto disk : DISKSET) {
    pr("-------Testing disk[%lu]: %s", iDisk++, disk);
    check0(ext2fs_open, disk, flags, sblk, bsz, manager, &fs);
    sb = fs->super;

    FTL::setImage(disk);
    auto fsa = new Ext4();

    ASSERT_EQ(sb->s_inode_size, fsa->sb->s_inode_size);

    fs->inode_map = nullptr;
    check0(ext2fs_read_inode_bitmap, fs);
    ASSERT_TRUE(fs->inode_map);

    size_t nrUsed = sb->s_inodes_count - sb->s_free_inodes_count;
    size_t nrFound = 0;
    pr("Expect %lu Inodes used:", nrUsed);

    ext2_ino_t s = 1;
    const ext2_ino_t e = sb->s_inodes_count;
    for (ext2_ino_t ffs; s < e && nrFound < nrUsed; s = ++ffs) {
      ext2_inode inode;
      check0(ext2fs_find_first_set_inode_bitmap2, fs->inode_map, s, e, &ffs);
      check0(ext2fs_read_inode, fs, ffs, &inode);
      pipe2xxd("e2fs inode:", &inode, sizeof(ext2_inode), NULL);

      // get FSA imap and test target bit
      bitmap_t fsa_imap;
      ASSERT_TRUE(fsa_imap = fsa->getInoMap(ffs));
      ASSERT_TRUE(fsa->testIMap(fsa_imap, ffs, sb->s_inodes_per_group));

      // get and compare inode data
      Ext4::inode *fsa_inode;
      ASSERT_TRUE(fsa_inode = fsa->getInode(ffs));
      // note: use sb.s_inode_size instead of sizeof(inode)
      ASSERT_FALSE(memcmp(fsa_inode, &inode, sizeof(ext2_inode)));

      // parse mode (QBCDRLS UGS USR_RWX GRP_RWX OTH_RWX)
      const char *type;
      if (LINUX_S_ISDIR(inode.i_mode))
        type = "D";
      else if (LINUX_S_ISREG(inode.i_mode))
        type = "R";
      else
        type = "?";

      pr("[%lu]: %u (type: %s)", ++nrFound, ffs, type);
      pr("------------------------------------------");
      check1(ext2fs_test_inode_bitmap, fs->inode_map, ffs);

      free(fsa_imap);
      free(fsa_inode);
    }
    ASSERT_EQ(nrUsed, nrFound);

    // ext2fs_test_inode_bitmap_range() returns true if all bits in range are 0
    if (s < e) {
      pr("Checking Inodes[%u, %u] are all clear", s, e - 1);
      check1(ext2fs_test_inode_bitmap_range, fs->inode_map, s, e - s - 1);

      // todo: test remaining bits
    }

    delete fsa;
    FTL::destory();
    ext2fs_close(fs);
  }
}

#undef PR_SECTION
#define PR_SECTION ExTreeTest
TEST(Ext4Test, PR_SECTION) {
  size_t iDisk = 0;
  for (auto disk : DISKSET) {
    pr("-------Testing disk[%lu]: %s", iDisk++, disk);

    check0(ext2fs_open, disk, flags, sblk, bsz, manager, &fs);
    sb = fs->super;
    FTL::setImage(disk);
    auto fsa = new Ext4();

    check0(ext2fs_read_inode_bitmap, fs);

    auto e = sb->s_inodes_count;
    auto nr = e - sb->s_free_inodes_count;

    // traverse inodes
    size_t cnt = 0, cntall = 0;
    for (ext2_ino_t s = 1, ffs; s < e && cnt < nr; s = ++ffs, ++cnt) {
      check0(ext2fs_find_first_set_inode_bitmap2, fs->inode_map, s, e, &ffs);

      ext2_inode inode;
      check0(ext2fs_read_inode, fs, ffs, &inode);

      cntall++;
      if (ffs != 2 && ffs < 12 /* min non-reserved ino */)
        continue;

      ASSERT_TRUE(inode.i_flags | EXT4_EXTENTS_FL);

      const char *type;
      if (LINUX_S_ISDIR(inode.i_mode))
        type = "Directory";
      else if (LINUX_S_ISREG(inode.i_mode))
        type = "Regular";
      else
        type = "???";
      pr("Ino[%u] is %s file:", ffs, type);

      // parse the extent tree
      // https://www.kernel.org/doc/html/latest/filesystems/ext4/dynamic.html#extent-tree
      size_t sz;
      auto exts = fsa->getExtent(ffs, &sz, nullptr);

      // compare with libext2fs's result
      struct ee_pack {
        Ext4::Ext *list;
        size_t sz;
      } pack = {.list = exts, .sz = sz};

      ext2fs_block_iterate(
          fs, ffs, 0, nullptr,
          [](ext2_filsys, blk_t *num, int ib, void *elist) -> int {
            // less than zero: called on a metadata block
            if (ib < 0)
              return 0;

            // non-negative: the logical block number of a data block
            // check whether this node is recorded in the extent list
            // pr("\tnum,ib=%d,%d", *num, ib);
            ee_pack *ee = (ee_pack *)elist;
            Ext4::Ext e{.len = 0};
            size_t fsa_ib = 0;
            for (size_t i = 0; i < ee->sz; ++i) {
              auto beg = ee->list[i].block;
              auto end = beg + ee->list[i].len;
              if (beg <= (size_t)ib && (size_t)ib < end) {
                e = ee->list[i];
                fsa_ib += *num - ee->list[i].slbn;
                break;
              }
              fsa_ib += ee->list[i].len;
            }
            EXPECT_TRUE(e.len)
                << std::printf("blk %d not found in any extent list\n", *num);
            EXPECT_LE(e.slbn, (size_t)*num);
            EXPECT_LT((size_t)*num, e.slbn + e.len);
            EXPECT_EQ(fsa_ib, (size_t)ib) << "block index not match";
            return 0;
          },
          &pack);
      free(exts);
    }
    ASSERT_EQ(cnt, nr) << std::printf("e, cntall= %u,%lu\n", e, cntall);

    ext2fs_close(fs);
    delete fsa;
    FTL::destory();
  }
}

#undef PR_SECTION
#define PR_SECTION DirTest
TEST(Ext4Test, PR_SECTION) {
  size_t iDisk = 0;
  for (auto disk : DISKSET) {
    pr("-------Testing disk[%lu]: %s", iDisk++, disk);

    FTL::setImage(disk);
    auto fsa = new Ext4();
    ASSERT_TRUE(fsa);

    check0(ext2fs_open, disk, flags, sblk, bsz, manager, &fs);
    sb = fs->super;

    // traverse directory (BFS)
    struct Data {
      std::map<ino_t, char *> garbage;
      std::queue<pair<ino_t, const char *>> pending;
      const char *cwd;
      Ext4 *fsa;
      int count;
    } data = {
        .fsa = fsa,
        .count = 0,
    };
    data.pending.push({2, ""});

    while (!data.pending.empty()) {
      auto dir = data.pending.front();
      data.pending.pop();

      data.cwd = dir.second;
      pr("Traversing dir[%s] (%lu)", dir.second, dir.first);
      check0(
          ext2fs_dir_iterate, fs, dir.first, 0, nullptr,
          [](ext2_dir_entry *e, int ofs, int bsz, char *, void *data) {
            auto d = (Data *)data;

            if (!strcmp(e->name, ".") || !strcmp(e->name, ".."))
              return 0;

            if (((Ext4::fake_dirent *)e)->file_type != 2 /* dir */)
              return 0;

            pr("-------------------------------------------------");
            pr("Dir['%.*s'] -> Ino[%u]", e->name_len & 0xff, e->name, e->inode);
            pr("\tname/rec length: %u/%u", e->name_len & 0xff, e->rec_len);
            pr("\tofs,bsz=%d,%d", ofs, bsz);

            char *path;
            asprintf(&path, "%s/%.*s", d->cwd, e->name_len & 0xff, e->name);

            auto it = d->garbage.find(e->inode);
            if (it != d->garbage.end())
              pr("Inode[%lu] DUP: '%s' vs '%s", (*it).first, (*it).second,
                 path);
            else
              d->garbage.insert({e->inode, path});

            d->pending.push({e->inode, path});
            d->count++;

            auto fsa = d->fsa;
            auto fsa_ino = fsa->namei(path);
            EXPECT_EQ(fsa_ino, e->inode);

            // pipe2xxd("DirEnt buffer data", buf, 128, NULL);
            return 0;
          },
          &data);
    }
    pr("Total number of directories: %lu", data.garbage.size());

    for (auto &dir : data.garbage)
      free(dir.second);

    ext2fs_close(fs);
    delete fsa;
    FTL::destory();
  }
}

#undef PR_SECTION
#define PR_SECTION NameiTest
TEST(Ext4Test, PR_SECTION) {
  size_t iDisk = 0;
  for (auto disk : DISKSET) {
    pr("-------Testing disk[%lu]: %s", iDisk++, disk);

    check0(ext2fs_open, disk, flags, sblk, bsz, manager, &fs);
    FTL::setImage(disk);

    // traverse directory (BFS)
    struct Data {
      std::map<ino_t, char *> garbage;
      std::queue<pair<ino_t, const char *>> pending;
      const char *cwd;
      Ext4 *fsa;
      int nrFiles;
    } data = {
        .fsa = new Ext4(),
        .nrFiles = 0,
    };
    data.pending.push({2, ""});

    while (!data.pending.empty()) {
      auto dir = data.pending.front();
      data.pending.pop();

      data.cwd = dir.second;
      pr("Traversing dir[%s] (%lu)", dir.second, dir.first);
      check0(
          ext2fs_dir_iterate, fs, dir.first, 0, nullptr,
          [](ext2_dir_entry *e, int, int, char *, void *data) {
            auto d = (Data *)data;

            if (!strcmp(e->name, ".") || !strcmp(e->name, ".."))
              return 0;

            char *path;
            auto ftype = ((Ext4::fake_dirent *)e)->file_type;
            if (ftype != 1 /* reg file */ && ftype != 2 /* dir */)
              return 0;

            pr("-------------------------------------------------");
            if (ftype == 1) {
              d->nrFiles++;
              asprintf(&path, "%s/%.*s", d->cwd, e->name_len & 0xff, e->name);
              pr("Regular file found: %s", path);
              auto fsa_ino = d->fsa->namei(path);
              free(path);
              EXPECT_EQ(e->inode, fsa_ino);
              return 0;
            }

            asprintf(&path, "%s/%.*s", d->cwd, e->name_len & 0xff, e->name);

            d->garbage.insert({e->inode, path});
            d->pending.push({e->inode, path});
            return 0;
          },
          &data);
    }
    pr("Total number of files: %d", data.nrFiles);
    pr("Total number of directories: %lu", data.garbage.size());

    for (auto &dir : data.garbage)
      free(dir.second);

    ext2fs_close(fs);
    delete data.fsa;
    FTL::destory();
  }
}
