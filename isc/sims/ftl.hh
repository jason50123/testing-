#ifndef __SIMPLESSD_ISC_SIM_FTL_HH__
#define __SIMPLESSD_ISC_SIM_FTL_HH__

#include <unistd.h>

#include <cstdint>
#include <cstdlib>

#ifdef ISC_TEST
#include "sims/cpu.hh"
#else
#include "isc/sims/cpu.hh"
#endif

#ifdef ISC_TEST
#endif

namespace SimpleSSD {
namespace ISC {
namespace SIM {

class FTL {
 protected:
  static size_t lbaSize;
  static char *pathFilesystemImg;
  static void *cache;

 public:
  FTL() = delete;

  static void setImage(const char *p, size_t = 512);
  static void setCache(void *pICL) { FTL::cache = pICL; }

  static void destory() {
    free(pathFilesystemImg);
    pathFilesystemImg = nullptr;
  }
void  setCurrentUid(uint32_t uid);
#ifndef ISC_TEST
  static void read(void *, size_t, size_t);
#endif
  static void read(void *, size_t, size_t _ADD_SIM_PARAMS);
};
  void setCurrentUid(uint32_t uid);
  
}  // namespace SIM
}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SIM_FTL_HH__ */