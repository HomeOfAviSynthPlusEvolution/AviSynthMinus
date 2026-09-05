#include "avs_simd/target_policy.h"
#include <hwy/targets.h>
#include <hwy/detect_targets.h>
#include <avs/cpuid.h>

namespace avs_simd {

int64_t AvsFlagsToHighwayMask(int avs_cpu_flags) {
#if HWY_ARCH_X86
  int64_t mask = 0;

  // SSE2
  if ((avs_cpu_flags & CPUF_SSE2) == CPUF_SSE2) {
    mask |= HWY_SSE2;
  }

  // SSSE3: requires SSE2 + SSSE3
  constexpr int kReqSSSE3 = CPUF_SSE2 | CPUF_SSSE3;
  if ((avs_cpu_flags & kReqSSSE3) == kReqSSSE3) {
    mask |= HWY_SSSE3;
  }

  // SSE4: Highway requires SSE4.1, SSE4.2, AES, CLMUL, plus SSSE3.
  // AVS exposes CPUF_SSE4_1, CPUF_SSE4_2, CPUF_AES (CLMUL is checked by Highway hardware detection).
  constexpr int kReqSSE4 = kReqSSSE3 | CPUF_SSE4_1 | CPUF_SSE4_2 | CPUF_AES;
  if ((avs_cpu_flags & kReqSSE4) == kReqSSE4) {
    mask |= HWY_SSE4;
  }

  // AVX2: Highway requires AVX, AVX2, FMA, F16C, plus SSE4 group.
  // (BMI, BMI2, LZCNT are not exposed by AVS and checked by Highway hardware detection).
  constexpr int kReqAVX2 = kReqSSE4 | CPUF_AVX | CPUF_AVX2 | CPUF_FMA3 | CPUF_F16C;
  if ((avs_cpu_flags & kReqAVX2) == kReqAVX2) {
    mask |= HWY_AVX2;
  }

  // AVX3 (AVX-512 foundation): Highway requires AVX-512F, VL, DQ, BW, CD, plus AVX2 group.
  constexpr int kReqAVX3 = kReqAVX2 | CPUF_AVX512F | CPUF_AVX512VL | CPUF_AVX512DQ | CPUF_AVX512BW | CPUF_AVX512CD;
  if ((avs_cpu_flags & kReqAVX3) == kReqAVX3) {
    mask |= HWY_AVX3;
  }

  // AVX3_DL: Highway requires AVX3 group plus VBMI, etc.
  // AVS exposes CPUF_AVX512VBMI.
  constexpr int kReqAVX3_DL = kReqAVX3 | CPUF_AVX512VBMI;
  if ((avs_cpu_flags & kReqAVX3_DL) == kReqAVX3_DL) {
    mask |= HWY_AVX3_DL;
    // AVX3_ZEN4 and AVX3_SPR require additional features (BF16, FP16)
    // which are not exposed in AVS CPU flags; Highway hardware detection checks them.
    mask |= HWY_AVX3_ZEN4;
    mask |= HWY_AVX3_SPR;

    // AVS has no AVX10/APX flags. Requiring the strongest AVS-visible x86
    // tier prevents SetMaxCPU("avx2") from accidentally selecting AVX10.2;
    // Highway's hardware check still verifies the AVX10-specific features.
    mask |= HWY_AVX10_2;
  }

  return mask;
#else
  // Non-x86 SetMaxCPU records explicit "none" as zero; other settings and
  // the default retain native dispatch. Highway checks hardware support.
  return avs_cpu_flags == 0 ? 0 : ~int64_t{0};
#endif
}

int64_t ChooseTarget(int avs_cpu_flags, int64_t hardware_supported, int64_t generated_targets) {
  const int64_t allowed_by_avs = AvsFlagsToHighwayMask(avs_cpu_flags);
  constexpr int64_t kSimdMask = ~(HWY_SCALAR | HWY_EMU128);
  const int64_t candidates = hardware_supported & allowed_by_avs & generated_targets & kSimdMask;
  if (candidates == 0) {
    return TARGET_C_FALLBACK;
  }
  // In Highway, targets with lower bit indices represent higher priority / capability.
  // The least significant 1-bit selects the highest priority candidate:
  return candidates & (-candidates);
}

int64_t GetHardwareSupportedTargets() {
  return hwy::SupportedTargets();
}

int64_t ChooseTarget(int avs_cpu_flags, int64_t generated_targets) {
  return ChooseTarget(avs_cpu_flags, GetHardwareSupportedTargets(), generated_targets);
}

const char* TargetName(int64_t target) {
  if (target == TARGET_C_FALLBACK) {
    return "C";
  }
  return hwy::TargetName(target);
}

} // namespace avs_simd
