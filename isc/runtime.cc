#include "sims/ftl.hh"

using namespace SimpleSSD::ISC::SIM;

#include <cstring>

#include "runtime.hh"
#include "types.hh"

#define PR_SECTION LOG_ISC_RUNTIME

namespace SimpleSSD {
namespace ISC {

/* -------------------------------------------------------------------------- */
/*                          init static class members                         */
/* -------------------------------------------------------------------------- */

ISC_STS_SLET_ID Runtime::nextSletId = 0;
ISC_STS_SLET_ID Runtime::nextFSAId = 0;

list<pair<ISC_STS_SLET_ID, GenericSlet *>> Runtime::lSlet;
list<pair<ISC_STS_SLET_ID, GenericFSA *>> Runtime::lFSA;

/* -------------------------------------------------------------------------- */
/*                       major function implementations                       */
/* -------------------------------------------------------------------------- */

ISC_STS Runtime::delSlet(ISC_STS_SLET_ID id) {
  pr("Del slet %d", id);

  size_t nRemoved = 0;
  lSlet.remove_if(
      [id, &nRemoved](pair<ISC_STS_SLET_ID, GenericSlet *> _slet) mutable {
        if (_slet.first == id) {
          nRemoved++;
          delete _slet.second;
          pr("Slet[%d] deleted", id);
          return true;
        }
        return false;
      });

  pr("%ld slets are removed", nRemoved);
  pr("Remains %lu slets", lSlet.size());
  return ISC_STS_OK;
}

ISC_STS Runtime::startSlet(ISC_STS_SLET_ID id _ADD_SIM_PARAMS) {
  auto doit = [](ISC_STS_SLET_ID id _ADD_SIM_PARAMS) -> ISC_STS {
    pr("Start slet %d", id);
    GenericSlet *slet = Runtime::findSlet(id);
    if (!slet) {
      pr("Slet %d: not found", id);
      return ISC_STS_EID;
    }
    return slet->builtin_startup(_sim_params);
  };

  auto res = doit(id _add_sim_params);
  simApplyLatency(CPU::ISC__RUNTIME, CPU::ISC__START_SLET);
  return res;
}

GenericFSA::ExtList Runtime::getExts(const char *path _ADD_SIM_PARAMS) {
  // search all FSA and find the mount point
  auto doit = [](const char *path _ADD_SIM_PARAMS) -> GenericFSA::ExtList {
    for (auto &fsa : lFSA) {
      const char *cwd = (char *)fsa.second->getOpt("cwd");
      if (!strncmp(path, cwd, strlen(cwd))) {
        return fsa.second->builtin_getExt(path _add_sim_params);
      }
    }
    pr("No appropriate FSA found for '%s'", path);
    return GenericFSA::ExtList();
  };

  auto res = doit(path _add_sim_params);
  simApplyLatency(CPU::ISC__RUNTIME, CPU::ISC__GET_EXTENT);
  return res;
}

void *Runtime::getInode(const char *path, uint64_t ino _ADD_SIM_PARAMS) {
  auto doit = [](const char *path, uint64_t ino _ADD_SIM_PARAMS) -> void * {
    for (auto &fsa : lFSA) {
      const char *cwd = (char *)fsa.second->getOpt("cwd");
      if (!strncmp(path, cwd, strlen(cwd))) {
        return fsa.second->builtin_getInode(ino _add_sim_params);
      }
    }
    pr("No appropriate FSA found for '%s'", path);
    return nullptr;
  };

  auto res = doit(path, ino _add_sim_params);
  simApplyLatency(CPU::ISC__RUNTIME, CPU::ISC__GET_INODE);
  return res;
}

ISC_STS Runtime::setOpt(ISC_STS_SLET_ID id, const char *k,
                        void *v _ADD_SIM_PARAMS) {
  auto doit = [](ISC_STS_SLET_ID id, const char *k, void *v _ADD_SIM_PARAMS) {
    auto slet = Runtime::findSlet(id);
    if (!slet) {
      pr("Slet %d not found", id);
      return ISC_STS_FAIL;
    }
    return slet->setOpt(k, v);
  };

  auto res = doit(id, k, v _add_sim_params);
  simApplyLatency(CPU::ISC__RUNTIME, CPU::ISC__SET_OPT);
  return res;
}

void *Runtime::getOpt(ISC_STS_SLET_ID id, const char *k _ADD_SIM_PARAMS) {
  auto doit = [](ISC_STS_SLET_ID id, const char *k _ADD_SIM_PARAMS) -> void * {
    auto slet = Runtime::findSlet(id);
    if (!slet) {
      pr("Slet %d not found", id);
      return nullptr;
    }
    return slet->getOpt(k);
  };
  auto res = doit(id, k _add_sim_params);
  simApplyLatency(CPU::ISC__RUNTIME, CPU::ISC__GET_OPT);
  return res;
}

}  // namespace ISC
}  // namespace SimpleSSD
