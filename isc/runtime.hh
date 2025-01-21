#ifndef __SIMPLESSD_ISC_RUNTIME_HH__
#define __SIMPLESSD_ISC_RUNTIME_HH__

#include "sims/cpu.hh"

#include "types.hh"

namespace SimpleSSD {
namespace ISC {

#define PR_SECTION LOG_ISC_RUNTIME

class Runtime {  // make this class semantically static

 private:
  static ISC_STS_SLET_ID nextSletId;
  static ISC_STS_SLET_ID nextFSAId;
  static list<pair<ISC_STS_SLET_ID, GenericSlet *>> lSlet;
  static list<pair<ISC_STS_SLET_ID, GenericFSA *>> lFSA;

 public:
  Runtime() = delete;
  ~Runtime() {}

  /**
   * @brief delete all slets in the runtime
   */
  static void destory() {
    for (auto &s : lSlet) {
      pr("APP id %d deleted", s.first);
      delete s.second;
    }
    lSlet.clear();
    for (auto &s : lFSA) {
      pr("FSA id %d deleted", s.first);
      delete s.second;
    }
    lFSA.clear();
  }

  template <class S>
  static ISC_STS_SLET_ID addSlet(_SIM_PARAMS) {
    static_assert(
        std::is_base_of<GenericAPP, S>() || std::is_base_of<GenericFSA, S>(),
        "Make sure your slet class is based on Generic(APP|FSA)");

    const char *type;
    auto slet = new S(_sim_params);
    auto id = ISC_STS_EID;

    if (dynamic_cast<GenericFSA *>(slet)) {
      type = "FSA";
      id = ++nextFSAId;
      lFSA.push_back({id, (GenericFSA *)slet});
    }
    else if (dynamic_cast<GenericAPP *>(slet)) {
      type = "APP";
      id = ++nextSletId;
      lSlet.push_back({id, slet});
    }
    else {
      delete slet;
      pr("WARN!!! not expected to be here, compiler should complain");
    }

    pr("Assign id %d to %s: %s (mangled name)", id, type, typeid(S).name());
    return id;
  }
  static ISC_STS delSlet(ISC_STS_SLET_ID id);
  static ISC_STS startSlet(ISC_STS_SLET_ID _ADD_SIM_PARAMS);
  static ISC_STS setOpt(ISC_STS_SLET_ID, const char *, void *_ADD_SIM_PARAMS);
  static void *getOpt(ISC_STS_SLET_ID, const char *_ADD_SIM_PARAMS);

  static GenericFSA::ExtList getExts(const char *_ADD_SIM_PARAMS);
  static void *getInode(const char *, uint64_t _ADD_SIM_PARAMS);

 protected:
  static inline GenericSlet *findSlet(ISC_STS_SLET_ID id) {
    for (auto &s : lSlet)
      if (s.first == id)
        return s.second;
    return nullptr;
  }
};

#undef PR_SECTION

}  // namespace ISC
}  // namespace SimpleSSD

#endif  // __SIMPLESSD_ISC_RUNTIME_HH__