#ifndef __SIMPLESSD_ISC_TYPES_HH__
#define __SIMPLESSD_ISC_TYPES_HH__

#include <cstring>  // for memcpy

#include "sims/cpu.hh"

#include "utils/debug.hh"
#include "utils/types.hh"

#define PR_SECTION LOG_ISC

namespace SimpleSSD {
namespace ISC {

typedef uint8_t byte;

typedef int ISC_STS;
#define ISC_STS_OK 0
#define ISC_STS_FAIL -1       // generic failed
#define ISC_STS_EID -2        // invalid slet id or slet not exists
#define ISC_STS_EFUNC -3      // slet function not defined or implemented
#define ISC_STS_EARGS -4      // invalid arguments
typedef int ISC_STS_SLET_ID;  // positive for id, negative for errno

struct Extent {
 public:
  size_t fblk;  // file offset
  size_t slpn;
  size_t nlp;
  Extent(size_t b, size_t s, size_t n) : fblk(b), slpn(s), nlp(n) {}
};

struct SletOpts {
  struct SletKeyCmp {
    /**
     * @brief Allocator for comparing map key
     *
     * In imprecise terms, two objects a and b are considered equivalent (not
     * unique) if neither compares less than the other: !comp(a, b) && !comp(b,
     * a).
     *
     * @ref https://en.cppreference.com/w/cpp/container/map
     */
    bool operator()(const char *a, const char *b) {
      auto res = strcmp(a, b);
      return res < 0;
    }
  };

 public:
  char *cwd = nullptr;
  char *name = nullptr;
  list<Extent> extents;
  map<const char *, void *, SletKeyCmp> extra;
};

enum SletType : uint16_t {
  FSA = 0,
  APP = 1,
  // add types here, value should be less than NUMS
  NUMS = UINT16_MAX,
};

class GenericSlet {
 public:
  virtual ISC_STS builtin_startup(_SIM_PARAMS) { return ISC_STS_EFUNC; }
  virtual ISC_STS builtin_shutdown(byte *, size_t) { return ISC_STS_EFUNC; }

  GenericSlet(SletType t) : type(t) {
    assert(t != SletType::NUMS || "Invalid type");
  }
  virtual ~GenericSlet();

  /**
   * @brief Set the slet options
   *
   * @note the data param should be allocated for each slet, so that the
   * destructor can work normally without causing double/invalid free.
   *
   * @param key pointer to target option key
   * @param data pointer to target option value
   * @return ISC_STS ISC_STS_OK if no error
   */
  ISC_STS setOpt(const char *key, void *data);

  void *getOpt(const char *key);

 protected:
  SletType type;
  SletOpts opt;

  size_t szStartup;
  size_t szShutdown;
};

// just a class for convenience
class GenericAPP : public GenericSlet {
 public:
  GenericAPP() : GenericSlet(SletType::APP) {}
  ~GenericAPP() {}
};

class GenericFSA : public GenericSlet {
 public:
  struct Ext {
    size_t block;
    size_t slbn;
    size_t len;  // num of blocks
  };

  struct ExtList {
    Ext *exts;
    size_t len;    // how many extents
    size_t bytes;  // bytes for this file (might not align block size)

    ExtList() : exts(nullptr), len(0), bytes(0) {}
  };

  GenericFSA() : GenericSlet(SletType::FSA) {}
  ~GenericFSA() {}

  virtual void *builtin_getInode(uint64_t _ADD_SIM_PARAMS) {
    pr("%s not implemented", __func__);
    return nullptr;
  }

  virtual ExtList builtin_getExt(const char *_ADD_SIM_PARAMS) {
    pr("%s not implemented", __func__);
    return ExtList();
  }
  virtual ExtList builtin_getExtRange(const char *, size_t, size_t) {
    pr("%s not implemented", __func__);
    return ExtList();
  }

 protected:
  size_t szGetExt;
  size_t szGetExtRange;
};

}  // namespace ISC
}  // namespace SimpleSSD

#undef PR_SECTION

#endif  // __SIMPLESSD_ISC_TYPES_HH__
