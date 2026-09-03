#include <gtest/gtest.h>

#include "turn_test_helpers.h"

#include "filters/intel/turn_sse.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

void add_rgb_cases(std::vector<TurnCase>& cases, const char* format, std::size_t bytes_per_pixel,
                   std::size_t width_pixels, std::size_t height_pixels, std::size_t source_pitch,
                   std::size_t destination_pitch, std::uint32_t seed, const char* left_hash,
                   const char* right_hash, TurnFuncPtr left_c, TurnFuncPtr left_sse2,
                   TurnFuncPtr right_c, TurnFuncPtr right_sse2) {
  const auto add_direction = [&](TurnDirection direction, TurnFuncPtr scalar, TurnFuncPtr sse2) {
    const char* expected_hash = direction == TurnDirection::Left ? left_hash : right_hash;
    cases.push_back(make_turn_case(format, direction, width_pixels, height_pixels, bytes_per_pixel,
                                   source_pitch, destination_pitch, seed, scalar,
                                   Variant<TurnFuncPtr>{"c", scalar, IsaRequirement::Scalar},
                                   expected_hash, true));
    if (sse2 != nullptr) {
      cases.push_back(make_turn_case(format, direction, width_pixels, height_pixels,
                                     bytes_per_pixel, source_pitch, destination_pitch, seed, scalar,
                                     Variant<TurnFuncPtr>{"sse2", sse2, IsaRequirement::Sse2},
                                     expected_hash, true));
    }
  };

  add_direction(TurnDirection::Left, left_c, left_sse2);
  add_direction(TurnDirection::Right, right_c, right_sse2);
}

std::vector<TurnCase> rgb_cases() {
  std::vector<TurnCase> cases;
  add_rgb_cases(cases, "Rgb24", 3, 11, 7, 40, 24, 0x2400BEEFU, "88c85e5fc739395d",
                "b4b150e80b3ba553", turn_left_rgb24, nullptr, turn_right_rgb24, nullptr);
  add_rgb_cases(cases, "Rgb32", 4, 9, 11, 48, 64, 0x3200C0DEU, "5e1ac6fa24872d08",
                "15524f6ca6c60548", turn_left_rgb32_c, turn_left_rgb32_sse2,
                turn_right_rgb32_c, turn_right_rgb32_sse2);
  add_rgb_cases(cases, "Rgb48", 6, 7, 5, 48, 32, 0x4800BEEFU, "282ea5f3d6839382",
                "7e2eddb2be212b85", turn_left_rgb48_c, nullptr, turn_right_rgb48_c, nullptr);
  add_rgb_cases(cases, "Rgb64", 8, 5, 7, 48, 64, 0x6400C0DEU, "43aecb8338f174c5",
                "43839f955a957725", turn_left_rgb64_c, turn_left_rgb64_sse2,
                turn_right_rgb64_c, turn_right_rgb64_sse2);
  return cases;
}

class PackedRgbTurnKernels : public ::testing::TestWithParam<TurnCase> {};

TEST_P(PackedRgbTurnKernels, MatchesReferenceAndScalar) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_turn_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, PackedRgbTurnKernels, ::testing::ValuesIn(rgb_cases()),
                         [](const ::testing::TestParamInfo<TurnCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
