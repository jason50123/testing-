#ifndef __SIMPLESSD_ISC_SIM_DRAM_HH__
#define __SIMPLESSD_ISC_SIM_DRAM_HH__

#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "sims/cpu.hh"

namespace SimpleSSD {
namespace ISC {
namespace SIM {

class DRAM {
#ifndef ISC_TEST
 protected:
  static void *pDRAM;

 public:
  static void setDRAM(void *dram) { pDRAM = dram; }
#endif

 public:
  DRAM() = delete;

  enum TYPES {
    NORMAL,
    LRU_CACHE,
  };

  class Region {
   public:
    typedef int (*Cmp_t)(const void *, const void *, size_t);
    typedef void *(*Cpy_t)(void *, const void *, size_t);

   protected:
    char *addr;
    size_t nmem;
    size_t unit;
    size_t size;

    Cmp_t cmp;
    Cpy_t cpin, cpout;

   public:
    virtual ~Region() { free(addr); }
    Region(void *a, size_t n, size_t u, Cmp_t cm, Cpy_t cpi, Cpy_t cpo)
        : addr((char *)a),
          nmem(n),
          unit(u),
          size(n * u),
          cmp(cm),
          cpin(cpi),
          cpout(cpo) {}
    Region() : Region(nullptr, 0, 0, nullptr, memcpy, memcpy) {}

    size_t getSize() { return size; }

    virtual int read(size_t, size_t sz, void *data _ADD_SIM_PARAMS) = 0;
    virtual int write(size_t, size_t sz, void *data _ADD_SIM_PARAMS) = 0;
  };

  static void destroy();

  static Region *alloc(size_t, size_t, TYPES = TYPES::NORMAL,
                       Region::Cmp_t = memcmp, Region::Cpy_t = memcpy,
                       Region::Cpy_t = memcpy);
  static void dealloc(Region *);

  static inline void read(void *data, size_t sz _ADD_SIM_PARAMS);
  static inline void write(void *data, size_t sz _ADD_SIM_PARAMS);
};

}  // namespace SIM
}  // namespace ISC
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_SIM_DRAM_HH__ */