#ifndef __SIMPLESSD_ISC_SLET_STATS32_HH__
#define __SIMPLESSD_ISC_SLET_STATS32_HH__

#include "sims/cpu.hh"

#include "types.hh"

namespace SimpleSSD {
namespace ISC {

class Stats32APP : public GenericAPP {
 public:
  Stats32APP(_SIM_PARAMS);
  ~Stats32APP() {}

  struct result_t {
    int64_t sum;
    int32_t min;
    int32_t max;
  };

  static constexpr size_t BYTES_PER_RESULT = sizeof(result_t);

  ISC_STS sum(const int32_t *, size_t, result_t *_ADD_SIM_PARAMS);

  ISC_STS builtin_startup(_SIM_PARAMS) override;

  static const size_t BLK_SIZE = 4096;
  static const char *keyNumFiles;
  static const char *keyFileSizes;
  static const char *keyExts;
  static const char *keyPath;
  static const char *keyResult;
  static const char *keyResultSize;
};

}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SLET_STATS32_HH__ */