#include "avs_simd/target_policy.h"
#include <hwy/targets.h>
#include <hwy/detect_targets.h>
#include <avs/cpuid.h>

namespace avs_simd {

int64_t AvsArmFlagsToHighwayMask(int64_t flags, bool apple) {
  if (!(flags & CPUF_ARM_NEON))
    return 0;
  int64_t mask = HWY_NEON | HWY_NEON_WITHOUT_AES;
  // The Apple NEON_BF16 target additionally enables I8MM in Highway 1.4.
  if ((flags & CPUF_ARM_DOTPROD) && (!apple || (flags & CPUF_ARM_I8MM)))
    mask |= HWY_NEON_BF16;
  // AVS has no SVE1 bit: conservatively gate all SVE on SVE2 permission.
  if (flags & CPUF_ARM_SVE2) {
    mask |= HWY_SVE | HWY_SVE_256 | HWY_SVE2;
    // Highway's specialized SVE2_128 target enables I8MM and BF16.
    // Hardware detection checks SVE I8MM/BF16 and actual vector length.
    if (flags & CPUF_ARM_I8MM)
      mask |= HWY_SVE2_128;
  }
  // SVE2.1 has no separate Highway target and cannot enable missing lower bits.
  return mask;
}

int64_t AvsFlagsToHighwayMask(int64_t avs_cpu_flags) {
#if HWY_ARCH_X86
  int64_t mask = 0;

  // SSE2
  if ((avs_cpu_flags & CPUF_SSE2) == CPUF_SSE2) {
    mask |= HWY_SSE2;
  }

  // SSSE3: requires SSE2 + SSSE3
  constexpr int64_t kReqSSSE3 = CPUF_SSE2 | CPUF_SSSE3;
  if ((avs_cpu_flags & kReqSSSE3) == kReqSSSE3) {
    mask |= HWY_SSSE3;
  }

  // SSE4: Highway requires SSE4.1, SSE4.2, AES, CLMUL, plus SSSE3.
  // AVS exposes CPUF_SSE4_1, CPUF_SSE4_2, CPUF_AES (CLMUL is checked by Highway hardware detection).
  constexpr int64_t kReqSSE4 = kReqSSSE3 | CPUF_SSE4_1 | CPUF_SSE4_2 | CPUF_AES;
  if ((avs_cpu_flags & kReqSSE4) == kReqSSE4) {
    mask |= HWY_SSE4;
  }

  // AVX2: Highway requires AVX, AVX2, FMA, F16C, plus SSE4 group.
  // (BMI, BMI2, LZCNT are not exposed by AVS and checked by Highway hardware detection).
  constexpr int64_t kReqAVX2 = kReqSSE4 | CPUF_AVX | CPUF_AVX2 | CPUF_FMA3 | CPUF_F16C;
  if ((avs_cpu_flags & kReqAVX2) == kReqAVX2) {
    mask |= HWY_AVX2;
  }

  // AVX3 (AVX-512 foundation): Highway requires AVX-512F, VL, DQ, BW, CD, plus AVX2 group.
  constexpr int64_t kReqAVX3 = kReqAVX2 | CPUF_AVX512F | CPUF_AVX512VL | CPUF_AVX512DQ | CPUF_AVX512BW | CPUF_AVX512CD;
  if ((avs_cpu_flags & kReqAVX3) == kReqAVX3) {
    mask |= HWY_AVX3;
  }

  // Respect every V12-visible prerequisite; Highway checks the remaining
  // hardware/OS requirements (including crypto extensions).
  constexpr int64_t kReqAVX3_DL = kReqAVX3 | CPUF_AVX512VBMI |
      CPUF_AVX512VNNI | CPUF_AVX512VBMI2 | CPUF_AVX512BITALG | CPUF_AVX512VPOPCNTDQ;
  if ((avs_cpu_flags & kReqAVX3_DL) == kReqAVX3_DL) {
    mask |= HWY_AVX3_DL;
    if (avs_cpu_flags & CPUF_AVX512BF16) {
      mask |= HWY_AVX3_ZEN4;
      if (avs_cpu_flags & CPUF_AVX512FP16)
        mask |= HWY_AVX3_SPR | HWY_AVX10_2;
    }
  }

  return mask;
#elif HWY_ARCH_ARM
  return AvsArmFlagsToHighwayMask(avs_cpu_flags, HWY_OS_APPLE != 0);
#else
  // Non-x86 SetMaxCPU records explicit "none" as zero; other settings and
  // the default retain native dispatch. Highway checks hardware support.
  return avs_cpu_flags == 0 ? 0 : ~int64_t{0};
#endif
}

int64_t ChooseTarget(int64_t avs_cpu_flags, int64_t hardware_supported, int64_t generated_targets) {
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

int64_t ChooseTarget(int64_t avs_cpu_flags, int64_t generated_targets) {
  return ChooseTarget(avs_cpu_flags, GetHardwareSupportedTargets(), generated_targets);
}

const char* TargetName(int64_t target) {
  if (target == TARGET_C_FALLBACK) {
    return "C";
  }
  return hwy::TargetName(target);
}

} // namespace avs_simd
