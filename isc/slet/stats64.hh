#ifndef __SIMPLESSD_ISC_SLET_SUM64_HH__
#define __SIMPLESSD_ISC_SLET_SUM64_HH__

#include "sims/cpu.hh"

#include "types.hh"

namespace SimpleSSD {
namespace ISC {

// clang-format won't break after attribute, use this to make do that
#if defined(__clang__)
#define NO_SANS(...) __attribute__((no_sanitize(__VA_ARGS__)))
#define NO_UINT_SANS NO_SANS("unsigned-integer-overflow")
#else
#define NO_SANS(...)
#define NO_UINT_SANS
#endif

class Stats64APP : public GenericAPP {
 public:
  Stats64APP(_SIM_PARAMS);
  ~Stats64APP() {}

  struct result_t {
    uint64_t sum;
    int64_t min;
    int64_t max;
  };

  static constexpr size_t BYTES_PER_RESULT = sizeof(result_t);

  ISC_STS sum(const int64_t *, size_t, result_t *_ADD_SIM_PARAMS);

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

#endif /* __SIMPLESSD_ISC_SLET_SUM64_HH__ */