#include "sims/ftl.hh"

// make sims before utils/debug.hh
#include "ext4.hh"
#include "runtime.hh"
#include "utils/math.hh"

using namespace SimpleSSD::ISC::SIM;

#define PR_SECTION LOG_ISC_EXT4

namespace SimpleSSD {
namespace ISC {

template ISC_STS_SLET_ID Runtime::addSlet<Ext4>(_SIM_PARAMS);

using SIM::DRAM;  // shadow SimpleSSD::DRAM
using SIM::FTL;   // shadow SimpleSSD::FTL

struct GrpNode {
  bool valid;
  Ext4::group_desc data;
};

struct ICacheNode {
  ino_t number;
  void *inode;

  static constexpr size_t szKey = sizeof(number);
  static size_t szVal;
};
size_t ICacheNode::szVal;

struct NameiNode {
  ino_t diri;      // key1
  char name[256];  // key2
  ino_t inode;

  NameiNode(ino_t di, const char *n, size_t nl, ino_t ino)
      : diri(di), inode(ino) {
    memset(name, 0, szKey2);
    strncpy(name, n, nl);
  }

  static constexpr size_t szKey1 = sizeof(diri);
  static constexpr size_t szKey2 = sizeof(name);
  static constexpr size_t szVal = sizeof(inode);
  static constexpr size_t size() { return szKey1 + szKey2 + szVal; }
};

Ext4::Ext4(_SIM_PARAMS) {
  this->setOpt("name", strdup("Ext4"));
  this->setOpt("cwd", strdup("/"));

  this->sb = this->getSuper(_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__INIT);

  assert(this->sb);

  // allocate inode cache
  ICacheNode::szVal =
      std::max((size_t)this->sb->s_inode_size, sizeof(Ext4::inode));

  this->lruInoCache = DRAM::alloc(
      256, ICacheNode::szKey + ICacheNode::szVal, DRAM::TYPES::LRU_CACHE,
      [](const void *a, const void *b, size_t) -> int {
        auto aIno = ((ICacheNode *)a)->number;
        auto bIno = ((ICacheNode *)b)->number;
        return aIno == bIno ? 0 : (aIno < bIno ? -1 : 1);
      },
      [](void *mem, const void *buf, size_t) -> void * {
        auto b = (const ICacheNode *)buf;
        memcpy(mem, &b->number, ICacheNode::szKey);
        memcpy((char *)mem + ICacheNode::szKey, b->inode, ICacheNode::szVal);
        return mem;
      },
      [](void *buf, const void *mem, size_t) -> void * {
        auto b = (ICacheNode *)buf;
        memcpy(&b->number, mem, ICacheNode::szKey);
        memcpy(b->inode, (char *)mem + ICacheNode::szKey, ICacheNode::szVal);
        return buf;
      });

  // allocate namei cache
  this->NameiCache = DRAM::alloc(
      10000, NameiNode::size(), DRAM::TYPES::LRU_CACHE,
      [](const void *a, const void *b, size_t) -> int {
        auto akey1 = ((NameiNode *)a)->diri;
        auto bkey1 = ((NameiNode *)b)->diri;
        if (akey1 != bkey1)
          return akey1 < bkey1 ? -1 : 1;

        auto akey2 = ((NameiNode *)a)->name;
        auto bkey2 = ((NameiNode *)b)->name;
        return strncmp(akey2, bkey2, 255);
      },
      [](void *mem, const void *buf, size_t) -> void * {  // in (write)
        auto m = (NameiNode *)mem;
        auto b = (const NameiNode *)buf;
        m->diri = b->diri;
        memcpy(m->name, b->name, NameiNode::szKey2);
        m->inode = b->inode;
        return mem;
      },
      [](void *buf, const void *mem, size_t) -> void * {  // out (read)
        auto m = (NameiNode *)mem;
        auto b = (NameiNode *)buf;
        b->diri = m->diri;
        memcpy(b->name, m->name, NameiNode::szKey2);
        b->inode = m->inode;
        return buf;
      });

  // calculate some important info
  auto sb = this->sb;
  auto iBlk0 = sb->s_first_data_block;

  this->nrBlks = CAT3232U(sb->s_blocks_count_hi, sb->s_blocks_count_lo);
  this->szBlk = EXT4_MIN_BLOCK_SIZE << sb->s_log_block_size;
  this->nrGrps = DIV64_CEIL(this->nrBlks - iBlk0, sb->s_blocks_per_group);
  this->nrGrpsPerFlex = 1 << sb->s_log_groups_per_flex;

  if (this->szBlk != Ext4::BLK_SIZE) {
    panic("Unsupported block size: %lu", this->szBlk);
    assert(0);
  }

  pr("Ext4 Info:");
  pr("\tBlock Size: %lu", this->szBlk);
  pr("\tInodes: %u/%u free", sb->s_free_inodes_count, sb->s_inodes_count);
  pr("\tBlock Groups: %lu", this->nrGrps);
  pr("\tBlock Groups per Flex: %lu", this->nrGrpsPerFlex);

  // dump group bitmaps, iTable location
  this->blksBMap = DIV64_CEIL(sb->s_blocks_per_group, this->szBlk);
  this->blksIMap = DIV64_CEIL(sb->s_inodes_per_group, this->szBlk);
  this->szITab = sb->s_inodes_per_group * sizeof(inode);
  this->blksITab = DIV64_CEIL(this->szITab, this->szBlk);

  this->grpCache = DRAM::alloc(this->nrGrps, sizeof(GrpNode));
  for (size_t i = 0; i < this->nrGrps; ++i) {
    auto gd = getGrpDesc(i _add_sim_params);

    auto locBMap = CAT3232U(gd->bg_block_bitmap_hi, gd->bg_block_bitmap_lo);
    auto locIMap = CAT3232U(gd->bg_inode_bitmap_hi, gd->bg_inode_bitmap_lo);
    auto locITab = CAT3232U(gd->bg_inode_table_hi, gd->bg_inode_table_lo);

    pr("Group[%lu]", i);
    pr("\tBMap: %lu(+%luBlks)", locBMap, blksBMap);
    pr("\tIMap: %lu(+%luBlks)", locIMap, blksIMap);
    pr("\tITab: %lu(+%luBlks)", locITab, blksITab);
    free(gd);
  }
}

Ext4::~Ext4() {
  free(this->sb);
  DRAM::dealloc(this->grpCache);
  DRAM::dealloc(this->lruInoCache);
  DRAM::dealloc(this->NameiCache);
}

/* -------------------------------------------------------------------------- */
/*                              internal members                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Get the superblock structure from disk
 *
 * @note This function is currently only used in ctor and testing
 *
 * @return Ext4::super_block* return nullptr if failed; otherwise a malloc
 * super_block object is returned and shall be explicitly free'd after use.
 */
Ext4::super_block *Ext4::getSuper(_SIM_PARAMS) {
  auto doit = [](_SIM_PARAMS) -> super_block * {
    super_block *sb;

    sb = (super_block *)malloc(sizeof(super_block));
    if (!sb) {
      pr("sb malloc fail");
      return nullptr;
    }

    FTL::read(sb, Ext4::SB_OFFSET, Ext4::SB_SIZE _add_sim_params);
    if (sb->s_magic != Ext4::SB_MAGIC_LE) {
      pr("Weird magic: %04X", sb->s_magic);
      free(sb);
    }
    return sb;
  };

  auto res = doit(_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__GET_SUPER);
  return res;
}

/**
 * @brief Get the group descriptor structure of target group from disk
 *
 * @param iGrp The target group number
 *
 * @return Ext4::group_desc* return nullptr if failed; otherwise a malloc
 * group_desc object is returned and shall be explicitly free'd after use.
 */
Ext4::group_desc *Ext4::getGrpDesc(grp_t iGrp _ADD_SIM_PARAMS) {
  auto doit = [this](grp_t iGrp _ADD_SIM_PARAMS) -> group_desc * {
    assert(iGrp < this->nrGrps);

    group_desc *gd = (group_desc *)malloc(sizeof(group_desc));
    if (!gd)
      return nullptr;

    // if cached, return a replica
    GrpNode t;
    this->grpCache->read(iGrp * sizeof(t), sizeof(t), &t _add_sim_params);
    if (t.valid) {
      memcpy(gd, &t.data, sizeof(group_desc));
      pr("Group[%lu]: Cache hit", iGrp);
    }
    // if not cached (startup), read from SSD
    else {
      // GDT at block1, iGrp-th entry for group iGrp
      off_t ofs = iGrp * sizeof(group_desc);
      FTL::read(gd, Ext4::BLK_SIZE + ofs, sizeof(group_desc) _add_sim_params);

      t = {.valid = true, .data = *gd};
      this->grpCache->write(iGrp * sizeof(t), sizeof(t), &t _add_sim_params);
      pr("Group[%lu]: Cache miss, updated", iGrp);
    }
    return gd;
  };

  auto res = doit(iGrp _add_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__GET_GROUP);
  return res;
}

/**
 * @brief Get inode bitmap of a specific group from disk
 *
 * @param i The group number of target inode bitmap
 *
 * @return bitmap_t return nullptr if failed; otherwise a malloc bitmap_t
 * object is returned and shall be explicitly free'd after use.
 */
bitmap_t Ext4::getInoMap(ino_t ino _ADD_SIM_PARAMS) {
  auto doit = [this](ino_t ino _ADD_SIM_PARAMS) -> bitmap_t {
    // get group desc
    auto gd =
        getGrpDesc((ino - 1) / this->sb->s_inodes_per_group _add_sim_params);
    if (!gd) {
      pr("gd malloc fail");
      return nullptr;
    }

    // where is the imap
    blk_t baseIMap = CAT3232U(gd->bg_inode_bitmap_hi, gd->bg_inode_bitmap_lo);
    size_t szIMap = this->sb->s_inodes_per_group >> 3;
    assert(baseIMap && szIMap);
    free(gd);

    // alloc bitmap buffer
    auto bm = (bitmap_t)malloc(szIMap);
    if (!bm) {
      pr("im malloc fail");
      return nullptr;
    }

    FTL::read(bm, baseIMap * this->BLK_SIZE, szIMap _add_sim_params);
    return bm;
  };

  auto res = doit(ino _add_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__GET_IMAP);
  return res;
}

/**
 * @brief Get inode structure from disk
 *
 * @note The inode size is determined by the `s_inode_size` member in the
 * superblock, don't use `sizeof(inode)` to parse the data.
 *
 * @param inoNum The inode number (0-based, but ino #0 is not existing)
 *
 * @return Ext4::inode* return nullptr if failed; otherwise a malloc inode
 * object is returned and shall be explicitly free'd after use.
 */
Ext4::inode *Ext4::getInode(ino_t inoNum _ADD_SIM_PARAMS) {
  auto doit = [this](ino_t inoNum _ADD_SIM_PARAMS) -> inode * {
    if (!inoNum) {
      pr("ino #0 not exists");
      return nullptr;
    }

    auto ino = (Ext4::inode *)malloc(ICacheNode::szVal);
    if (!ino) {
      perr("ino malloc fail");
      return nullptr;
    }

    // check inode cache
    ICacheNode e = {.number = inoNum, .inode = ino};
    if (-ENOENT != lruInoCache->read(0, 0, &e _add_sim_params)) {
      pr("ICache hit %lu", inoNum);
      return ino;
    }

    // inode #0 not exists, start from #1
    grp_t iGrp = (inoNum - 1) / this->sb->s_inodes_per_group;
    off_t oGrp = (inoNum - 1) % this->sb->s_inodes_per_group;
    group_desc *gd = this->getGrpDesc(iGrp _add_sim_params);
    if (!gd) {
      pr("gd malloc fail");
      free(ino);
      return nullptr;
    }

    blk_t baseITab = CAT3232U(gd->bg_inode_table_hi, gd->bg_inode_table_lo);
    off_t ofsITab = baseITab * Ext4::BLK_SIZE + oGrp * this->sb->s_inode_size;
    FTL::read(ino, ofsITab, this->sb->s_inode_size _add_sim_params);

    // add to inode cache
    lruInoCache->write(0, 0, &e _add_sim_params);

    free(gd);
    return ino;
  };

  auto res = doit(inoNum _add_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__GET_INODE);

  // if not contig, reset counter
  if (inoNum != lastIno + 1) {
    numContigIno = 0;
  }
  else {
    numContigIno += 1;

    // contig but too much (out of prefetch range) -> reset
    if (numContigIno >= INODE_PREFETCH_NUM)
      numContigIno = 0;

    // contig but not reach the threshold yet
    else if (numContigIno < INODE_PREFETCH_THRESHOLD) {
      pr("Need %lu more contig inodes to trigger prefetching",
         INODE_PREFETCH_THRESHOLD - numContigIno);
    }

    // just reach the threshold -> do prefetch
    else if (numContigIno == INODE_PREFETCH_THRESHOLD) {
      pr("getInode: start prefetching (from %lu)", lastIno + 1);
      for (size_t i = 1; i <= INODE_PREFETCH_NUM; ++i) {
        lastIno = inoNum + i;
        free(doit(lastIno _add_sim_params));
        simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__GET_INODE);
      }
      pr("getInode: end prefetching (at %lu)", lastIno);
    }

    // already prefetched, and not over the range yet -> skip
  }
  lastIno = inoNum;

  return res;
}

/**
 * @brief Traverse the extent tree and push extents into the given list
 *
 * This function use DFS to traverse all the extent blocks. During the recursion
 * process, either `blkNum` or `buf` will be used to get the target extent block
 * (explained later). On visiting an extent block, this function will copy the
 * extent entries into `el` and increase `ofs` by the number of entries found.
 *
 * For the initial call, the `buf` is expected to be the inline extent block
 * `inode.i_block`.
 *
 * For the following calls, the `blkNum` is expected to be the block number of
 * the next-level extent node, so that this function can issue FTL request to
 * get the target block.
 *
 * @param blkNum The block number of target extent block
 * @param buf The data buffer of target extent block
 * @param el The extent list buffer
 * @param ofs The current extent list offset
 */
void Ext4::extractExtents(blk_t blkNum, byte *buf, Ext *el, size_t ofs,
                          size_t dep, size_t *sz _ADD_SIM_PARAMS) {
  auto doit = [](blk_t blkNum, byte *buf, Ext *el, size_t ofs [[maybe_unused]],
                 size_t dep, size_t *sz _ADD_SIM_PARAMS) {
    // get extent block from disk if not given
    byte defaultBuffer[Ext4::BLK_SIZE] = {0};

    if (!el) {
      pr("invalid address for returning extent list");
      return;
    }

    if (!sz) {
      pr("invalid address for returning current extent list offset");
      return;
    }

    if (!buf) {
      buf = defaultBuffer;
      FTL::read(buf, blkNum * Ext4::BLK_SIZE, Ext4::BLK_SIZE);
    }

    // check whether leaf node
    auto eh = (extent_header *)buf;
    assert(eh->eh_magic == EXT4_EXTENT_HEADER_MAGIC);
    if (eh->eh_depth) {
      // parse extent idx
      auto ei = (extent_idx *)&buf[sizeof(*eh)];
      for (size_t ie = 0; ie < eh->eh_entries; ++ie) {
        auto b = CAT3232U(ei[ie].ei_leaf_hi, ei[ie].ei_leaf_lo);
        size_t len = 0;
        extractExtents(b, nullptr, el, *sz, dep + 1, &len _add_sim_params);
        el += len;
        *sz += len;
      }
    }
    else {
      // push all extents
      auto es = (extent *)&buf[sizeof(*eh)];
      for (size_t ie = 0; ie < eh->eh_entries; ++ie) {
        // pipe2xxd("extent raw data:", &es[ie], sizeof(extent), NULL);
        const auto e = es[ie];

        el[ie].block = e.ee_block;
        el[ie].slbn = CAT3232U(e.ee_start_hi, e.ee_start_lo);
        el[ie].len = e.ee_len;

        // pr("Ext[%lu][%lu+%lu]: push b,s,l=%lu,%lu,%lu", dep, ofs, ie,
        //    el[ie].block, el[ie].slbn, el[ie].len);
      }
      *sz += eh->eh_entries;
    }
  };

  doit(blkNum, buf, el, ofs, dep, sz _add_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__GET_EXTENT_INTERNAL);
}

/**
 * @brief Traverse the extent tree and calc the extent list length
 *
 * This function is similar to `extractExtents`, but only calc the number of
 * extent entries used by the inode and put the resul into `len`.
 *
 * @param blkNum The block number of target extent block
 * @param buf The data buffer of target extent block
 * @param len The output of extent list length
 */
void Ext4::calcExtentSize(blk_t blkNum, byte *buf,
                          size_t *len _ADD_SIM_PARAMS) {
  auto doit = [](blk_t blkNum, byte *buf, size_t *len _ADD_SIM_PARAMS) {
    // get extent block from disk if not given
    byte defaultBuffer[Ext4::BLK_SIZE] = {0};

    if (!len) {
      pr("invalid address for returning extent list length");
      return;
    }

    if (!buf) {
      buf = defaultBuffer;
      FTL::read(buf, blkNum * Ext4::BLK_SIZE, Ext4::BLK_SIZE);
    }

    // check whether leaf node
    auto eh = (extent_header *)buf;
    assert(eh->eh_magic == EXT4_EXTENT_HEADER_MAGIC);
    assert(eh->eh_depth <= 5);
    assert(eh->eh_entries <= eh->eh_max);
    if (!eh->eh_depth) {
      *len += eh->eh_entries;
    }
    else {
      // parse extent idx
      auto ei = (extent_idx *)&buf[sizeof(*eh)];
      for (size_t ie = 0; ie < eh->eh_entries; ++ie) {
        auto b = CAT3232U(ei[ie].ei_leaf_hi, ei[ie].ei_leaf_lo);
        calcExtentSize(b, nullptr, len _add_sim_params);
      }
    }
  };

  doit(blkNum, buf, len _add_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__GET_EXTENT_SIZE);
}

/**
 * @brief Get extent list from disk
 *
 * @bug File with multi-level extent not implemented yet
 *
 * @param inoNum The inode number (0-based, but ino #0 is not existing)
 * @param len The number of entries of output extent list
 * @param pIno The buffer where the target inode struct will be copied into if
 * not NULL
 *
 * @return Ext4::Ext* return nullptr if failed; otherwise a malloc extent list
 * object is returned and shall be explicitly free'd after use.
 */
Ext4::Ext *Ext4::getExtent(ino_t inoNum, size_t *len,
                           inode *pIno _ADD_SIM_PARAMS) {
  auto doit = [this](ino_t inoNum, size_t *len,
                     inode *pIno _ADD_SIM_PARAMS) -> Ext * {
    size_t sz = 0, ofs = 0;
    Ext *exts = nullptr;

    inode *ino;
    byte *ee;
    extent_header *eh;

    if (!len) {
      pr("invalid address for returning extent size");
      goto out;
    }

    ino = getInode(inoNum _add_sim_params);
    if (!(ino->i_flags & EXT4_EXTENTS_FL)) {
      pr("feature of extent is not supported");
      goto free_inode;
    }

    // parse extent tree
    eh = (extent_header *)ino->i_block;
    if (eh->eh_magic != EXT4_EXTENT_HEADER_MAGIC) {
      pr("invalid eh magic: %x", eh->eh_magic);
      goto free_inode;
    }

    if (eh->eh_depth > 5)
      panic("!!!Extent Tree depth should never > 5!!!");

    // calc needed entries
    sz = 0;
    ee = (byte *)ino->i_block;
    calcExtentSize(0, ee, &sz _add_sim_params);
    pr("Number of extent entries: %lu", sz);

    // do DFS (recursion)
    exts = (Ext *)calloc(sz, sizeof(Ext));
    if (!exts) {
      perr("exts calloc failed");
      goto free_inode;
    }
    extractExtents(0, ee, exts, 0, 0, &ofs _add_sim_params);
    assert(sz == (size_t)ofs);

    // copy inode to given buffer
    if (pIno)
      memcpy(pIno, ino, MIN32U(this->sb->s_inode_size, sizeof(inode)));

  free_inode:
    free(ino);
  out:
    *len = sz;
    return exts;
  };

  auto res = doit(inoNum, len, pIno _add_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__GET_EXTENT);
  return res;
}

/**
 * @brief Get the parent (dotdot) inode number of the given inode number
 *
 * @todo Add inode cache
 *
 * @param curIno The inode number you want to find its parent inode
 * @return Ext4::ino_t The parent inode number of the target file.
 * Ext4::ERROR_INO if not found
 */
Ext4::ino_t Ext4::getParentInode(ino_t curIno _ADD_SIM_PARAMS) {
  auto doit = [this](ino_t curIno _ADD_SIM_PARAMS) -> ino_t {
    pr("Ino[%lu]: get parent inode", curIno);

    if (curIno == Ext4::ROOT_INO)
      return Ext4::ROOT_INO;

    // first 4B of data block is parent inode
    size_t szExt;
    auto exts = this->getExtent(curIno, &szExt, nullptr _add_sim_params);
    if (!exts) {
      pr("Ino[%lu]: extent not found", curIno);
      return Ext4::ERROR_INO;
    }

    char buf[Ext4::BLK_SIZE];
    FTL::read(buf, exts[0].slbn, Ext4::BLK_SIZE _add_sim_params);
    free(exts);

    dx_root *dx = (dx_root *)buf;
    pr("\tparent inode: %u", dx->dotdot.inode);
    return dx->dotdot.inode;
  };

  auto res = doit(curIno _add_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__GET_INODE_PARENT);
  return res;
}

/**
 * @brief Find the inode number of the specified file in the given dir
 *
 * @todo Support htree (dir_index feature)
 * @todo Add dir_entry cache
 *
 * @param tgName The target file name (name only, not path)
 * @param tgNameLen The length of the target file name
 * @param dirIno The inode number of the directory to find the file
 * @return Ext4::ino_t The inode number of the target file. Ext4::ERROR_INO if
 * not found
 */
Ext4::ino_t Ext4::dirSearchFile(const char *tgName, size_t tgNameLen,
                                ino_t dirIno _ADD_SIM_PARAMS) {
  auto doit = [this](const char *tgName, size_t tgNameLen,
                     ino_t dirIno _ADD_SIM_PARAMS) -> ino_t {
    if (!tgName) {
      pr("ERROR!! searching failed, invalid address for component name");
      return Ext4::ERROR_INO;
    }
    pr("DirIno[%lu]: searching file '%s'(+%lu)", dirIno, tgName, tgNameLen);

    inode ino;
    size_t szExts;

    // namei cache to speed up
    NameiNode cache(dirIno, tgName, tgNameLen, Ext4::ERROR_INO);
    pr("NameiCache search %lu::'%s'", cache.diri, cache.name);

    if (-ENOENT != this->NameiCache->read(0, 0, &cache _add_sim_params)) {
      pr("NameiCache hit");
      return cache.inode;
    }
    pr("NameiCache miss");

    auto exts = this->getExtent(dirIno, &szExts, &ino _add_sim_params);
    if (!exts) {
      pr("ERROR!! Ino[%lu]: extent not found", dirIno);
      return Ext4::ERROR_INO;
    }
    assert(szExts > 0);

    size_t szBuf = 0;
    for (size_t ie = 0; ie < szExts; ++ie)
      szBuf += exts[ie].len * Ext4::BLK_SIZE;
    pr("\tbuffer size: %lu", szBuf);

    char *buf = (char *)malloc(szBuf);
    if (!buf) {
      perr("extent malloc fail");
      free(exts);
      return Ext4::ERROR_INO;
    }

    for (size_t ie = 0; ie < szExts; ++ie) {
      auto ofsBuf = ie * Ext4::BLK_SIZE;
      auto ofsData = exts[ie].slbn * Ext4::BLK_SIZE;
      auto szData = exts[ie].len * Ext4::BLK_SIZE;
      FTL::read(&buf[ofsBuf], ofsData, szData _add_sim_params);
    }

    // search dir entries
    cache.inode = Ext4::ERROR_INO;
    if (ino.i_flags & EXT4_INDEX_FL) {
      // this dir use htree
      pr("dirent: htree struct not supported");
    }
    else {
      // simple linear struct
      for (size_t ie [[maybe_unused]] = 0, ofs = 0; ofs < szBuf; ++ie) {
        auto node = (dx_node *)&buf[ofs];
        auto ino = node->fake.inode;
        auto nlen = node->fake.name_len;
        auto rlen = node->fake.rec_len;
        auto name = &node->data;

        if (!ino && rlen < sizeof(fake_dirent))  // ino 0 indicates last entry
          break;

        if (tgNameLen == nlen && !strncmp(name, tgName, tgNameLen)) {
          // don't break immediately, add all to cache
          cache.inode = ino;
        }

        // cache all namei ever seen, not the target only
        NameiNode tmp(dirIno, name, nlen, ino);
        pr("NameiCache add %lu::'%s'", tmp.diri, tmp.name);
        this->NameiCache->write(0, 0, &tmp _add_sim_params);

        ofs += rlen;
      }
    }

    if (cache.inode == Ext4::ERROR_INO) {
      pr("component '%s' not found...| szExts = %lu | tgNameLen = %lu", tgName,
         szExts, tgNameLen);
    }
    else {
      pr("component found: Ino[%lu]", cache.inode);
    }

    free(exts);
    free(buf);
    return cache.inode;
  };

  auto res = doit(tgName, tgNameLen, dirIno _add_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__DIR_SEARCH_FILE);
  return res;
}

/**
 * @brief Translate the given file path into its
 *
 * @todo Support relative path.
 *
 * @param p The path of the target file
 * @return Ext4::ino_t The inode number of the target file. Ext4::ERROR_INO if
 * not found
 */
Ext4::ino_t Ext4::namei(const char *p _ADD_SIM_PARAMS) {
  auto doit = [this](const char *p _ADD_SIM_PARAMS) -> ino_t {
    pr("lookup: '%s'", p);

    // parse leading slashes
    bool absPath = *p == '/';
    while (*p == '/')
      ++p;

    // default component is root dir for abs path
    ino_t inoComp = Ext4::ROOT_INO;

    if (!absPath) {
      pr("WARN!! relative path not implemented, treat as asb path");
      // inoComp = this->cwdIno;
    }

    // walk components to get target dir entry
    const char *comp = p;
    int clen = 0;

    for (;; comp += clen, clen = 0) {
      // handle leading slashes again
      while (*comp == '/')
        ++comp;

      // last comp done
      if (!*comp) {
        pr("last component done, out");
        break;
      }

      // calc comp len
      while (comp[clen] && comp[clen] != '/')
        ++clen;
      pr("component: '%.*s'(+%d)", (int)clen, comp, clen);
      assert(clen > 0);

      // handle special paths: . and ..
      if (clen < 2 && *comp == '.') {
        if (clen == 2)  // back to parent if comp is '..'
          inoComp = getParentInode(inoComp _add_sim_params);
        else
          pr("searching '.', cont");
      }
      // get component inode from current dir
      else {
        inoComp = dirSearchFile(comp, clen, inoComp _add_sim_params);
        if (Ext4::ERROR_INO == inoComp) {
          pr("ERROR!! searching fail, out");
          break;
        }

        // fixme: handle symlink
      }
    }

    pr("last component inode: %ld", *(int64_t *)&inoComp);
    return inoComp;
  };

  auto res = doit(p _add_sim_params);
  simApplyLatency(CPU::ISC__FSA__EXT4, CPU::ISC__NAMEI);
  return res;
}

GenericFSA::ExtList Ext4::builtin_getExt(const char *p _ADD_SIM_PARAMS) {
  ExtList list;

  inode ino;
  ino_t inoNum = namei(p _add_sim_params);

  if (inoNum != Ext4::ERROR_INO) {
    list.exts = getExtent(inoNum, &list.len, &ino _add_sim_params);
    list.bytes = CAT3232(ino.i_size_high, ino.i_size_lo);
  }
  else
    pr("File '%s' not found...", p);
  return list;
}

void *Ext4::builtin_getInode(uint64_t ino _ADD_SIM_PARAMS) {
  return Ext4::getInode(ino _add_sim_params);
}

}  // namespace ISC
}  // namespace SimpleSSD