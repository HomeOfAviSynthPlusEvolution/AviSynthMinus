#include <gtest/gtest.h>

#include "support/guarded_video_buffer.h"
#include "turn_test_helpers.h"

#include "filters/intel/turn_sse.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

void add_plane_cases(std::vector<TurnCase>& cases, const char* format, std::size_t bytes_per_pixel,
                     std::size_t width_pixels, std::size_t height_pixels, std::size_t source_pitch,
                     std::size_t destination_pitch, std::uint32_t seed, const char* left_hash,
                     const char* right_hash, TurnFuncPtr left_c, TurnFuncPtr left_sse2,
                     TurnFuncPtr right_c, TurnFuncPtr right_sse2) {
  const auto add_direction = [&](TurnDirection direction, TurnFuncPtr scalar, TurnFuncPtr sse2) {
    const char* expected_hash = direction == TurnDirection::Left ? left_hash : right_hash;
    cases.push_back(make_turn_case(format, direction, width_pixels, height_pixels, bytes_per_pixel,
                                   source_pitch, destination_pitch, seed, scalar,
                                   Variant<TurnFuncPtr>{"c", scalar, IsaRequirement::Scalar},
                                   expected_hash));
    if (sse2 != nullptr) {
      cases.push_back(make_turn_case(format, direction, width_pixels, height_pixels, bytes_per_pixel,
                                     source_pitch, destination_pitch, seed, scalar,
                                     Variant<TurnFuncPtr>{"sse2", sse2, IsaRequirement::Sse2},
                                     expected_hash));
    }
  };

  add_direction(TurnDirection::Left, left_c, left_sse2);
  add_direction(TurnDirection::Right, right_c, right_sse2);
}

std::vector<TurnCase> plane_cases() {
  std::vector<TurnCase> cases;
  add_plane_cases(cases, "Plane8", 1, 33, 17, 40, 32, 0xC0FFEEU, "9d4f11c702db4abb",
                  "6bd054de3d79c781", turn_left_plane_8_c, turn_left_plane_8_sse2,
                  turn_right_plane_8_c, turn_right_plane_8_sse2);
  add_plane_cases(cases, "Plane16", 2, 17, 19, 40, 48, 0x1600BEEFU, "db4e5f13c5b07ed5",
                  "27f43f7d2e758b85", turn_left_plane_16_c, turn_left_plane_16_sse2,
                  turn_right_plane_16_c, turn_right_plane_16_sse2);
  add_plane_cases(cases, "Plane32", 4, 9, 11, 48, 64, 0x3200BEEFU, "8f8457e88990cdf5",
                  "fbc817cd498ed5b1", turn_left_plane_32_c, turn_left_plane_32_sse2,
                  turn_right_plane_32_c, turn_right_plane_32_sse2);
  return cases;
}

class PlaneTurnKernels : public ::testing::TestWithParam<TurnCase> {};

TEST(PlaneTurn, MapsCoordinatesForSmallFrame) {
  const auto test_case =
      make_turn_case("Plane8", TurnDirection::Left, 3, 2, 1, 8, 8, 0x1234U, turn_left_plane_8_c,
                     Variant<TurnFuncPtr>{"c", turn_left_plane_8_c, IsaRequirement::Scalar});
  GuardedVideoBuffer<std::uint8_t> source(3, 2, 8);
  GuardedVideoBuffer<std::uint8_t> expected(2, 3, 8);
  GuardedVideoBuffer<std::uint8_t> actual(2, 3, 8);
  fill_incrementing(source.view());

  map_turn_reference(test_case, source.view().as_const(), expected.view());
  invoke_turn(test_case.variant.function, source.view().as_const(), actual.view());

  EXPECT_TRUE(compare_turn_pixels(test_case, expected.view().as_const(), actual.view().as_const()));
  EXPECT_TRUE(source.memory_intact());
  EXPECT_TRUE(expected.memory_intact());
  EXPECT_TRUE(actual.memory_intact());
}

TEST_P(PlaneTurnKernels, MatchesReferenceAndScalar) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_turn_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, PlaneTurnKernels, ::testing::ValuesIn(plane_cases()),
                         [](const ::testing::TestParamInfo<TurnCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
