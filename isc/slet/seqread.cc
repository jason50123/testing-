#include "sims/cpu.hh"
#include "sims/ftl.hh"

#include "slet/seqread.hh"

#include "runtime.hh"

#define PR_SECTION LOG_ISC_SLET_SEQREAD

namespace SimpleSSD {
namespace ISC {

using namespace SIM;
using SIM::FTL;

const char *SeqReadAPP::keyPath = "path";
const char *SeqReadAPP::keyResult = "result";

SeqReadAPP::SeqReadAPP(_SIM_PARAMS) {
  this->setOpt("name", strdup("SeqReadAPP"));
}

ISC_STS SeqReadAPP::builtin_startup(_SIM_PARAMS) {
  // get required args
  auto path = (const char *)this->getOpt(SeqReadAPP::keyPath);
  pr("target file '%s'", path);
  if (!path) {
    pr("target path not set!");
    return ISC_STS_EARGS;
  }

  // get target file extents
  auto extlist = Runtime::getExts(path _add_sim_params);
  if (!extlist.exts) {
    pr("failed to get extents of '%s'", path);
    return ISC_STS_FAIL;
  }

  // calc buffer for data blocks
  const size_t SZ_BLK = 4096;
  size_t szBuf = 0;
  for (size_t i = 0; i < extlist.len; ++i)
    szBuf += extlist.exts[i].len * SZ_BLK;
  pr("buf size = %lu", szBuf);

  // read file data
  char *buffer = (char *)malloc(szBuf);
  for (size_t i = 0, ofs = 0, s, l; i < extlist.len; ++i) {
    s = extlist.exts[i].slbn * SZ_BLK;
    l = extlist.exts[i].len * SZ_BLK;
    FTL::read(&buffer[ofs], s, l _add_sim_params);
    ofs += SZ_BLK * extlist.exts[i].len;
  }

  this->setOpt(SeqReadAPP::keyResult, buffer);

  free(extlist.exts);
  return ISC_STS_OK;
}

}  // namespace ISC
}  // namespace SimpleSSD
