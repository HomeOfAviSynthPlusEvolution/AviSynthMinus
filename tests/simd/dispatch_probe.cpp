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
#define AVS_SIMD_TARGET(target, choose) case target: return choose(ProbeKernel_SIMD);
#include "avs_simd/targets.inc"
#undef AVS_SIMD_TARGET


    default:
      break;
  }
  return &ProbeKernel_C;
}

ProbeKernelFn ResolveProbeKernel(int64_t avs_cpu_flags) {
  const int64_t chosen = avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS);
  return ResolveProbeKernelForTarget(chosen);
}

int64_t GetProbeKernelChosenTarget(int64_t avs_cpu_flags) {
  return avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS);
}

int64_t GetProbeKernelCompiledTargets() {
  return HWY_TARGETS;
}

} // namespace probe
#endif // HWY_ONCE
