#include <gtest/gtest.h>

#include "convert_rgb_planar_test_helpers.h"

#include "support/cpu_features.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace avsut::test {
namespace {

using PackedToPlanarHashes = std::array<std::string, 4>;

template <typename T, bool TargetHasAlpha>
void add_rgb_to_rgbp_variants(std::vector<PackedToPlanarRgbCase>& cases, std::string operation,
                              std::size_t width, std::size_t height, std::size_t source_pitch,
                              std::array<std::size_t, 4> destination_pitches,
                              PackedToPlanarHashes expected_hashes) {
  cases.push_back(make_packed_to_planar_rgb_case(
      operation, sizeof(T), 3, TargetHasAlpha, width, height, source_pitch, destination_pitches,
      Variant<PackedToPlanarRgbFunction>{"ssse3", convert_rgb_to_rgbp_ssse3_adapter<T, TargetHasAlpha>,
                                         IsaRequirement::Ssse3},
      expected_hashes));
  cases.push_back(make_packed_to_planar_rgb_case(
      std::move(operation), sizeof(T), 3, TargetHasAlpha, width, height, source_pitch,
      destination_pitches,
      Variant<PackedToPlanarRgbFunction>{"avx2", convert_rgb_to_rgbp_avx2_adapter<T, TargetHasAlpha>,
                                         IsaRequirement::Avx2},
      std::move(expected_hashes)));
}

template <typename T, bool TargetHasAlpha>
void add_rgba_to_rgbp_variants(std::vector<PackedToPlanarRgbCase>& cases, std::string operation,
                               std::size_t width, std::size_t height, std::size_t source_pitch,
                               std::array<std::size_t, 4> destination_pitches,
                               PackedToPlanarHashes expected_hashes) {
  cases.push_back(make_packed_to_planar_rgb_case(
      std::move(operation), sizeof(T), 4, TargetHasAlpha, width, height, source_pitch,
      destination_pitches,
      Variant<PackedToPlanarRgbFunction>{"ssse3", convert_rgba_to_rgbp_ssse3<T, TargetHasAlpha>,
                                         IsaRequirement::Ssse3},
      std::move(expected_hashes)));
}

std::vector<PackedToPlanarRgbCase> packed_to_planar_rgb_cases() {
  constexpr std::array<std::size_t, 4> kDestinationPitches{64, 128, 192, 256};
  std::vector<PackedToPlanarRgbCase> cases;
  add_rgb_to_rgbp_variants<std::uint8_t, false>(
      cases, "Rgb24ToRgbp", 45, 5, 192, kDestinationPitches,
      {"51d772b3d2a5921c", "fa435e9fbaca6195", "e139506c167e8206", ""});
  add_rgb_to_rgbp_variants<std::uint8_t, true>(
      cases, "Rgb24ToRgbap", 45, 5, 192, kDestinationPitches,
      {"51d772b3d2a5921c", "fa435e9fbaca6195", "e139506c167e8206", "f96717f53740373f"});
  add_rgba_to_rgbp_variants<std::uint8_t, false>(
      cases, "Rgb32ToRgbp", 32, 5, 192, kDestinationPitches,
      {"91ca9d4a3043e390", "cc4f44e656c5177e", "47557bc00ddad30e", ""});
  add_rgba_to_rgbp_variants<std::uint8_t, true>(
      cases, "Rgb32ToRgbap", 32, 5, 192, kDestinationPitches,
      {"91ca9d4a3043e390", "cc4f44e656c5177e", "47557bc00ddad30e", "665993527e29788e"});
  add_rgb_to_rgbp_variants<std::uint16_t, false>(
      cases, "Rgb48ToRgbp16", 21, 5, 192, kDestinationPitches,
      {"517d265a7bd28966", "df4a09dc225d9a1b", "737e639a958fa548", ""});
  add_rgb_to_rgbp_variants<std::uint16_t, true>(
      cases, "Rgb48ToRgbap16", 21, 5, 192, kDestinationPitches,
      {"517d265a7bd28966", "df4a09dc225d9a1b", "737e639a958fa548", "507acb1b1f60b28d"});
  add_rgba_to_rgbp_variants<std::uint16_t, false>(
      cases, "Rgb64ToRgbp16", 16, 5, 192, kDestinationPitches,
      {"2c39ff371531d2fe", "65688e14fd0b2b1a", "236b9a693d5a25bf", ""});
  add_rgba_to_rgbp_variants<std::uint16_t, true>(
      cases, "Rgb64ToRgbap16", 16, 5, 192, kDestinationPitches,
      {"2c39ff371531d2fe", "65688e14fd0b2b1a", "236b9a693d5a25bf", "e8f69f09c9dcbe10"});
  return cases;
}

class PackedToPlanarRgbKernels : public ::testing::TestWithParam<PackedToPlanarRgbCase> {};

TEST_P(PackedToPlanarRgbKernels, MatchesIndependentPackedLayout) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bytes_per_component == 1) {
    run_packed_to_planar_rgb_case<std::uint8_t>(test_case);
  } else {
    run_packed_to_planar_rgb_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(PackedToPlanar, PackedToPlanarRgbKernels,
                         ::testing::ValuesIn(packed_to_planar_rgb_cases()),
                         [](const ::testing::TestParamInfo<PackedToPlanarRgbCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
