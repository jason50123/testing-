#ifndef __SIMPLESSD_ISC_SLET_STATDIR_HH__
#define __SIMPLESSD_ISC_SLET_STATDIR_HH__

#include "sims/cpu.hh"

#include "types.hh"

namespace SimpleSSD {

namespace ISC {

class StatdirAPP : public GenericAPP {
  typedef struct {
    uint32_t mtime;
    uint32_t size;
    uint32_t mode;
    char data[256];
  } data_t;

 public:
  StatdirAPP(_SIM_PARAMS);
  ~StatdirAPP() {}

  ISC_STS builtin_startup(_SIM_PARAMS) override;

  size_t inodeFilter(const char *, const char *, size_t,
                     data_t *_ADD_SIM_PARAMS);

  static const size_t BLK_SIZE = 4096;
  static const char *keyPath;
  static const char *keyResult;
  static const char *keyResultSize;
};

}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SLET_STATDIR_HH__ */