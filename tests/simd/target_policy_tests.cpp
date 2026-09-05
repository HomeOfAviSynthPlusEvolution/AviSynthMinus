#include <gtest/gtest.h>
#include "avs_simd/target_policy.h"
#include <hwy/targets.h>
#include <hwy/detect_targets.h>
#include <avs/cpuid.h>

namespace {

#if HWY_ARCH_X86

constexpr int kSSE2Flags = CPUF_SSE2;
constexpr int kSSSE3Flags = CPUF_SSE2 | CPUF_SSSE3;
constexpr int kSSE4Flags = kSSSE3Flags | CPUF_SSE4_1 | CPUF_SSE4_2 | CPUF_AES;
constexpr int kAVX2Flags = kSSE4Flags | CPUF_AVX | CPUF_AVX2 | CPUF_FMA3 | CPUF_F16C;
constexpr int kAVX3Flags = kAVX2Flags | CPUF_AVX512F | CPUF_AVX512VL | CPUF_AVX512DQ | CPUF_AVX512BW | CPUF_AVX512CD;
constexpr int kAVX3DLFlags = kAVX3Flags | CPUF_AVX512VBMI;

TEST(TargetPolicyTest, AvsFlagsToHighwayMaskReturnsZeroForNoneOrForce) {
  EXPECT_EQ(avs_simd::AvsFlagsToHighwayMask(0), 0);
  EXPECT_EQ(avs_simd::AvsFlagsToHighwayMask(CPUF_FORCE), 0);
  EXPECT_EQ(avs_simd::AvsFlagsToHighwayMask(CPUF_FPU | CPUF_MMX), 0);
}

TEST(TargetPolicyTest, AvsFlagsToHighwayMaskIdentifiesSSE2AndSSSE3) {
  const int64_t mask_sse2 = avs_simd::AvsFlagsToHighwayMask(kSSE2Flags);
  EXPECT_TRUE((mask_sse2 & HWY_SSE2) != 0);
  EXPECT_FALSE((mask_sse2 & HWY_SSSE3) != 0);
  EXPECT_FALSE((mask_sse2 & HWY_SSE4) != 0);
  EXPECT_FALSE((mask_sse2 & HWY_AVX2) != 0);

  // SSSE3 without SSE2 prerequisite must not qualify
  EXPECT_EQ(avs_simd::AvsFlagsToHighwayMask(CPUF_SSSE3), 0);

  const int64_t mask_ssse3 = avs_simd::AvsFlagsToHighwayMask(kSSSE3Flags);
  EXPECT_TRUE((mask_ssse3 & HWY_SSE2) != 0);
  EXPECT_TRUE((mask_ssse3 & HWY_SSSE3) != 0);
  EXPECT_FALSE((mask_ssse3 & HWY_SSE4) != 0);
}

TEST(TargetPolicyTest, AvsFlagsToHighwayMaskRequiresAllSSE4Components) {
  // SSE4.1 alone without SSE4.2 or AES must not qualify for HWY_SSE4
  const int64_t partial_sse4 = avs_simd::AvsFlagsToHighwayMask(kSSSE3Flags | CPUF_SSE4_1);
  EXPECT_FALSE((partial_sse4 & HWY_SSE4) != 0);

  // SSE4 without AES must not qualify
  const int64_t sse4_no_aes = avs_simd::AvsFlagsToHighwayMask(kSSSE3Flags | CPUF_SSE4_1 | CPUF_SSE4_2);
  EXPECT_FALSE((sse4_no_aes & HWY_SSE4) != 0);

  // Complete SSE4 set
  const int64_t full_sse4 = avs_simd::AvsFlagsToHighwayMask(kSSE4Flags);
  EXPECT_TRUE((full_sse4 & HWY_SSE4) != 0);
}

TEST(TargetPolicyTest, AvsFlagsToHighwayMaskRequiresFMAAndF16CForAVX2) {
  // AVX2 without FMA3
  const int64_t avx2_no_fma = avs_simd::AvsFlagsToHighwayMask(kAVX2Flags & ~CPUF_FMA3);
  EXPECT_FALSE((avx2_no_fma & HWY_AVX2) != 0);
  EXPECT_TRUE((avx2_no_fma & HWY_SSE4) != 0); // Falls back to SSE4 capability

  // AVX2 without F16C
  const int64_t avx2_no_f16c = avs_simd::AvsFlagsToHighwayMask(kAVX2Flags & ~CPUF_F16C);
  EXPECT_FALSE((avx2_no_f16c & HWY_AVX2) != 0);

  // AVX2 without AVX
  const int64_t avx2_no_avx = avs_simd::AvsFlagsToHighwayMask(kAVX2Flags & ~CPUF_AVX);
  EXPECT_FALSE((avx2_no_avx & HWY_AVX2) != 0);

  // Complete AVX2 set
  const int64_t full_avx2 = avs_simd::AvsFlagsToHighwayMask(kAVX2Flags);
  EXPECT_TRUE((full_avx2 & HWY_AVX2) != 0);
}

TEST(TargetPolicyTest, AvsFlagsToHighwayMaskChecksAVX512Components) {
  // AVX512 missing CD
  const int64_t avx3_no_cd = avs_simd::AvsFlagsToHighwayMask(kAVX3Flags & ~CPUF_AVX512CD);
  EXPECT_FALSE((avx3_no_cd & HWY_AVX3) != 0);
  EXPECT_TRUE((avx3_no_cd & HWY_AVX2) != 0);

  // AVX512 missing VL
  const int64_t avx3_no_vl = avs_simd::AvsFlagsToHighwayMask(kAVX3Flags & ~CPUF_AVX512VL);
  EXPECT_FALSE((avx3_no_vl & HWY_AVX3) != 0);

  // Complete AVX512 foundation set
  const int64_t full_avx3 = avs_simd::AvsFlagsToHighwayMask(kAVX3Flags);
  EXPECT_TRUE((full_avx3 & HWY_AVX3) != 0);

  // AVX3_DL requires VBMI
  const int64_t full_avx3_dl = avs_simd::AvsFlagsToHighwayMask(kAVX3DLFlags);
  EXPECT_TRUE((full_avx3_dl & HWY_AVX3_DL) != 0);
  // AVX10.2 has no AVS-specific flag. It is allowed only at the strongest
  // AVS-visible tier; Highway still verifies its additional CPUID features.
  EXPECT_TRUE((full_avx3_dl & HWY_AVX10_2) != 0);
}

#endif // HWY_ARCH_X86

TEST(TargetPolicyTest, PureChooseTargetRespectsThreeSetIntersection) {
#if HWY_ARCH_X86
  const int64_t hw = HWY_AVX2 | HWY_SSE4 | HWY_SSE2;
  const int64_t compiled = HWY_AVX2 | HWY_SSE2; // SSE4 was NOT compiled in this TU

  // 1. AVS allows AVX2 -> intersection has AVX2 -> selects AVX2
  EXPECT_EQ(avs_simd::ChooseTarget(kAVX2Flags, hw, compiled), HWY_AVX2);

  // 2. AVS allows SSE4 only -> HW has SSE4, but compiled lacks SSE4 -> falls back to SSE2
  EXPECT_EQ(avs_simd::ChooseTarget(kSSE4Flags, hw, compiled), HWY_SSE2);

  // 3. AVS allows SSE2 only -> selects SSE2
  EXPECT_EQ(avs_simd::ChooseTarget(kSSE2Flags, hw, compiled), HWY_SSE2);

  // 4. AVS SetMaxCPU("none") (0) -> returns C fallback
  EXPECT_EQ(avs_simd::ChooseTarget(0, hw, compiled), avs_simd::TARGET_C_FALLBACK);

  // 5. Hardware lacks AVX2
  const int64_t hw_no_avx2 = HWY_SSE4 | HWY_SSE2;
  EXPECT_EQ(avs_simd::ChooseTarget(kAVX2Flags, hw_no_avx2, compiled), HWY_SSE2);

  // 6. No overlapping targets at all -> returns C fallback
  EXPECT_EQ(avs_simd::ChooseTarget(kAVX2Flags, HWY_AVX3, compiled), avs_simd::TARGET_C_FALLBACK);

  // 7. AVX10.2 participates when the AVS-visible AVX-512 tier permits it.
  EXPECT_EQ(avs_simd::ChooseTarget(kAVX3DLFlags, HWY_AVX10_2, HWY_AVX10_2), HWY_AVX10_2);
#else
  EXPECT_EQ(avs_simd::ChooseTarget(0, HWY_TARGETS, HWY_TARGETS), avs_simd::TARGET_C_FALLBACK);
  // Simulated masks verify policy without executing a foreign instruction set.
  EXPECT_EQ(avs_simd::ChooseTarget(CPUF_FORCE, HWY_NEON | HWY_SVE, HWY_NEON), HWY_NEON);
  EXPECT_EQ(avs_simd::ChooseTarget(CPUF_FORCE, HWY_NEON, HWY_SVE), avs_simd::TARGET_C_FALLBACK);
  EXPECT_EQ(avs_simd::ChooseTarget(CPUF_FORCE, HWY_SCALAR | HWY_EMU128,
                                  HWY_SCALAR | HWY_EMU128), avs_simd::TARGET_C_FALLBACK);
#endif
}

TEST(TargetPolicyTest, TargetNameFormatting) {
  EXPECT_STREQ(avs_simd::TargetName(avs_simd::TARGET_C_FALLBACK), "C");
#if HWY_ARCH_X86
  EXPECT_STREQ(avs_simd::TargetName(HWY_SSE2), "SSE2");
  EXPECT_STREQ(avs_simd::TargetName(HWY_AVX2), "AVX2");
#endif
}

} // namespace
