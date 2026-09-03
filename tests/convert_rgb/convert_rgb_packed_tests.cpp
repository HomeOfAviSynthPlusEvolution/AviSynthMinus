#include <gtest/gtest.h>

#include "convert_rgb_packed_test_helpers.h"

#include "support/cpu_features.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace avsut::test {
namespace {

template <typename T, bool HasSourceAlpha>
void add_planar_to_packed_rgb_variants(std::vector<PlanarToPackedRgbCase>& cases,
                                       std::string operation, std::size_t width, std::size_t height,
                                       std::array<std::size_t, 4> source_pitches,
                                       std::size_t destination_pitch, std::string expected_hash) {
  cases.push_back(make_planar_to_packed_rgb_case(
      std::move(operation), sizeof(T), HasSourceAlpha, width, height, source_pitches, destination_pitch,
      Variant<PlanarToPackedRgbFunction>{"sse2", convert_rgbp_to_rgba_sse2<T, HasSourceAlpha>,
                                         IsaRequirement::Sse2},
      std::move(expected_hash)));
}

std::vector<PlanarToPackedRgbCase> planar_to_packed_rgb_cases() {
  constexpr std::array<std::size_t, 4> kSourcePitches{64, 128, 192, 256};
  std::vector<PlanarToPackedRgbCase> cases;
  add_planar_to_packed_rgb_variants<std::uint8_t, false>(cases, "Rgbp8ToRgb32OpaqueAlpha", 48, 5,
                                                         kSourcePitches, 256, "dc47e46ef12f21a5");
  add_planar_to_packed_rgb_variants<std::uint8_t, true>(cases, "Rgbap8ToRgb32SourceAlpha", 48, 5,
                                                        kSourcePitches, 256, "5b4b61f04fc5ac78");
  add_planar_to_packed_rgb_variants<std::uint16_t, false>(cases, "Rgbp16ToRgb64OpaqueAlpha", 24, 5,
                                                          kSourcePitches, 256, "21d676e96caea470");
  add_planar_to_packed_rgb_variants<std::uint16_t, true>(cases, "Rgbap16ToRgb64SourceAlpha", 24, 5,
                                                         kSourcePitches, 256, "fa9c36377ef40a7c");
  return cases;
}

class PlanarToPackedRgbKernels : public ::testing::TestWithParam<PlanarToPackedRgbCase> {};

TEST_P(PlanarToPackedRgbKernels, MatchesIndependentPackedLayout) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bytes_per_component == 1) {
    run_planar_to_packed_rgb_case<std::uint8_t>(test_case);
  } else {
    run_planar_to_packed_rgb_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(PlanarToPacked, PlanarToPackedRgbKernels,
                         ::testing::ValuesIn(planar_to_packed_rgb_cases()),
                         [](const ::testing::TestParamInfo<PlanarToPackedRgbCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
