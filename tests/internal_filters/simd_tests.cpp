#include <gtest/gtest.h>
#include "focus/kernel/backend.h"
#include "rotation/kernel/backend.h"
#include "limiter/kernel/backend.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace {

TEST(InternalFilterSimd, RequestedBuildRetainsNativeBackends) {
  const auto focus = aif_focus_supported_cpu();
  const auto rotation = aif_rotation_supported_cpu();
  const auto limiter = aif_limiter_supported_cpu();
  RecordProperty("focus_cpu", focus);
  RecordProperty("rotation_cpu", rotation);
  RecordProperty("limiter_cpu", limiter);
#if AVS_TEST_AIF_SCALAR_ONLY
  EXPECT_EQ(focus, 0u);
  EXPECT_EQ(rotation, 0u);
  EXPECT_EQ(limiter, 0u);
#elif defined(__aarch64__) || defined(_M_ARM64)
  // ARM64 must expose a real NEON kernel, not silently pass using C fallback.
  EXPECT_NE(focus & AIF_FOCUS_NEON, 0u);
  EXPECT_NE(rotation & AIF_ROTATION_NEON, 0u);
  EXPECT_NE(limiter & AIF_LIMITER_NEON, 0u);
#elif defined(__x86_64__) || defined(_M_X64)
  // SSE2 is baseline on x64, even when legacy ENABLE_INTEL_SIMD is OFF.
  EXPECT_NE(focus & AIF_FOCUS_SSE2, 0u);
  EXPECT_NE(rotation & AIF_ROTATION_SSE2, 0u);
  EXPECT_NE(limiter & AIF_LIMITER_SSE2, 0u);
#endif
  if (focus) EXPECT_NE(aif::focus::backend(focus), nullptr);
  if (rotation) EXPECT_NE(aif::rotation::backend(rotation), nullptr);
  if (limiter) EXPECT_NE(aif::limiter::backend(limiter), nullptr);
  EXPECT_EQ(aif_focus_selected_cpu(0), 0u);
  EXPECT_EQ(aif::focus::backend(0), nullptr);
  EXPECT_EQ(aif::rotation::backend(0), nullptr);
  EXPECT_EQ(aif::limiter::backend(0), nullptr);
}

TEST(InternalFilterSimd, EveryAvailableBackendProcessesPixelsAndPreservesPadding) {
  constexpr int pitch = 64, width = 37, height = 3;
  alignas(32) std::array<uint8_t, pitch * height> source{}, other{};
  int64_t sad = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < pitch; ++x) {
      source[y * pitch + x] = static_cast<uint8_t>(x * 17 + y * 31);
      other[y * pitch + x] = static_cast<uint8_t>(x * 7 + y * 13);
      if (x < width) sad += std::abs(int(source[y * pitch + x]) - int(other[y * pitch + x]));
    }
  }
  // Include C, then each supported SIMD target separately. Check backend pointers
  // so a broken resolver cannot pass the numerical check through silent fallback.
  for (uint32_t cpu = 0; cpu <= AIF_FOCUS_SVE2; cpu = cpu ? cpu << 1 : 1) {
    if (cpu && !(aif_focus_supported_cpu() & cpu)) continue;
    // SVE2 additionally requires NEON permission in the Focus contract.
    const uint32_t allowed = cpu == AIF_FOCUS_SVE2 ? cpu | AIF_FOCUS_NEON : cpu;
    SCOPED_TRACE(allowed);
    if (cpu) ASSERT_NE(aif::focus::backend(allowed), nullptr);
    int64_t actual = -1;
    ASSERT_EQ(aif_focus_sad(source.data(), other.data(), pitch, pitch, width, height, 8, allowed, &actual), AIF_FOCUS_OK);
    EXPECT_EQ(actual, sad);
  }
  for (uint32_t cpu = 0; cpu <= AIF_ROTATION_AVX10_2; cpu = cpu ? cpu << 1 : 1) {
    if (cpu && !(aif_rotation_supported_cpu() & cpu)) continue;
    SCOPED_TRACE(cpu);
    if (cpu) ASSERT_NE(aif::rotation::backend(cpu), nullptr);
    std::array<uint8_t, pitch * height> output;
    output.fill(0xA5);
    ASSERT_EQ(aif_rotation_apply(source.data(), output.data(), width, height, pitch, pitch,
                                1, AIF_ROTATION_HORIZONTAL, cpu), 0);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < pitch; ++x)
        EXPECT_EQ(output[y * pitch + x], x < width ? source[y * pitch + width - 1 - x] : 0xA5);
  }
  for (uint32_t cpu = 0; cpu <= AIF_LIMITER_AVX10_2; cpu = cpu ? cpu << 1 : 1) {
    if (cpu && !(aif_limiter_supported_cpu() & cpu)) continue;
    SCOPED_TRACE(cpu);
    if (cpu) ASSERT_NE(aif::limiter::backend(cpu), nullptr);
    auto output = source;
    const aif_limiter_limits limits{16, 235, 16, 240, 8};
    ASSERT_EQ(aif_limiter_apply(output.data(), pitch, width, height, &limits, 0, 0, cpu), 0);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < pitch; ++x)
        EXPECT_EQ(output[y * pitch + x], x < width ? std::clamp(int(source[y * pitch + x]), 16, 235)
                                                  : source[y * pitch + x]);
  }
}

} // namespace
