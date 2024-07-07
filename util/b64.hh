#ifndef __SIMPLESSD_SA_UTIL_ZB64_HH__
#define __SIMPLESSD_SA_UTIL_ZB64_HH__

#include <assert.h>
#include <cstdlib>
#include <cstring>

namespace SimpleSSD {

void encode(const char *in, size_t isz, char **out, size_t *osz);
void decode(const char *in, size_t isz, char **out, size_t *osz);

}  // namespace SimpleSSD

#endif /* __SIMPLESSD_SA_UTIL_ZB64_HH__ */
