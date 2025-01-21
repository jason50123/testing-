#include "sims/cpu.hh"
#include "sims/ftl.hh"

#include "slet/randread.hh"

#include "runtime.hh"

#define PR_SECTION LOG_ISC_SLET_RANDREAD

namespace SimpleSSD {
namespace ISC {

using namespace SIM;

const char *RandReadAPP::keyPath = "path";
const char *RandReadAPP::keyOffsets = "offsets";
const char *RandReadAPP::keyResult = "result";
const char *RandReadAPP::keyConf = "conf";

RandReadAPP::RandReadAPP(_SIM_PARAMS) {
  this->setOpt("name", strdup("RandReadAPP"));
}

ISC_STS RandReadAPP::builtin_startup(_SIM_PARAMS) {
  // get required args
  auto path = (const char *)this->getOpt(RandReadAPP::keyPath);
  auto conf = (Work *)this->getOpt(RandReadAPP::keyConf);
  auto offsets = (size_t *)this->getOpt(RandReadAPP::keyOffsets);
  if (!path || !conf || !offsets) {
    pr("some required input opts are missing!");
    return ISC_STS_EARGS;
  }
  pr("target file '%s'", path);
  pr("configs:");
  pr("\tszFile=%lu", conf->szFile);
  pr("\tszEachIO=%lu", conf->szEachRead);
  pr("\tszTotalIO=%lu", conf->szTotal);
  pr("\tnumIO=%lu", conf->numIO);

  // get target file extents
  auto extlist = Runtime::getExts(path _add_sim_params);
  if (!extlist.exts) {
    pr("failed to get extents of '%s'", path);
    return ISC_STS_FAIL;
  }

  // read file data
  const size_t BLOCKSIZE = 4096;
  char *result = (char *)malloc(conf->szTotal);
  assert(result);
  for (size_t iOfs = 0, ofs = 0; iOfs < conf->numIO; ++iOfs) {
    size_t ie, le, dist = offsets[iOfs];

    // move to the extent of target file offset
    for (ie = 0; dist >= (le = extlist.exts[ie].len * BLOCKSIZE); ++ie)
      dist -= le;

    // determine data address and size
    size_t szWant = conf->szEachRead, szLeft = le - dist;
    size_t beg = (extlist.exts[ie].slbn * BLOCKSIZE) + dist,
           len = std::min(szLeft, szWant);

    while (szWant > 0) {
      FTL::read(&result[ofs], beg, len _add_sim_params);
      ofs += len;

      // need still next extent?
      if (szWant == len)
        break;
      else if (szWant < len)
        assert(0 && "not expect to be here");

      // yes, check next extent
      szWant -= len;
      szLeft = extlist.exts[++ie].len * BLOCKSIZE;
      beg = extlist.exts[ie].slbn * BLOCKSIZE;
      len = std::min(szLeft, szWant);
    }
  }

  free(extlist.exts);
  this->setOpt(RandReadAPP::keyResult, result);
  return ISC_STS_OK;
}

}  // namespace ISC
}  // namespace SimpleSSD