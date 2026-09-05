#include "tests/simd/dispatch_probe.h"
#include "avs_simd/highway_config.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/simd/dispatch_probe.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace probe {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

void ProbeKernel_SIMD(const float* src, float* dst, size_t count, const char** out_target_name) {
  if (out_target_name) {
    *out_target_name = hwy::TargetName(HWY_TARGET);
  }
  const hn::ScalableTag<float> d;
  const size_t N = hn::Lanes(d);
  size_t i = 0;
  const auto two = hn::Set(d, 2.0f);
  const auto one_half = hn::Set(d, 1.5f);
  for (; i + N <= count; i += N) {
    const auto v = hn::LoadU(d, src + i);
    const auto res = hn::MulAdd(v, two, one_half);
    hn::StoreU(res, d, dst + i);
  }
  for (; i < count; ++i) {
    dst[i] = src[i] * 2.0f + 1.5f;
  }
}

} // namespace HWY_NAMESPACE
} // namespace probe
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
#include "avs_simd/target_policy.h"

namespace probe {

void ProbeKernel_C(const float* src, float* dst, size_t count, const char** out_target_name) {
  if (out_target_name) {
    *out_target_name = "C";
  }
  for (size_t i = 0; i < count; ++i) {
    dst[i] = src[i] * 2.0f + 1.5f;
  }
}

ProbeKernelFn ResolveProbeKernelForTarget(int64_t target) {
  switch (target) {
#if HWY_ARCH_X86
#if HWY_TARGETS & HWY_AVX10_2
    case HWY_AVX10_2:
      if (auto fn = HWY_CHOOSE_AVX10_2(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_AVX3_SPR
    case HWY_AVX3_SPR:
      if (auto fn = HWY_CHOOSE_AVX3_SPR(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_AVX3_ZEN4
    case HWY_AVX3_ZEN4:
      if (auto fn = HWY_CHOOSE_AVX3_ZEN4(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_AVX3_DL
    case HWY_AVX3_DL:
      if (auto fn = HWY_CHOOSE_AVX3_DL(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_AVX3
    case HWY_AVX3:
      if (auto fn = HWY_CHOOSE_AVX3(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_AVX2
    case HWY_AVX2:
      if (auto fn = HWY_CHOOSE_AVX2(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SSE4
    case HWY_SSE4:
      if (auto fn = HWY_CHOOSE_SSE4(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SSSE3
    case HWY_SSSE3:
      if (auto fn = HWY_CHOOSE_SSSE3(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SSE2
    case HWY_SSE2:
      if (auto fn = HWY_CHOOSE_SSE2(ProbeKernel_SIMD)) return fn;
      break;
#endif
#endif // HWY_ARCH_X86

#if HWY_ARCH_ARM
#if HWY_TARGETS & HWY_SVE2_128
    case HWY_SVE2_128:
      if (auto fn = HWY_CHOOSE_SVE2_128(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SVE_256
    case HWY_SVE_256:
      if (auto fn = HWY_CHOOSE_SVE_256(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SVE2
    case HWY_SVE2:
      if (auto fn = HWY_CHOOSE_SVE2(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SVE
    case HWY_SVE:
      if (auto fn = HWY_CHOOSE_SVE(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_NEON_BF16
    case HWY_NEON_BF16:
      if (auto fn = HWY_CHOOSE_NEON_BF16(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_NEON
    case HWY_NEON:
      if (auto fn = HWY_CHOOSE_NEON(ProbeKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_NEON_WITHOUT_AES
    case HWY_NEON_WITHOUT_AES:
      if (auto fn = HWY_CHOOSE_NEON_WITHOUT_AES(ProbeKernel_SIMD)) return fn;
      break;
#endif
#endif // HWY_ARCH_ARM

#if HWY_TARGETS & HWY_RVV
    case HWY_RVV:
      return HWY_CHOOSE_RVV(ProbeKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_PPC8
    case HWY_PPC8:
      return HWY_CHOOSE_PPC8(ProbeKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_PPC9
    case HWY_PPC9:
      return HWY_CHOOSE_PPC9(ProbeKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_PPC10
    case HWY_PPC10:
      return HWY_CHOOSE_PPC10(ProbeKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_Z14
    case HWY_Z14:
      return HWY_CHOOSE_Z14(ProbeKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_Z15
    case HWY_Z15:
      return HWY_CHOOSE_Z15(ProbeKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_LSX
    case HWY_LSX:
      return HWY_CHOOSE_LSX(ProbeKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_LASX
    case HWY_LASX:
      return HWY_CHOOSE_LASX(ProbeKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_WASM
    case HWY_WASM:
      return HWY_CHOOSE_WASM(ProbeKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_WASM_EMU256
    case HWY_WASM_EMU256:
      return HWY_CHOOSE_WASM_EMU256(ProbeKernel_SIMD);
#endif


    default:
      break;
  }
  return &ProbeKernel_C;
}

ProbeKernelFn ResolveProbeKernel(int avs_cpu_flags) {
  const int64_t chosen = avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS);
  return ResolveProbeKernelForTarget(chosen);
}

int64_t GetProbeKernelChosenTarget(int avs_cpu_flags) {
  return avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS);
}

int64_t GetProbeKernelCompiledTargets() {
  return HWY_TARGETS;
}

} // namespace probe
#endif // HWY_ONCE
