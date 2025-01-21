#ifndef __SIMPLESSD_ISC_SLET_GREP_HH__
#define __SIMPLESSD_ISC_SLET_GREP_HH__

#include "sims/cpu.hh"

#include "types.hh"

namespace SimpleSSD {
namespace ISC {

class GrepAPP : public GenericAPP {
 public:
  GrepAPP(_SIM_PARAMS);
  ~GrepAPP() {}

  struct result_t {
    char *line;
    size_t len;
  };

  int strstr(const char *, size_t, const char *, size_t _ADD_SIM_PARAMS);
  ISC_STS grep(const char *, size_t, const char *, void *_ADD_SIM_PARAMS);
  ISC_STS builtin_startup(_SIM_PARAMS) override;

  static const char *keyNumFiles;
  static const char *keyFileSizes;
  static const char *keyExts;

  static const char *keyPath;
  static const char *keyPatt;
  static const char *keyResult;
  static const char *keyResultSize;

  const size_t BLK_SIZE = 4096;

 protected:
};

}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SLET_GREP_HH__ */