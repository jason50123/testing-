#ifndef __SIMPLESSD_ISC_SLET_MD5_HH__
#define __SIMPLESSD_ISC_SLET_MD5_HH__

#include "sims/cpu.hh"

#include "types.hh"

namespace SimpleSSD {
namespace ISC {

#define ISC_APP_CLASS MD5APP
class ISC_APP_CLASS : public GenericAPP {
 public:
  ISC_APP_CLASS(_SIM_PARAMS);
  ~ISC_APP_CLASS() {}

  struct result_t {
    uint8_t data[16];
  };

  void md5sum(uint8_t *, uint32_t, uint32_t[4] _ADD_SIM_PARAMS);
  ISC_STS builtin_startup(_SIM_PARAMS) override;

  static const char *keyNumFiles;
  static const char *keyFileSizes;
  static const char *keyExts;

  static const char *keyPath;
  static const char *keyResult;
  static const char *keyResultSize;

  static const size_t BLK_SIZE = 4096;
  static constexpr size_t BYTES_PER_RESULT = sizeof(result_t);

 protected:
};
#undef ISC_APP_CLASS

}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SLET_MD5_HH__ */