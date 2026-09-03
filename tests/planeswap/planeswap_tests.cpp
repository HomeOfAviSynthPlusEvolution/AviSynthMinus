#include <gtest/gtest.h>

#include "planeswap_test_helpers.h"

#include "support/cpu_features.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

std::vector<Yuy2SwapCase> yuy2_cases() {
  constexpr auto hash = "aa4fa9f290b9d1f8";
  return {
      make_yuy2_case(48, 5, 64, 64,
                     Variant<PlaneSwapFuncPtr>{"sse2", yuy2_swap_sse2, IsaRequirement::Sse2}, hash),
      make_yuy2_case(48, 5, 64, 64,
                     Variant<PlaneSwapFuncPtr>{"ssse3", yuy2_swap_ssse3, IsaRequirement::Ssse3},
                     hash),
      make_yuy2_case(
          80, 7, 96, 112,
          Variant<PlaneSwapFuncPtr>{"sse2", yuy2_swap_sse2, IsaRequirement::Sse2},
          "a2242d88e87142cc", 0xF30A5A01U),
      make_yuy2_case(
          80, 7, 96, 112,
          Variant<PlaneSwapFuncPtr>{"ssse3", yuy2_swap_ssse3, IsaRequirement::Ssse3},
          "a2242d88e87142cc", 0xF30A5A01U),
  };
}

std::vector<Yuy2UvToYCase> yuy2_uv_to_y_cases() {
  return {
      make_yuy2_uv_to_y_case(
          "Yuy2UvToY", true, 32, 5, 80, 64, 1,
          Variant<Yuy2UvToYFuncPtr>{"sse2", yuy2_uvtoy_sse2, IsaRequirement::Sse2},
          "c6e351ce228df1ae"),
      make_yuy2_uv_to_y_case(
          "Yuy2UvToY", true, 32, 5, 80, 64, 3,
          Variant<Yuy2UvToYFuncPtr>{"sse2", yuy2_uvtoy_sse2, IsaRequirement::Sse2},
          "83356ed499ef5dc7"),
      make_yuy2_uv_to_y_case(
          "Yuy2UvToY8", false, 32, 5, 160, 64, 1,
          Variant<Yuy2UvToYFuncPtr>{"sse2", yuy2_uvtoy8_sse2, IsaRequirement::Sse2},
          "5b73aaadb3308435"),
      make_yuy2_uv_to_y_case(
          "Yuy2UvToY8", false, 32, 5, 160, 64, 3,
          Variant<Yuy2UvToYFuncPtr>{"sse2", yuy2_uvtoy8_sse2, IsaRequirement::Sse2},
          "ce501d893cec226c"),
  };
}

std::vector<Yuy2ToUvCase> yuy2_to_uv_cases() {
  return {
      make_yuy2_to_uv_case(
          true, 64, 5, 96, 64, 96,
          Variant<Yuy2ToUvFuncPtr>{"sse2", yuy2_ytouv_sse2<true>, IsaRequirement::Sse2},
          "601073aa4c80ec7d"),
      make_yuy2_to_uv_case(
          false, 64, 5, 96, 64, 96,
          Variant<Yuy2ToUvFuncPtr>{"sse2", yuy2_ytouv_sse2<false>, IsaRequirement::Sse2},
          "e524cc0b56ef34d5"),
  };
}

class Yuy2SwapKernels : public ::testing::TestWithParam<Yuy2SwapCase> {};

TEST_P(Yuy2SwapKernels, SwapsChromaBytePositions) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_yuy2_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, Yuy2SwapKernels, ::testing::ValuesIn(yuy2_cases()),
                         [](const ::testing::TestParamInfo<Yuy2SwapCase>& info) {
                           return info.param.name;
                         });

class Yuy2UvToYKernels : public ::testing::TestWithParam<Yuy2UvToYCase> {};

TEST_P(Yuy2UvToYKernels, ExtractsChromaWithIndependentNeutralChromaReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_yuy2_uv_to_y_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, Yuy2UvToYKernels, ::testing::ValuesIn(yuy2_uv_to_y_cases()),
                         [](const ::testing::TestParamInfo<Yuy2UvToYCase>& info) {
                           return info.param.name;
                         });

class Yuy2ToUvKernels : public ::testing::TestWithParam<Yuy2ToUvCase> {};

TEST_P(Yuy2ToUvKernels, InterleavesPlanarComponentsWithBothLumaModes) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_yuy2_to_uv_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, Yuy2ToUvKernels, ::testing::ValuesIn(yuy2_to_uv_cases()),
                         [](const ::testing::TestParamInfo<Yuy2ToUvCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
