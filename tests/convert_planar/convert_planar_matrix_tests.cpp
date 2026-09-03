#include <gtest/gtest.h>

#include "convert_planar_matrix_test_helpers.h"

#include <array>
#include <vector>

namespace avsut::test {
namespace {

void add_planar_matrix_variants(std::vector<PlanarMatrixCase>& cases,
                                PlanarMatrixDirection direction, int matrix, bool source_full,
                                bool destination_full, std::array<std::string, 3> expected_hashes,
                                int source_bit_depth = 8, std::size_t width = 32,
                                std::size_t source_pitch = 64,
                                std::size_t destination_pitch = 64,
                                int target_bit_depth = -1) {
  constexpr std::size_t height = 5;
  cases.push_back(make_planar_matrix_case(
      direction, matrix, source_full, destination_full, width, height, source_pitch,
      destination_pitch,
      Variant<PlanarMatrixVariant>{"c", PlanarMatrixVariant::C, IsaRequirement::Scalar},
      expected_hashes, source_bit_depth, target_bit_depth));
  cases.push_back(make_planar_matrix_case(
      direction, matrix, source_full, destination_full, width, height, source_pitch,
      destination_pitch,
      Variant<PlanarMatrixVariant>{"sse2", PlanarMatrixVariant::Sse2, IsaRequirement::Sse2},
      expected_hashes, source_bit_depth, target_bit_depth));
  cases.push_back(make_planar_matrix_case(
      direction, matrix, source_full, destination_full, width, height, source_pitch,
      destination_pitch,
      Variant<PlanarMatrixVariant>{"avx2", PlanarMatrixVariant::Avx2, IsaRequirement::Avx2},
      std::move(expected_hashes), source_bit_depth, target_bit_depth));
}

std::vector<PlanarMatrixCase> planar_matrix_cases() {
  std::vector<PlanarMatrixCase> cases;
  add_planar_matrix_variants(cases, PlanarMatrixDirection::YuvToRgb, AVS_MATRIX_BT709, false, true,
                             {"9220436c8c8beea6", "9dca5bdef66702d1", "65ec7e8bf2be60ca"});
  add_planar_matrix_variants(cases, PlanarMatrixDirection::YuvToRgb, AVS_MATRIX_BT470_BG, true, true,
                             {"34591881d47524ae", "9d30f5cdc314dbbb", "a9be29014e3c2cdc"});
  add_planar_matrix_variants(cases, PlanarMatrixDirection::YuvToRgb, AVS_MATRIX_BT470_BG, true, false,
                             {"22e3df3aa66892f0", "740fccc45c01d676", "340f68e1baac4ca5"});
  add_planar_matrix_variants(cases, PlanarMatrixDirection::YuvToRgb, AVS_MATRIX_BT470_BG, false, false,
                             {"a4104bc5c63ff6e8", "6184e70f37f69207", "43739202bd94f0be"});
  add_planar_matrix_variants(cases, PlanarMatrixDirection::YuvToRgb, AVS_MATRIX_BT2020_NCL, false,
                             true, {"89c7146fd04957ad", "c9b6c786aab11e8b", "cb0bf966f007078e"}, 10);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::YuvToRgb, AVS_MATRIX_BT2020_CL, true,
                             false, {"3c5c71c1ed9df96d", "11d3c48091578d0a", "d4b8af3d4b390109"}, 16);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::YuvToRgb, AVS_MATRIX_BT470_M, false, true,
                             {"07872cb96b268fdf", "9dca5bdef66702d1", "3f577d3ee1d1ff5a"});
  add_planar_matrix_variants(cases, PlanarMatrixDirection::YuvToRgb, AVS_MATRIX_ST240_M, true, true,
                             {"b907cb80f155fdb3", "1593f46b40af3cd4", "d460e9bc7069605a"});
  add_planar_matrix_variants(cases, PlanarMatrixDirection::RgbToYuv, AVS_MATRIX_BT470_BG, true,
                             false, {"31d45deab1a469cd", "5daee92036e3c06d", "3d34e7e301d4927a"});
  add_planar_matrix_variants(cases, PlanarMatrixDirection::YuvToRgb, AVS_MATRIX_BT709, true, false,
                             {"6085b145b00a60c1", "68dddb121ad246fd", "90795b6cae10d8de"}, 10);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::RgbToYuv, AVS_MATRIX_BT470_BG, false, true,
                             {"65e2a12fe84cea1d", "3379d27f592ddd5c", "8d424504f0c3799d"}, 16);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::RgbToYuv, AVS_MATRIX_BT709, false, true,
                             {"caca96eeaa4b780f", "d1524218df470e54", "10ca21aec7492267"}, 8, 33,
                             96, 96);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::RgbToYuv, AVS_MATRIX_BT2020_NCL, true,
                             false, {"6b72b430feb18ebc", "b5f7cb89443278f1", "863ac6887866f541"},
                             10, 17, 64, 64);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::RgbToYuv, AVS_MATRIX_BT2020_CL, false,
                             true, {"26b967d36501441e", "f45201db0c6bd47c", "34e3e30116b5ebcf"},
                             16, 17, 96, 96);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::RgbToYuv, AVS_MATRIX_BT470_M, true,
                             true, {"5d5ad3fb129365cd", "102df1da2f3a9040", "6cf9d8bc5f67825f"},
                             8, 19, 96, 96);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::RgbToYuv, AVS_MATRIX_ST240_M, true,
                             false, {"f86e7a501ed3bb4a", "646ebe92f6151f71", "89e258d609591c3e"},
                             8, 17, 96, 96);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::YuvToRgb, AVS_MATRIX_BT2020_CL, false,
                             true, {}, 32, 17, 128, 128);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::RgbToYuv, AVS_MATRIX_ST240_M, true,
                             false, {}, 32, 19, 128, 128);
  add_planar_matrix_variants(cases, PlanarMatrixDirection::RgbToYuv, AVS_MATRIX_BT709, true,
                             false, {}, 8, 33, 64, 128, 10);
  for (const int source_bit_depth : {8, 10, 12, 14}) {
    const auto source_pitch = source_bit_depth == 8 ? 64U : 128U;
    add_planar_matrix_variants(cases, PlanarMatrixDirection::RgbToYuv, AVS_MATRIX_BT709, true,
                               false, {}, source_bit_depth, 33, source_pitch, 128, 16);
  }
  return cases;
}

class PlanarMatrixKernels : public ::testing::TestWithParam<PlanarMatrixCase> {};

TEST_P(PlanarMatrixKernels, MatchesIndependentMatrixReferenceAcrossInstructionSets) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_planar_matrix_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, PlanarMatrixKernels, ::testing::ValuesIn(planar_matrix_cases()),
                         [](const ::testing::TestParamInfo<PlanarMatrixCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
