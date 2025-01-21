#ifndef __SIMPLESSD_ISC_SLET_LISTDIR_HH__
#define __SIMPLESSD_ISC_SLET_LISTDIR_HH__

#include "sims/cpu.hh"

#include "types.hh"

namespace SimpleSSD {
namespace ISC {

class ListdirAPP : public GenericAPP {
 public:
  ListdirAPP(_SIM_PARAMS);
  ~ListdirAPP() {}

  ISC_STS builtin_startup(_SIM_PARAMS) override;

  static const char *keyPath;
  static const char *keyResult;
  static const char *keyResultSize;
};

}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SLET_LISTDIR_HH__ */