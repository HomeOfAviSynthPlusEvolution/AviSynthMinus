#include <gtest/gtest.h>

#include "convert_rgb_test_helpers.h"

#include "support/cpu_features.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

std::vector<PackedRgbConversionCase> packed_rgb_conversion_cases() {
  return {
      make_packed_rgb_conversion_case(
          "Rgb24ToRgb32", 1, 3, 4, 29, 5, 96, 128,
          Variant<PackedRgbConversionFunction>{"ssse3", convert_rgb24_to_rgb32_ssse3,
                                               IsaRequirement::Ssse3},
          "4a547d0ec7a16989", PackedRgbOutputPaddingPolicy::AllowToSimdAlignment),
      make_packed_rgb_conversion_case(
          "Rgb32ToRgb24", 1, 4, 3, 29, 5, 128, 96,
          Variant<PackedRgbConversionFunction>{"ssse3", convert_rgb32_to_rgb24_ssse3,
                                               IsaRequirement::Ssse3},
          "8b18a5788b9ae2fa", PackedRgbOutputPaddingPolicy::AllowToSimdAlignment),
      make_packed_rgb_conversion_case(
          "Rgb48ToRgb64", 2, 3, 4, 11, 5, 80, 96,
          Variant<PackedRgbConversionFunction>{"ssse3", convert_rgb48_to_rgb64_ssse3,
                                               IsaRequirement::Ssse3},
          "70c28e524b0ebab7"),
      make_packed_rgb_conversion_case(
          "Rgb64ToRgb48", 2, 4, 3, 11, 5, 96, 80,
          Variant<PackedRgbConversionFunction>{"ssse3", convert_rgb64_to_rgb48_ssse3,
                                               IsaRequirement::Ssse3},
          "b4188f4710203cd7"),
  };
}

class PackedRgbConversionKernels : public ::testing::TestWithParam<PackedRgbConversionCase> {};

TEST_P(PackedRgbConversionKernels, MatchesIndependentPackedLayout) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bytes_per_component == 1) {
    run_packed_rgb_conversion_case<std::uint8_t>(test_case);
  } else {
    run_packed_rgb_conversion_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, PackedRgbConversionKernels,
                         ::testing::ValuesIn(packed_rgb_conversion_cases()),
                         [](const ::testing::TestParamInfo<PackedRgbConversionCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
