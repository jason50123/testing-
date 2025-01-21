#include <assert.h>

#include "utils/types.hh"

namespace SimpleSSD {
namespace Utils {

bool testBitmap(bitmap_t bm, uint32_t bit) {
  size_t b = bit >> 3;
  switch (bit & 0b111) {
    case 0:
      return bm[b].bit0;
    case 1:
      return bm[b].bit1;
    case 2:
      return bm[b].bit2;
    case 3:
      return bm[b].bit3;
    case 4:
      return bm[b].bit4;
    case 5:
      return bm[b].bit5;
    case 6:
      return bm[b].bit6;
    case 7:
      return bm[b].bit7;
  }
  assert(!"Not Expected to be here");
  return 0;
}

}  // namespace Utils
}  // namespace SimpleSSD
