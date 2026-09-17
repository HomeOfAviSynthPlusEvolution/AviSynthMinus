#include <gtest/gtest.h>

#include <vector>

#include "limiter_test_helpers.h"
#include "support/kernel_cpu_profiles.h"

namespace avsut::test {
namespace {

std::vector<Limiter8Case> limiter8_cases() {
  return {
      make_limiter8_case(37, 5, 48, 16, 235, KernelCpuProfile{"sse2", AIF_LIMITER_SSE2}, "bafa5e1c8ef8ad16")};
}

std::vector<Limiter16Case> limiter16_cases() {
  return {
      make_limiter16_case(23, 5, 64, 64, 60000, KernelCpuProfile{"sse2", AIF_LIMITER_SSE2},
                          "8c0d91bd6aab6687"),
      make_limiter16_case(23, 5, 64, 64, 60000, KernelCpuProfile{"sse4.1", AIF_LIMITER_SSE4},
                          "8c0d91bd6aab6687"),
  };
}

class Limiter8Kernels : public ::testing::TestWithParam<Limiter8Case> {};

TEST_P(Limiter8Kernels, ClampsBoundaryValuesInPlace) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_limiter_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_limiter8_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, Limiter8Kernels, ::testing::ValuesIn(limiter8_cases()),
                         [](const ::testing::TestParamInfo<Limiter8Case>& info) { return info.param.name; });

class Limiter16Kernels : public ::testing::TestWithParam<Limiter16Case> {};

TEST_P(Limiter16Kernels, ClampsBoundaryValuesInPlace) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_limiter_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_limiter16_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, Limiter16Kernels, ::testing::ValuesIn(limiter16_cases()),
                         [](const ::testing::TestParamInfo<Limiter16Case>& info) { return info.param.name; });

}  // namespace
}  // namespace avsut::test
