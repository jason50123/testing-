#ifndef __SIMPLESSD_ISC_SLET_RANDREAD_HH__
#define __SIMPLESSD_ISC_SLET_RANDREAD_HH__

#include "sims/cpu.hh"

#include "types.hh"

namespace SimpleSSD {
namespace ISC {

/**
 * @brief Single threaded random reads on a 1GB file. Stops after predefined
 * amount of data has been read.​
 */
class RandReadAPP : public GenericAPP {
 public:
  struct Work {
    size_t szFile;
    size_t szEachRead;
    size_t szTotal;
    size_t numIO;
  };

  RandReadAPP(_SIM_PARAMS);
  ISC_STS builtin_startup(_SIM_PARAMS) override;

  static const char *keyPath;
  static const char *keyOffsets;
  static const char *keyResult;
  static const char *keyConf;
};

}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SLET_RANDREAD_HH__ */