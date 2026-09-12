#include <gtest/gtest.h>
#include "avs_simd/target_policy.h"
#include "tests/simd/dispatch_probe.h"
#include "tests/simd/dispatch_probe_consumer.h"
#include "tests/simd/shared_consumer.h"
#include "support/avisynth_environment.h"
#include <avs/cpuid.h>

#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <string>
#include <cmath>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

constexpr float kSentinel = -99999.0f;
constexpr size_t kGuardLanes = 16;

struct GuardedBuffer {
  std::vector<float> raw;
  float* data;
  size_t count;

  GuardedBuffer(size_t n, float init_val = 0.0f) : count(n) {
    raw.assign(n + 2 * kGuardLanes, kSentinel);
    data = raw.data() + kGuardLanes;
    for (size_t i = 0; i < n; ++i) {
      data[i] = init_val;
    }
  }

  void VerifyGuards() const {
    for (size_t i = 0; i < kGuardLanes; ++i) {
      ASSERT_EQ(raw[i], kSentinel) << "Front guard corrupted at index " << i;
    }
    for (size_t i = count + kGuardLanes; i < raw.size(); ++i) {
      ASSERT_EQ(raw[i], kSentinel) << "Rear guard corrupted at index " << i;
    }
  }
};

// 1. C fallback execution and pointer check
TEST(SimdDispatchTest, FallbackReturnsOrdinaryCFunctionPointerAndProducesCorrectResults) {
  const auto fn = probe::ResolveProbeKernel(0);
  EXPECT_EQ(fn, &probe::ProbeKernel_C)
      << "Resolver must return the ordinary C function pointer, NOT a Highway scalar wrapper";

  const char* executed_target = nullptr;
  GuardedBuffer src(32, 2.5f);
  GuardedBuffer dst(32, 0.0f);

  fn(src.data, dst.data, 32, &executed_target);

  EXPECT_STREQ(executed_target, "C");
  dst.VerifyGuards();
  for (size_t i = 0; i < 32; ++i) {
    EXPECT_FLOAT_EQ(dst.data[i], 2.5f * 2.0f + 1.5f);
  }
}

// 2. Native SIMD target execution on current machine
TEST(SimdDispatchTest, NativeTargetExecutesCorrectlyOrFallsBack) {
  avsut::test::AviSynthEnvironment env;
  const int64_t native_flags = env.get()->GetCPUFlagsEx();
  const int64_t chosen = probe::GetProbeKernelChosenTarget(native_flags);
  const auto fn = probe::ResolveProbeKernel(native_flags);

  std::cout << "[          ] Native CPU flags: 0x" << std::hex << native_flags << std::dec
            << ", Compiled targets: 0x" << std::hex << probe::GetProbeKernelCompiledTargets() << std::dec
            << ", Chosen target: " << avs_simd::TargetName(chosen) << std::endl;

  const char* executed_target = nullptr;
  GuardedBuffer src(64, 3.0f);
  GuardedBuffer dst(64, 0.0f);

  fn(src.data, dst.data, 64, &executed_target);
  dst.VerifyGuards();

  for (size_t i = 0; i < 64; ++i) {
    EXPECT_FLOAT_EQ(dst.data[i], 3.0f * 2.0f + 1.5f);
  }

  if (chosen == avs_simd::TARGET_C_FALLBACK) {
    EXPECT_EQ(fn, &probe::ProbeKernel_C);
    EXPECT_STREQ(executed_target, "C");
    GTEST_SKIP() << "Machine or configuration has no viable SIMD targets; verified C fallback successfully";
  } else {
    EXPECT_NE(fn, &probe::ProbeKernel_C);
    EXPECT_STRNE(executed_target, "C");
    EXPECT_STREQ(executed_target, avs_simd::TargetName(chosen));
  }
}

// 3. Length coverage and guard validation
TEST(SimdDispatchTest, HandlesSmallAndNonVectorLengthsWithoutOverrun) {
  avsut::test::AviSynthEnvironment env;
  const auto native_fn = probe::ResolveProbeKernel(env.get()->GetCPUFlagsEx());

  const std::vector<size_t> test_lengths = {
      0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257
  };

  for (size_t len : test_lengths) {
    GuardedBuffer src(len);
    for (size_t i = 0; i < len; ++i) {
      src.data[i] = static_cast<float>(i) * 0.25f - 1.0f;
    }

    GuardedBuffer dst_c(len, 0.0f);
    GuardedBuffer dst_native(len, 0.0f);

    const char* target_c = nullptr;
    const char* target_native = nullptr;

    probe::ProbeKernel_C(src.data, dst_c.data, len, &target_c);
    native_fn(src.data, dst_native.data, len, &target_native);

    dst_c.VerifyGuards();
    dst_native.VerifyGuards();

    for (size_t i = 0; i < len; ++i) {
      EXPECT_FLOAT_EQ(dst_native.data[i], dst_c.data[i])
          << "Mismatch at index " << i << " for length " << len;
    }
  }
}

// 5. Environment isolation between native and SetMaxCPU("none")
TEST(SimdDispatchTest, MultiEnvironmentInstancesRemainIsolated) {
  avsut::test::AviSynthEnvironment env_native;
  avsut::test::AviSynthEnvironment env_none;

  const AVSValue none("none");
  env_none.get()->Invoke("SetMaxCPU", AVSValue(&none, 1));

  const int64_t flags_native = env_native.get()->GetCPUFlagsEx();
  const int64_t flags_none = env_none.get()->GetCPUFlagsEx();

  const auto fn_native = probe::ResolveProbeKernel(flags_native);
  const auto fn_none = probe::ResolveProbeKernel(flags_none);

  EXPECT_EQ(fn_none, &probe::ProbeKernel_C)
      << "SetMaxCPU('none') must strictly resolve to ordinary C kernel";

  const int64_t native_target = probe::GetProbeKernelChosenTarget(flags_native);
  if (native_target != avs_simd::TARGET_C_FALLBACK) {
    EXPECT_NE(fn_native, &probe::ProbeKernel_C);
  }

  // Interleaved calls across multiple iterations
  for (int iter = 0; iter < 5; ++iter) {
    GuardedBuffer src(48, 1.25f + static_cast<float>(iter));
    GuardedBuffer dst_none(48, 0.0f);
    GuardedBuffer dst_native(48, 0.0f);

    const char* tag_none = nullptr;
    const char* tag_native = nullptr;

    fn_none(src.data, dst_none.data, 48, &tag_none);
    EXPECT_STREQ(tag_none, "C");

    fn_native(src.data, dst_native.data, 48, &tag_native);
    if (native_target != avs_simd::TARGET_C_FALLBACK) {
      EXPECT_STRNE(tag_native, "C");
      EXPECT_STREQ(tag_native, avs_simd::TargetName(native_target));
    }

    dst_none.VerifyGuards();
    dst_native.VerifyGuards();

    for (size_t i = 0; i < 48; ++i) {
      EXPECT_FLOAT_EQ(dst_none.data[i], dst_native.data[i]);
    }
  }
}

#if HWY_ARCH_ARM
TEST(SimdDispatchTest, ArmLimitsRestrictHighwayTargets) {
  for (const char* setting : {"none", "neon", "dotprod"}) {
    avsut::test::AviSynthEnvironment env;
    const AVSValue value(setting);
    env.get()->Invoke("SetMaxCPU", AVSValue(&value, 1));
    const int64_t mask = avs_simd::AvsFlagsToHighwayMask(env.get()->GetCPUFlagsEx());
    EXPECT_EQ(mask & (HWY_SVE | HWY_SVE_256 | HWY_SVE2 | HWY_SVE2_128), 0);
    if (std::string(setting) == "none")
      EXPECT_EQ(probe::ResolveProbeKernel(env.get()->GetCPUFlagsEx()), &probe::ProbeKernel_C);
  }
}
#endif

// 6. Concurrent execution of resolved pointers
TEST(SimdDispatchTest, ConcurrentExecutionsDoNotInterfere) {
  avsut::test::AviSynthEnvironment env;
  const auto fn = probe::ResolveProbeKernel(env.get()->GetCPUFlagsEx());

  constexpr int kThreads = 8;
  constexpr int kIterations = 100;
  constexpr size_t kCount = 128;

  std::vector<std::thread> workers;
  std::atomic<bool> failure_detected{false};

  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&failure_detected, fn, t]() {
      for (int iter = 0; iter < kIterations && !failure_detected.load(); ++iter) {
        std::vector<float> src(kCount);
        std::vector<float> dst(kCount, 0.0f);
        const float base = static_cast<float>(t * 1000 + iter);
        for (size_t i = 0; i < kCount; ++i) {
          src[i] = base + static_cast<float>(i);
        }

        const char* target_name = nullptr;
        fn(src.data(), dst.data(), kCount, &target_name);

        for (size_t i = 0; i < kCount; ++i) {
          const float expected = src[i] * 2.0f + 1.5f;
          if (std::fabs(dst[i] - expected) > 1e-5f) {
            failure_detected.store(true);
            break;
          }
        }
      }
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  EXPECT_FALSE(failure_detected.load()) << "Concurrent execution produced data corruption";
}

// 7. Multi-consumer translation unit link test without symbol collision
TEST(SimdDispatchTest, MultipleHighwayConsumerTranslationUnitsCoexist) {
  avsut::test::AviSynthEnvironment env;
  const int64_t flags = env.get()->GetCPUFlagsEx();

  const auto probe_fn = probe::ResolveProbeKernel(flags);
  const auto consumer_fn = consumer::ResolveConsumerKernel(flags);

  // Probe execution (float)
  float p_src[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  float p_dst[8] = {0.0f};
  const char* p_tag = nullptr;
  probe_fn(p_src, p_dst, 8, &p_tag);
  for (size_t i = 0; i < 8; ++i) {
    EXPECT_FLOAT_EQ(p_dst[i], p_src[i] * 2.0f + 1.5f);
  }

  // Consumer execution (int32)
  int32_t c_src[8] = {10, 20, 30, 40, 50, 60, 70, 80};
  int32_t c_dst[8] = {0};
  const char* c_tag = nullptr;
  consumer_fn(c_src, c_dst, 8, &c_tag);
  for (size_t i = 0; i < 8; ++i) {
    EXPECT_EQ(c_dst[i], c_src[i] * 3 + 7);
  }

  // Confirm target tag agreement (both should use the same target tier)
  EXPECT_STREQ(p_tag, c_tag);
}

TEST(SimdDispatchTest, SharedConsumerUsesStaticHighwayRuntime) {
  avsut::test::AviSynthEnvironment env;

  EXPECT_STREQ(avs_simd_shared_consumer_target_name(0), "C");

  const char* native_target =
      avs_simd_shared_consumer_target_name(env.get()->GetCPUFlagsEx());
  EXPECT_NE(native_target, nullptr);
  EXPECT_STRNE(native_target, "");

#if defined(_WIN32)
  // hwy is static inside simd_shared_consumer; loading this DLL must not add
  // a separate Highway DLL dependency.
  EXPECT_EQ(GetModuleHandleW(L"hwy.dll"), nullptr);
#endif
}

} // namespace
