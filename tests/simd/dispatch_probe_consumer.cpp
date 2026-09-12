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

ConsumerKernelFn ResolveConsumerKernel(int64_t avs_cpu_flags) {
  const int64_t chosen = avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS);
  switch (chosen) {
#define AVS_SIMD_TARGET(target, choose) case target: return choose(ConsumerKernel_SIMD);
#include "avs_simd/targets.inc"
#undef AVS_SIMD_TARGET


    default:
      break;
  }
  return &ConsumerKernel_C;
}

int64_t GetConsumerKernelChosenTarget(int64_t avs_cpu_flags) {
  return avs_simd::ChooseTarget(avs_cpu_flags, HWY_TARGETS);
}

int64_t GetConsumerKernelCompiledTargets() {
  return HWY_TARGETS;
}

} // namespace consumer
#endif // HWY_ONCE
