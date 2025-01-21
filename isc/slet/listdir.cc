#include "sims/cpu.hh"
#include "sims/ftl.hh"

#include "slet/listdir.hh"

#include "runtime.hh"
#include "sims/configs.hh"

#define ISC_APP_CLASS ListdirAPP
#define PR_SECTION LOG_ISC_SLET_LISTDIR

namespace SimpleSSD {
namespace ISC {

template ISC_STS_SLET_ID Runtime::addSlet<ISC_APP_CLASS>(_SIM_PARAMS);

using namespace SIM;

using SIM::FTL;

const char *ISC_APP_CLASS::keyPath = "path";
const char *ISC_APP_CLASS::keyResult = ISC_KEY_RESULT;
const char *ISC_APP_CLASS::keyResultSize = ISC_KEY_RESULT_SIZE;

ISC_APP_CLASS::ISC_APP_CLASS(_SIM_PARAMS) {
  this->opt.name = strdup(str(ISC_APP_CLASS));
}

ISC_STS ISC_APP_CLASS::builtin_startup(_SIM_PARAMS) {
  auto doit = [this](_SIM_PARAMS) -> ISC_STS {
    const size_t BLK_SIZE = 4096;

    // get inputs
    auto path = (const char *)this->getOpt(keyPath);

    // opendir
    auto extList = Runtime::getExts(path _add_sim_params);
    auto szBuf = (uintptr_t *)calloc(1, sizeof(size_t));
    for (size_t ie = 0; ie < extList.len; ++ie)
      *szBuf += extList.exts[ie].len * BLK_SIZE;

    // getdents
    auto dents = (char *)calloc(1, *szBuf);
    for (size_t ie = 0; ie < extList.len; ++ie) {
      auto ofsBuf = ie * BLK_SIZE;
      auto ofsData = extList.exts[ie].slbn * BLK_SIZE;
      auto szData = extList.exts[ie].len * BLK_SIZE;
      FTL::read(&dents[ofsBuf], ofsData, szData);
    }

    free(extList.exts);

    auto sts = this->setOpt(keyResultSize, szBuf);
    if (sts == ISC_STS_OK)
      return this->setOpt(keyResult, dents);
    return sts;
  };

  auto res = doit(_sim_params);
  simApplyLatency(CPU::ISC__SLET__LISTDIR, CPU::ISC__START_SLET);
  return res;
}

}  // namespace ISC
}  // namespace SimpleSSD