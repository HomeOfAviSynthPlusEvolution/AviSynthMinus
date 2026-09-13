#pragma once

#include "support/compat_375.h"
#include "filters/merge.h"
#include "filters/intel/merge_sse.h"
#include "filters/intel/merge_avx2.h"

namespace avsut::test {

// Adapt test sample counts and normalized 15-bit weights to the release API.
// The scalar integer kernel uses 16-bit weights; the SIMD kernels use 15-bit.
enum class LegacyMergeIsa { Scalar, Sse2, Avx2 };
using MergeFuncPtr = void (*)(BYTE*, const BYTE*, int, int, int, int, int, int, int);
using MergeFloatFuncPtr = void (*)(BYTE*, const BYTE*, int, int, int, int, float);

template <LegacyMergeIsa isa>
void legacy_merge_integer(BYTE* dst, const BYTE* src, int dp, int sp, int width, int height,
                          int weight, int inverse, int bits) {
  const int row_size = width * (bits == 8 ? 1 : 2);
  if constexpr (isa == LegacyMergeIsa::Scalar) {
    if (bits == 8)
      weighted_merge_planar_c<uint8_t>(dst, src, dp, sp, row_size, height, 0, weight * 2,
                                       inverse * 2);
    else
      weighted_merge_planar_c<uint16_t>(dst, src, dp, sp, row_size, height, 0, weight * 2,
                                        inverse * 2);
  } else if constexpr (isa == LegacyMergeIsa::Sse2) {
    if (bits == 8)
      weighted_merge_planar_sse2(dst, src, dp, sp, row_size, height, 0, weight, inverse);
    else if (bits < 16)
      weighted_merge_planar_uint16_sse2<true>(dst, src, dp, sp, row_size, height, 0, weight,
                                              inverse);
    else
      weighted_merge_planar_uint16_sse2<false>(dst, src, dp, sp, row_size, height, 0, weight,
                                               inverse);
  } else {
    if (bits == 8)
      weighted_merge_planar_avx2(dst, src, dp, sp, row_size, height, 0, weight, inverse);
    else if (bits < 16)
      weighted_merge_planar_uint16_avx2<true>(dst, src, dp, sp, row_size, height, 0, weight,
                                              inverse);
    else
      weighted_merge_planar_uint16_avx2<false>(dst, src, dp, sp, row_size, height, 0, weight,
                                               inverse);
  }
}

template <LegacyMergeIsa isa>
void legacy_merge_float(BYTE* dst, const BYTE* src, int dp, int sp, int width, int height,
                        float weight) {
  static_assert(isa != LegacyMergeIsa::Avx2, "release has no float AVX2 weighted merge");
  if constexpr (isa == LegacyMergeIsa::Scalar)
    weighted_merge_planar_c_float(dst, src, dp, sp, width * sizeof(float), height, weight, 0, 0);
  else
    weighted_merge_planar_sse2_float(dst, src, dp, sp, width * sizeof(float), height, weight, 0, 0);
}

}  // namespace avsut::test
