#ifndef __SIMPLESSD_ISC_SLET_SEQREAD_HH__
#define __SIMPLESSD_ISC_SLET_SEQREAD_HH__

#include "sims/cpu.hh"

#include "types.hh"

namespace SimpleSSD {
namespace ISC {

class SeqReadAPP : public GenericAPP {
 public:
  SeqReadAPP(_SIM_PARAMS);
  ~SeqReadAPP() {}

  ISC_STS builtin_startup(_SIM_PARAMS) override;

  static const char* keyPath;
  static const char* keyResult;
};

}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SLET_SEQREAD_HH__ */