#include "sims/configs.hh"
#include "sims/ftl.hh"

#include "runtime.hh"

#include "fs/ext4/ext4.hh"
#include "slet/statdir.hh"

namespace SimpleSSD {
namespace ISC {

#define PR_SECTION LOG_ISC_SLET_STATDIR

#define STATDIR_INODE_FILTER CPU::ISC__TASK1

template ISC_STS_SLET_ID Runtime::addSlet<StatdirAPP>(_SIM_PARAMS);

using namespace SIM;

typedef struct dirent : public Ext4::dx_node {
} dent_t;

typedef struct inode : public Ext4::inode {
} inode_t;

StatdirAPP::StatdirAPP(_SIM_PARAMS) {
  this->setOpt(ISC_KEY_NAME, strdup(str(StatdirAPP)));
}

size_t StatdirAPP::inodeFilter(const char *path, const char *dents,
                               size_t szBuf, data_t *res _ADD_SIM_PARAMS) {
  auto doit = [](const char *path, const char *dents, size_t szBuf,
                 data_t *res _ADD_SIM_PARAMS) -> size_t {
    size_t nd = 0, ofs = 0;
    for (auto d = (dent_t *)dents; ofs < szBuf && d->fake.inode;
         d = (dent_t *)(dents + ofs), ++nd) {
      auto ino =
          (inode_t *)Runtime::getInode(path, d->fake.inode _add_sim_params);

      res[nd].mtime = ino->i_mtime;
      res[nd].size = ino->i_size_lo;
      res[nd].mode = ino->i_mode;
      memcpy(res[nd].data, &d->data, d->fake.name_len);

      free(ino);

      // skip this dentry and 1 dummy tail dent (checksum)
      ofs += d->fake.rec_len;
      d = (dent_t *)(dents + ofs);
      if (!d->fake.inode && d->fake.file_type == 0xde)
        ofs += d->fake.rec_len;
    }
    return nd;
  };

  auto nums = doit(path, dents, szBuf, res _add_sim_params);
  simApplyManyLatency(CPU::ISC__SLET__STATDIR, STATDIR_INODE_FILTER, nums);
  return nums;
}

ISC_STS StatdirAPP::builtin_startup(_SIM_PARAMS) {
  auto doit = [this](_SIM_PARAMS) {
    // get inputs
    auto path = (const char *)this->getOpt(keyPath);

    // opendir
    auto extList = Runtime::getExts(path _add_sim_params);
    auto szBuf = (size_t *)calloc(1, sizeof(size_t));
    for (size_t ie = 0; ie < extList.len; ++ie)
      *szBuf += extList.exts[ie].len * BLK_SIZE;

    // getdents
    auto dents = (char *)calloc(1, *szBuf);
    for (size_t ie = 0; ie < extList.len; ++ie) {
      auto ofsBuf = ie * BLK_SIZE;
      auto ofsData = extList.exts[ie].slbn * BLK_SIZE;
      auto szData = extList.exts[ie].len * BLK_SIZE;
      FTL::read(&dents[ofsBuf], ofsData, szData _add_sim_params);
    }

    // read inodes
    auto res = (data_t *)calloc(*szBuf / sizeof(dent_t), sizeof(data_t));
    auto nd = inodeFilter(path, dents, *szBuf, res _add_sim_params);

    // update convert szbuf to result size
    *szBuf = sizeof(data_t) * nd;

    // clear data
    free(extList.exts);
    free(dents);

    auto sts = this->setOpt(keyResultSize, szBuf);
    if (sts != ISC_STS_OK)
      return sts;
    return this->setOpt(keyResult, res);
  };
  auto res = doit(_sim_params);
  simApplyLatency(CPU::ISC__SLET__STATDIR, CPU::ISC__START_SLET);
  return res;
}

const char *StatdirAPP::keyPath = "path";
const char *StatdirAPP::keyResult = ISC_KEY_RESULT;
const char *StatdirAPP::keyResultSize = ISC_KEY_RESULT_SIZE;

}  // namespace ISC
}  // namespace SimpleSSD
