#include "tests/simd/dispatch_probe_consumer.h"
#include "avs_simd/highway_config.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/simd/dispatch_probe_consumer.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace consumer {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

void ConsumerKernel_SIMD(const int32_t* src, int32_t* dst, size_t count, const char** out_target_name) {
  if (out_target_name) {
    *out_target_name = hwy::TargetName(HWY_TARGET);
  }
  const hn::ScalableTag<int32_t> d;
  const size_t N = hn::Lanes(d);
  size_t i = 0;
  const auto three = hn::Set(d, 3);
  const auto seven = hn::Set(d, 7);
  for (; i + N <= count; i += N) {
    const auto v = hn::LoadU(d, src + i);
    const auto res = hn::Add(hn::Mul(v, three), seven);
    hn::StoreU(res, d, dst + i);
  }
  for (; i < count; ++i) {
    dst[i] = src[i] * 3 + 7;
  }
}

} // namespace HWY_NAMESPACE
} // namespace consumer
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
#include "avs_simd/target_policy.h"

namespace consumer {

void ConsumerKernel_C(const int32_t* src, int32_t* dst, size_t count, const char** out_target_name) {
  if (out_target_name) {
    *out_target_name = "C";
  }
  for (size_t i = 0; i < count; ++i) {
    dst[i] = src[i] * 3 + 7;
  }
}

ConsumerKernelFn ResolveConsumerKernel(int avs_cpu_flags) {
  const int64_t chosen = avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS);
  switch (chosen) {
#if HWY_ARCH_X86
#if HWY_TARGETS & HWY_AVX10_2
    case HWY_AVX10_2:
      if (auto fn = HWY_CHOOSE_AVX10_2(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_AVX3_SPR
    case HWY_AVX3_SPR:
      if (auto fn = HWY_CHOOSE_AVX3_SPR(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_AVX3_ZEN4
    case HWY_AVX3_ZEN4:
      if (auto fn = HWY_CHOOSE_AVX3_ZEN4(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_AVX3_DL
    case HWY_AVX3_DL:
      if (auto fn = HWY_CHOOSE_AVX3_DL(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_AVX3
    case HWY_AVX3:
      if (auto fn = HWY_CHOOSE_AVX3(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_AVX2
    case HWY_AVX2:
      if (auto fn = HWY_CHOOSE_AVX2(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SSE4
    case HWY_SSE4:
      if (auto fn = HWY_CHOOSE_SSE4(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SSSE3
    case HWY_SSSE3:
      if (auto fn = HWY_CHOOSE_SSSE3(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SSE2
    case HWY_SSE2:
      if (auto fn = HWY_CHOOSE_SSE2(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#endif // HWY_ARCH_X86

#if HWY_ARCH_ARM
#if HWY_TARGETS & HWY_SVE2_128
    case HWY_SVE2_128:
      if (auto fn = HWY_CHOOSE_SVE2_128(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SVE_256
    case HWY_SVE_256:
      if (auto fn = HWY_CHOOSE_SVE_256(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SVE2
    case HWY_SVE2:
      if (auto fn = HWY_CHOOSE_SVE2(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_SVE
    case HWY_SVE:
      if (auto fn = HWY_CHOOSE_SVE(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_NEON_BF16
    case HWY_NEON_BF16:
      if (auto fn = HWY_CHOOSE_NEON_BF16(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_NEON
    case HWY_NEON:
      if (auto fn = HWY_CHOOSE_NEON(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#if HWY_TARGETS & HWY_NEON_WITHOUT_AES
    case HWY_NEON_WITHOUT_AES:
      if (auto fn = HWY_CHOOSE_NEON_WITHOUT_AES(ConsumerKernel_SIMD)) return fn;
      break;
#endif
#endif // HWY_ARCH_ARM

#if HWY_TARGETS & HWY_RVV
    case HWY_RVV:
      return HWY_CHOOSE_RVV(ConsumerKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_PPC8
    case HWY_PPC8:
      return HWY_CHOOSE_PPC8(ConsumerKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_PPC9
    case HWY_PPC9:
      return HWY_CHOOSE_PPC9(ConsumerKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_PPC10
    case HWY_PPC10:
      return HWY_CHOOSE_PPC10(ConsumerKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_Z14
    case HWY_Z14:
      return HWY_CHOOSE_Z14(ConsumerKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_Z15
    case HWY_Z15:
      return HWY_CHOOSE_Z15(ConsumerKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_LSX
    case HWY_LSX:
      return HWY_CHOOSE_LSX(ConsumerKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_LASX
    case HWY_LASX:
      return HWY_CHOOSE_LASX(ConsumerKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_WASM
    case HWY_WASM:
      return HWY_CHOOSE_WASM(ConsumerKernel_SIMD);
#endif
#if HWY_TARGETS & HWY_WASM_EMU256
    case HWY_WASM_EMU256:
      return HWY_CHOOSE_WASM_EMU256(ConsumerKernel_SIMD);
#endif


    default:
      break;
  }
  return &ConsumerKernel_C;
}

int64_t GetConsumerKernelChosenTarget(int avs_cpu_flags) {
  return avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS);
}

int64_t GetConsumerKernelCompiledTargets() {
  return HWY_TARGETS;
}

} // namespace consumer
#endif // HWY_ONCE
