#include <gtest/gtest.h>

#include "conditional_test_helpers.h"

#include "support/cpu_features.h"

#include <vector>

namespace avsut::test {
namespace {

std::vector<SumCase> sum_cases() {
  return {
      make_sum_case(
          37, 5, 64, Variant<SumFunction>{"sse2", get_sum_of_pixels_sse2, IsaRequirement::Sse2}),
      make_sum_case(
          53, 7, 96, Variant<SumFunction>{"sse2", get_sum_of_pixels_sse2, IsaRequirement::Sse2},
          0xF30E5301U),
  };
}

std::vector<SadIntCase> sad_int_cases() {
  return {
      make_sad_int_case(
          "Plane8", false, 37, 5, 64, 80,
          Variant<SadIntFunction>{"sse2", calculate_sad_sse2<false>, IsaRequirement::Sse2}),
      make_sad_int_case(
          "Plane8", false, 53, 7, 96, 112,
          Variant<SadIntFunction>{"sse2", calculate_sad_sse2<false>, IsaRequirement::Sse2},
          0xF30E5302U),
      make_sad_int_case(
          "PackedRgb32", true, 32, 5, 64, 80,
          Variant<SadIntFunction>{"sse2", calculate_sad_sse2<true>, IsaRequirement::Sse2}),
      make_sad_int_case(
          "PackedRgb32", true, 44, 7, 96, 112,
          Variant<SadIntFunction>{"sse2", calculate_sad_sse2<true>, IsaRequirement::Sse2},
          0xF30E5303U),
  };
}

std::vector<SadWideCase> sad_wide_cases() {
  return {
      make_sad_wide_case(
          "Plane8Wide", 1, false, 37, 5, 64, 80,
          Variant<SadWideFunction>{"sse2", calculate_sad_8_or_16_sse2<std::uint8_t, false>,
                                   IsaRequirement::Sse2}),
      make_sad_wide_case(
          "Plane8Wide", 1, false, 53, 7, 96, 112,
          Variant<SadWideFunction>{"sse2", calculate_sad_8_or_16_sse2<std::uint8_t, false>,
                                   IsaRequirement::Sse2},
          0xF30E5304U),
      make_sad_wide_case(
          "Plane16", 2, false, 37, 5, 96, 112,
          Variant<SadWideFunction>{"sse2", calculate_sad_8_or_16_sse2<std::uint16_t, false>,
                                   IsaRequirement::Sse2}),
      make_sad_wide_case(
          "Plane16", 2, false, 53, 7, 128, 144,
          Variant<SadWideFunction>{"sse2", calculate_sad_8_or_16_sse2<std::uint16_t, false>,
                                   IsaRequirement::Sse2},
          0xF30E5305U),
      make_sad_wide_case(
          "PackedRgb64", 2, true, 32, 5, 128, 144,
          Variant<SadWideFunction>{"sse2", calculate_sad_8_or_16_sse2<std::uint16_t, true>,
                                   IsaRequirement::Sse2}),
      make_sad_wide_case(
          "PackedRgb64", 2, true, 36, 7, 96, 112,
          Variant<SadWideFunction>{"sse2", calculate_sad_8_or_16_sse2<std::uint16_t, true>,
                                   IsaRequirement::Sse2},
          0xF30E5306U),
  };
}

class PixelSumKernels : public ::testing::TestWithParam<SumCase> {};

TEST_P(PixelSumKernels, MatchesIndependentSum) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_sum_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, PixelSumKernels, ::testing::ValuesIn(sum_cases()),
                         [](const ::testing::TestParamInfo<SumCase>& info) {
                           return info.param.name;
                         });

class SadIntKernels : public ::testing::TestWithParam<SadIntCase> {};

TEST_P(SadIntKernels, MatchesIndependentAbsoluteDifference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_sad_int_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, SadIntKernels, ::testing::ValuesIn(sad_int_cases()),
                         [](const ::testing::TestParamInfo<SadIntCase>& info) {
                           return info.param.name;
                         });

class SadWideKernels : public ::testing::TestWithParam<SadWideCase> {};

TEST_P(SadWideKernels, MatchesIndependentAbsoluteDifference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bytes_per_sample == 1) {
    run_sad_wide_case<std::uint8_t>(test_case);
  } else {
    run_sad_wide_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, SadWideKernels, ::testing::ValuesIn(sad_wide_cases()),
                         [](const ::testing::TestParamInfo<SadWideCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
