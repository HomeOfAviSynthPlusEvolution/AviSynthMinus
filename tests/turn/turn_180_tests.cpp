#include <gtest/gtest.h>

#include "turn_test_helpers.h"

#include "filters/intel/turn_sse.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

void add_180_cases(std::vector<TurnCase>& cases, const char* format, std::size_t bytes_per_pixel,
                   std::size_t width_pixels, std::size_t height_pixels, std::size_t pitch,
                   std::uint32_t seed, const char* expected_hash, TurnFuncPtr scalar,
                   TurnFuncPtr sse2, TurnFuncPtr ssse3) {
  cases.push_back(make_turn_case(
      format, TurnDirection::Half, width_pixels, height_pixels, bytes_per_pixel, pitch, pitch, seed,
      scalar, Variant<TurnFuncPtr>{"c", scalar, IsaRequirement::Scalar}, expected_hash));
  cases.push_back(make_turn_case(
      format, TurnDirection::Half, width_pixels, height_pixels, bytes_per_pixel, pitch, pitch, seed,
      scalar, Variant<TurnFuncPtr>{"sse2", sse2, IsaRequirement::Sse2}, expected_hash));
  if (ssse3 != nullptr) {
    cases.push_back(make_turn_case(
        format, TurnDirection::Half, width_pixels, height_pixels, bytes_per_pixel, pitch, pitch,
        seed, scalar, Variant<TurnFuncPtr>{"ssse3", ssse3, IsaRequirement::Ssse3}, expected_hash));
  }
}

std::vector<TurnCase> turn_180_cases() {
  std::vector<TurnCase> cases;
  add_180_cases(cases, "Plane8", 1, 33, 9, 40, 0x1800BEEFU, "27fe2002b6d13934",
                turn_180_plane_c<std::uint8_t>, turn_180_plane_sse2<std::uint8_t>,
                turn_180_plane_ssse3<std::uint8_t>);
  add_180_cases(cases, "Plane16", 2, 17, 7, 40, 0x1800C0DEU, "e1d7bc0fe504321c",
                turn_180_plane_c<std::uint16_t>, turn_180_plane_sse2<std::uint16_t>,
                turn_180_plane_ssse3<std::uint16_t>);
  add_180_cases(cases, "Plane32", 4, 9, 7, 48, 0x180032BEU, "1505fea3d7e012a6",
                turn_180_plane_c<std::uint32_t>, turn_180_plane_sse2<std::uint32_t>, nullptr);
  add_180_cases(cases, "Plane64", 8, 5, 7, 48, 0x180064BEU, "fc2d297705cb1972",
                turn_180_plane_c<std::uint64_t>, turn_180_plane_sse2<std::uint64_t>, nullptr);
  return cases;
}

class Turn180Kernels : public ::testing::TestWithParam<TurnCase> {};

TEST_P(Turn180Kernels, MatchesReferenceAndScalar) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_turn_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, Turn180Kernels, ::testing::ValuesIn(turn_180_cases()),
                         [](const ::testing::TestParamInfo<TurnCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
