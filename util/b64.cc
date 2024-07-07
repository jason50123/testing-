#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

namespace SimpleSSD {

#ifdef DEBUG
#define pr_d(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#else
#define pr_d(...)
#endif

static const char B64MAP[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "abcdefghijklmnopqrstuvwxyz"
                             "0123456789+/";

// clang-format off
static const uint8_t B64UNMAP[] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 0 ~ 42: empty
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 19
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 29
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    62,                                                 // 43 ('+') = 62
    255, 255, 255,                                      // 44 ~ 46: empty
    63,                                                 // 47 ('/') = 63
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61,             // 48 ('0') ~ 57 ('9'): 52 ~ 61
    255, 255, 255, 255, 255, 255, 255,                  // 58 ~ 64: empty
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, // 65 ('A') ~ 90 ('Z')
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, // = 0 ~ 25
    255, 255, 255, 255, 255, 255,                       // 91 ~ 97: empty
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, // 98 ('a') ~ 122 ('z')
    39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, // = 26 ~ 51
};
// clang-format on

typedef union {
  uint32_t in32;
  uint8_t in8[4];
} B64_t;

void encode(const char *in, size_t isz, char **out, size_t *osz) {
  int trailing = isz % 3;
  int nPad = trailing ? 3 - trailing : 0;
  size_t groups = isz / 3 + !!trailing;
  pr_d("G: %lu + T(P): %d(%d)", groups, trailing, nPad);

  // allocate output buffer
  *osz = (isz + nPad) / 3 * 4;
  *out = (char *)calloc(1, *osz);
  if (!*out)
    return;

  // convert every 3 in chars to 4 out chars
  B64_t input;
  for (size_t g = 0, io = 0, oo = 0; g < groups; ++g, io += 3, oo += 4) {
    input.in8[0] = io + 2 >= isz ? 0 : in[io + 2];  // fixme:
    input.in8[1] = io + 1 >= isz ? 0 : in[io + 1];  // fixme:
    input.in8[2] = in[io];
    input.in8[3] = 0;
    // in32 = ((u32)in[io] << 16) | ((u32)in[io + 1] << 8) | in[io + 2];

    pr_d("0x%X", input.in32);
    (*out)[oo + 0] = B64MAP[((input.in32 & 0xFC0000) >> 18) & 0x3F];
    (*out)[oo + 1] = B64MAP[((input.in32 & 0x3F000) >> 12) & 0x3F];
    (*out)[oo + 2] = B64MAP[((input.in32 & 0xFC0) >> 6) & 0x3F];
    (*out)[oo + 3] = B64MAP[input.in32 & 0x3F];
  }

  // may need to pad '=' to output
  for (int iPad = 0; iPad < nPad; ++iPad)
    (*out)[*osz - 1 - iPad] = '=';
}

void decode(const char *in, size_t isz, char **out, size_t *osz) {
  size_t groups = isz / 4;

  // alloc output buffer
  *osz = isz / 4 * 3;
  *out = (char *)calloc(1, *osz);
  if (!*out)
    return;

  // decode every 4 char to 3 char
  B64_t input;
  for (size_t g = 0, io = 0, oo = 0; g < groups; ++g, io += 4, oo += 3) {
    input.in32 = (B64UNMAP[(uint8_t)in[io + 0]] & 0x3F) << 18;
    input.in32 |= (B64UNMAP[(uint8_t)in[io + 1]] & 0x3F) << 12;
    input.in32 |= (B64UNMAP[(uint8_t)in[io + 2]] & 0x3F) << 6;
    input.in32 |= (B64UNMAP[(uint8_t)in[io + 3]] & 0x3F);

    (*out)[oo + 2] = input.in8[0];
    (*out)[oo + 1] = input.in8[1];
    (*out)[oo + 0] = input.in8[2];
  }

  // last 2 char may be padding
  *osz -= !!(in[isz - 1] == '=');
  *osz -= !!(in[isz - 2] == '=');
}

}  // namespace SimpleSSD
