#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "planeswap_test_helpers.h"
#include "support/kernel_cpu_profiles.h"

namespace avsut::test {
namespace {

std::vector<Yuy2SwapCase> yuy2_cases() {
  constexpr auto hash = "aa4fa9f290b9d1f8";
  return {
      make_yuy2_case(48, 5, 64, 64, KernelCpuProfile{"sse2", AIF_PLANES_SSE2}, hash),
      make_yuy2_case(48, 5, 64, 64, KernelCpuProfile{"ssse3", AIF_PLANES_SSSE3}, hash),
      make_yuy2_case(80, 7, 96, 112, KernelCpuProfile{"sse2", AIF_PLANES_SSE2}, "a2242d88e87142cc",
                     0xF30A5A01U),
      make_yuy2_case(80, 7, 96, 112, KernelCpuProfile{"ssse3", AIF_PLANES_SSSE3}, "a2242d88e87142cc",
                     0xF30A5A01U),
  };
}

std::vector<Yuy2UvToYCase> yuy2_uv_to_y_cases() {
  return {
      make_yuy2_uv_to_y_case("Yuy2UvToY", true, 32, 5, 80, 64, 1, KernelCpuProfile{"sse2", AIF_PLANES_SSE2},
                             "c6e351ce228df1ae"),
      make_yuy2_uv_to_y_case("Yuy2UvToY", true, 32, 5, 80, 64, 3, KernelCpuProfile{"sse2", AIF_PLANES_SSE2},
                             "83356ed499ef5dc7"),
      make_yuy2_uv_to_y_case("Yuy2UvToY8", false, 32, 5, 160, 64, 1,
                             KernelCpuProfile{"sse2", AIF_PLANES_SSE2}, "5b73aaadb3308435"),
      make_yuy2_uv_to_y_case("Yuy2UvToY8", false, 32, 5, 160, 64, 3,
                             KernelCpuProfile{"sse2", AIF_PLANES_SSE2}, "ce501d893cec226c"),
  };
}

std::vector<Yuy2ToUvCase> yuy2_to_uv_cases() {
  return {
      make_yuy2_to_uv_case(true, 64, 5, 96, 64, 96, KernelCpuProfile{"sse2", AIF_PLANES_SSE2},
                           "601073aa4c80ec7d"),
      make_yuy2_to_uv_case(false, 64, 5, 96, 64, 96, KernelCpuProfile{"sse2", AIF_PLANES_SSE2},
                           "e524cc0b56ef34d5"),
  };
}

class Yuy2SwapKernels : public ::testing::TestWithParam<Yuy2SwapCase> {};

TEST_P(Yuy2SwapKernels, SwapsChromaBytePositions) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_planes_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_yuy2_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, Yuy2SwapKernels, ::testing::ValuesIn(yuy2_cases()),
                         [](const ::testing::TestParamInfo<Yuy2SwapCase>& info) { return info.param.name; });

class Yuy2UvToYKernels : public ::testing::TestWithParam<Yuy2UvToYCase> {};

TEST_P(Yuy2UvToYKernels, ExtractsChromaWithIndependentNeutralChromaReference) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_planes_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_yuy2_uv_to_y_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, Yuy2UvToYKernels, ::testing::ValuesIn(yuy2_uv_to_y_cases()),
                         [](const ::testing::TestParamInfo<Yuy2UvToYCase>& info) { return info.param.name; });

class Yuy2ToUvKernels : public ::testing::TestWithParam<Yuy2ToUvCase> {};

TEST_P(Yuy2ToUvKernels, InterleavesPlanarComponentsWithBothLumaModes) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_planes_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_yuy2_to_uv_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, Yuy2ToUvKernels, ::testing::ValuesIn(yuy2_to_uv_cases()),
                         [](const ::testing::TestParamInfo<Yuy2ToUvCase>& info) { return info.param.name; });

}  // namespace
}  // namespace avsut::test
