#ifndef __SIMPLESSD_ISC_UTILS_MATH_HH__
#define __SIMPLESSD_ISC_UTILS_MATH_HH__

#include <cstddef>

namespace SimpleSSD {
namespace Utils {

#define CAT3232(hi, lo) ((ssize_t)(hi) << 32 | (lo))
#define CAT3232U(hi, lo) ((size_t)(hi) << 32 | (lo))
#define DIV64_CEIL(x, y) ((((size_t)(x)) - 1) / ((size_t)(y)) + 1)

#define _MIN32U(x, y) (x <= y ? x : y)
#define MIN32U(x, y) (_MIN32U((x), (y)))

}  // namespace Utils
}  // namespace SimpleSSD

#endif /* __SIMPLESSD_ISC_UTILS_MATH_HH__ */