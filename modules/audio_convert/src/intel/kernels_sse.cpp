// Avisynth+
// https://avs-plus.net
//
// This file is part of Avisynth+ which is released under GPL2+ with exception.

// Convert Audio helper functions (SSE2/SSSE3)
// Copyright (c) 2020 Xinyue Lu, (c) 2021 pinterf

#include "../kernels.h"
#include <avs/types.h>
#include <avs/config.h>
#include <smmintrin.h> // SSE4.1 at most
#include <cmath>

#if defined(GCC) || defined(CLANG)
  #define SSE2 __attribute__((__target__("sse2")))
  #define SSSE3 __attribute__((__target__("ssse3")))
  #define SSE41 __attribute__((__target__("sse4.1")))
#else
  #define SSE2
  #define SSSE3
  #define SSE41
#endif

// Float: 8-FLT, FLT-8, 16-FLT, FLT-16, 32-FLT, FLT-32

SSE41 void convert8ToFLT_SSE41(void* inbuf, void* outbuf, int count) {
  auto in = reinterpret_cast<uint8_t*>(inbuf);
  auto out = reinterpret_cast<SFLOAT*>(outbuf);
  constexpr float divisor = 1.0f / 128.f; // 1 << 7

  const int c_loop = count & ~3;

  for (int i = c_loop; i < count; i++)
    out[i] = (in[i] - 128) * divisor;

  __m128 divv = _mm_set1_ps(divisor);
  for (int i = 0; i < c_loop; i += 4) {
    __m128i in32 = _mm_cvtepu8_epi32(_mm_castps_si128(_mm_load_ss(reinterpret_cast<float *>(in)))); in += 4;
    in32 = _mm_sub_epi32(in32, _mm_set1_epi32(128));
    __m128 infl = _mm_cvtepi32_ps(in32);
    __m128 outfl = _mm_mul_ps(infl, divv);
    _mm_storeu_ps(out, outfl); out += 4;
  }
}

SSE2 void convertFLTTo8_SSE2(void* inbuf, void* outbuf, int count) {
  auto in = reinterpret_cast<SFLOAT*>(inbuf);
  auto out = reinterpret_cast<uint8_t*>(outbuf);
  constexpr float multiplier = 128.f;
  constexpr float max8 = 127.f;
  constexpr float min8 = -128.f;

  const int c_loop = count & ~3;

  for (int i = c_loop; i < count; i++) {
    float val = in[i] * multiplier;
    uint8_t result;
    if (std::isnan(val)) result = 128;
    else if (val >= max8) result = 255;
    else if (val <= min8) result = 0;
    else result = static_cast<int8_t>(val) + 128;
    out[i] = result;
  }

  __m128 mulv = _mm_set1_ps(multiplier);
  __m128 maxv = _mm_set1_ps(max8);
  __m128 minv = _mm_set1_ps(min8);
  for (int i = 0; i < c_loop; i += 4) {
    __m128 infl = _mm_loadu_ps(in); in += 4;
    infl = _mm_and_ps(infl, _mm_cmpord_ps(infl, infl)); // NaN becomes silence.
    __m128 outfl = _mm_max_ps(minv, _mm_min_ps(maxv,_mm_mul_ps(infl, mulv)));
    __m128i out32 = _mm_cvttps_epi32(outfl);
    __m128i out16 = _mm_packs_epi32(out32, out32);
    __m128i out8 = _mm_packs_epi16(out16, out16);
    out8 = _mm_add_epi8(out8, _mm_set1_epi8(-128)); // 128
    *(uint32_t *)(out) = _mm_cvtsi128_si32(out8); out += 4;
  }
}

SSE41 void convert16ToFLT_SSE41(void* inbuf, void* outbuf, int count) {
  auto in = reinterpret_cast<int16_t*>(inbuf);
  auto out = reinterpret_cast<SFLOAT*>(outbuf);
  constexpr float divisor = 1.0f / 32768.f; // 1 << 15

  const int c_loop = count & ~3;

  for (int i = c_loop; i < count; i++)
    out[i] = in[i] * divisor;

  __m128 divv = _mm_set1_ps(divisor);
  for (int i = 0; i < c_loop; i += 4) {
    __m128i in32 = _mm_cvtepi16_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(in))); in += 4;
    __m128 infl = _mm_cvtepi32_ps(in32);
    __m128 outfl = _mm_mul_ps(infl, divv);
    _mm_storeu_ps(out, outfl); out += 4;
  }
}

SSE2 void convertFLTTo16_SSE2(void* inbuf, void* outbuf, int count) {
  auto in = reinterpret_cast<SFLOAT*>(inbuf);
  auto out = reinterpret_cast<int16_t*>(outbuf);
  constexpr float multiplier = 32768.f;
  constexpr float max16 = 32767.f;
  constexpr float min16 = -32768.f;

  const int c_loop = count & ~3;

  for (int i = c_loop; i < count; i++) {
    float val = in[i] * multiplier;
    int16_t result;
    if (std::isnan(val)) result = 0;
    else if (val >= max16) result = 32767;
    else if (val <= min16) result = (int16_t)-32768;
    else result = static_cast<int16_t>(val);
    out[i] = result;
  }

  __m128 mulv = _mm_set1_ps(multiplier);
  __m128 maxv = _mm_set1_ps(max16);
  __m128 minv = _mm_set1_ps(min16);
  for (int i = 0; i < c_loop; i += 4) {
    __m128 infl = _mm_loadu_ps(in); in += 4;
    infl = _mm_and_ps(infl, _mm_cmpord_ps(infl, infl));
    __m128 outfl = _mm_max_ps(minv, _mm_min_ps(maxv, _mm_mul_ps(infl, mulv)));
    __m128i out32 = _mm_cvttps_epi32(outfl);
    __m128i out16 = _mm_packs_epi32(out32, out32);
    _mm_storel_epi64(reinterpret_cast<__m128i*>(out), out16); out += 4;
  }
}

SSE2 void convert32ToFLT_SSE2(void *inbuf, void *outbuf, int count) {
  auto in = reinterpret_cast<int32_t *>(inbuf);
  auto out = reinterpret_cast<SFLOAT *>(outbuf);
  const float divisor = 1.0f/2147483648.0f;

  const int c_loop = count & ~3;

  for (int i = c_loop; i < count; i++)
    out[i] = in[i] * divisor;

  __m128 divv = _mm_set1_ps(divisor);
  for (int i = 0; i < c_loop; i += 4) {
    __m128i in32 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in)); in += 4;
    __m128 infl = _mm_cvtepi32_ps(in32);
    __m128 outfl = _mm_mul_ps(infl, divv);
    _mm_storeu_ps(out, outfl); out += 4;
  }
}

SSE41 void convertFLTTo32_SSE41(void *inbuf, void *outbuf, int count) {
  auto in = reinterpret_cast<SFLOAT *>(inbuf);
  auto out = reinterpret_cast<int32_t *>(outbuf);
  constexpr float multiplier = 2147483648.0f;
  constexpr float max32 = 2147483647.0f; // 2147483648.0f in reality
  constexpr float min32 = -2147483648.0f;

  const int c_loop = count & ~3;

  for (int i = c_loop; i < count; i++) {
    float val = in[i] * multiplier;
    int32_t result;
    if (std::isnan(val)) result = 0;
    else if (val >= max32) result = 0x7FFFFFFF; // 2147483647
    else if (val <= min32) result = 0x80000000; // -2147483648
    else result = static_cast<int32_t>(val);
    out[i] = result;
  }

  __m128 mulv = _mm_set1_ps(multiplier);
  __m128 maxv = _mm_set1_ps(max32);
  __m128 minv = _mm_set1_ps(min32);
  __m128i maxv_i = _mm_set1_epi32(0x7FFFFFFF); // 2147483647
  __m128i minv_i = _mm_set1_epi32(0x80000000); // -2147483648
  for (int i = 0; i < c_loop; i += 4) {
    __m128 infl = _mm_loadu_ps(in); in += 4;
    infl = _mm_and_ps(infl, _mm_cmpord_ps(infl, infl));
    __m128 outfl = _mm_mul_ps(infl, mulv);
    __m128i cmphigh = _mm_castps_si128(_mm_cmpge_ps(outfl, maxv));
    __m128i cmplow = _mm_castps_si128(_mm_cmpge_ps(minv, outfl));
    __m128i out32 = _mm_cvttps_epi32(outfl);
    out32 = _mm_blendv_epi8(out32, maxv_i, cmphigh);
    out32 = _mm_blendv_epi8(out32, minv_i, cmplow);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(out), out32); out += 4;
  }
}

// Retained for the measured MSVC F32 -> S24 two-stage path.
SSSE3 void convert32To24_SSSE3(void *inbuf, void *outbuf, int count) {
  auto in = reinterpret_cast<int32_t *>(inbuf);
  auto in8 = reinterpret_cast<int8_t *>(inbuf);
  auto out8 = reinterpret_cast<int8_t *>(outbuf);

  const int c_loop = count & ~15;

  for (int i = c_loop; i < count; i++) {
    out8[i * 3 + 0] = in8[i * 4 + 1];
    out8[i * 3 + 1] = in8[i * 4 + 2];
    out8[i * 3 + 2] = in8[i * 4 + 3];
  }

  __m128i inv[4], outv[3], mask[6];
  // clang-format off
  mask[0] = _mm_set_epi8(
    -1, -1, -1, -1,
    15, 14, 13, 11,
    10,  9,  7,  6,
     5,  3,  2,  1);
  mask[1] = _mm_set_epi8(
     5,  3,  2,  1,
    -1, -1, -1, -1,
    -1, -1, -1, -1,
    -1, -1, -1, -1);
  mask[2] = _mm_set_epi8(
    -1, -1, -1, -1,
    -1, -1, -1, -1,
    15, 14, 13, 11,
    10,  9,  7,  6);
  mask[3] = _mm_set_epi8(
    10,  9,  7,  6,
     5,  3,  2,  1,
    -1, -1, -1, -1,
    -1, -1, -1, -1);
  mask[4] = _mm_set_epi8(
    -1, -1, -1, -1,
    -1, -1, -1, -1,
    -1, -1, -1, -1,
    15, 14, 13, 11);
  mask[5] = _mm_set_epi8(
    15, 14, 13, 11,
    10,  9,  7,  6,
     5,  3,  2,  1,
    -1, -1, -1, -1);

  for (int i = 0; i < c_loop; i += 16) {
    inv[0] = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in)); in += 4;
    inv[1] = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in)); in += 4;
    inv[2] = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in)); in += 4;
    inv[3] = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in)); in += 4;

    outv[0] = _mm_or_si128(
      _mm_shuffle_epi8(inv[0], mask[0]),
      _mm_shuffle_epi8(inv[1], mask[1])
    );
    outv[1] = _mm_or_si128(
      _mm_shuffle_epi8(inv[1], mask[2]),
      _mm_shuffle_epi8(inv[2], mask[3])
    );
    outv[2] = _mm_or_si128(
      _mm_shuffle_epi8(inv[2], mask[4]),
      _mm_shuffle_epi8(inv[3], mask[5])
    );

    _mm_storeu_si128(reinterpret_cast<__m128i *>(out8), outv[0]); out8 += 16;
    _mm_storeu_si128(reinterpret_cast<__m128i *>(out8), outv[1]); out8 += 16;
    _mm_storeu_si128(reinterpret_cast<__m128i *>(out8), outv[2]); out8 += 16;
  }
  // clang-format on
}
