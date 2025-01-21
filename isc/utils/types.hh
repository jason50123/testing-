#ifndef __SIMPLESSD_ISC_UTILS_TYPES_HH__
#define __SIMPLESSD_ISC_UTILS_TYPES_HH__

#include <linux/types.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <map>

using std::list;
using std::map;
using std::pair;

#define likely(x) x
#define unlikely(x) x

#if __has_attribute(__nonstring__)
#define __nonstring __attribute__((__nonstring__))
#else
#define __nonstring
#endif

typedef union {
  uint8_t b;
  struct {
    uint8_t bit0 : 1;
    uint8_t bit1 : 1;
    uint8_t bit2 : 1;
    uint8_t bit3 : 1;
    uint8_t bit4 : 1;
    uint8_t bit5 : 1;
    uint8_t bit6 : 1;
    uint8_t bit7 : 1;
  };
} bits_t, *bitmap_t;
static_assert(sizeof(bits_t) == sizeof(uint8_t), "Weird bitmap_t Size");

namespace SimpleSSD {
namespace Utils {

bool testBitmap(bitmap_t bm, uint32_t bit);

}  // namespace Utils
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_UTILS_TYPES_HH__ */