#include <gtest/gtest.h>

#include <vector>

#include "support/kernel_cpu_profiles.h"
#include "temporal_soften_test_helpers.h"

namespace avsut::test {
namespace {

std::vector<TemporalSoften8Case> temporal_soften8_cases() {
  return {
      make_temporal_soften8_case(48, 64, 2, 12U, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                 "ba587cc6b944c933"),
      make_temporal_soften8_case(48, 64, 2, 12U, KernelCpuProfile{"ssse3", AIF_FOCUS_SSSE3},
                                 "ba587cc6b944c933"),
      make_temporal_soften8_case(48, 64, 3, 255U, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                 "504f4bf90a8bbb1a"),
      make_temporal_soften8_case(48, 64, 3, 255U, KernelCpuProfile{"ssse3", AIF_FOCUS_SSSE3},
                                 "504f4bf90a8bbb1a"),
  };
}

std::vector<TemporalSoften16Case> temporal_soften16_cases() {
  return {
      make_temporal_soften16_case(48, 128, 2, 12U, 10, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                  "9ba32b12dab060ff"),
      make_temporal_soften16_case(48, 128, 2, 12U, 10, KernelCpuProfile{"sse41", AIF_FOCUS_SSE41},
                                  "9ba32b12dab060ff"),
      make_temporal_soften16_case(48, 128, 3, 255U, 10, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                  "71c2d93a089f302b"),
      make_temporal_soften16_case(48, 128, 3, 255U, 10, KernelCpuProfile{"sse41", AIF_FOCUS_SSE41},
                                  "71c2d93a089f302b"),
      make_temporal_soften16_case(48, 128, 2, 12U, 16, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                  "8fcdbf8e0d206de1"),
      make_temporal_soften16_case(48, 128, 2, 12U, 16, KernelCpuProfile{"sse41", AIF_FOCUS_SSE41},
                                  "8fcdbf8e0d206de1"),
      make_temporal_soften16_case(48, 128, 2, 255U, 16, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                  "6ad6b30d924503fd"),
      make_temporal_soften16_case(48, 128, 2, 255U, 16, KernelCpuProfile{"sse41", AIF_FOCUS_SSE41},
                                  "6ad6b30d924503fd"),
  };
}

class TemporalSoften8Kernels : public ::testing::TestWithParam<TemporalSoften8Case> {};

TEST_P(TemporalSoften8Kernels, MatchesIndependentThresholdedAverage) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_temporal_soften8_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, TemporalSoften8Kernels, ::testing::ValuesIn(temporal_soften8_cases()),
                         [](const ::testing::TestParamInfo<TemporalSoften8Case>& info) {
                           return info.param.name;
                         });

class TemporalSoften16Kernels : public ::testing::TestWithParam<TemporalSoften16Case> {};

TEST_P(TemporalSoften16Kernels, MatchesIndependentThresholdedAverage) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_temporal_soften16_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, TemporalSoften16Kernels, ::testing::ValuesIn(temporal_soften16_cases()),
                         [](const ::testing::TestParamInfo<TemporalSoften16Case>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
