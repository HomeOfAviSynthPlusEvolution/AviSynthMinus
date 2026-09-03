#include <gtest/gtest.h>

#include "convert_rgb_to_y_test_helpers.h"

#include "support/cpu_features.h"

#include <vector>

namespace avsut::test {
namespace {

void add_rgb_to_y_variants(std::vector<RgbToYCase>& cases, int matrix, bool source_full,
                           bool destination_full, int source_bit_depth, int target_bit_depth,
                           bool force_float, std::size_t width = 32, std::size_t height = 3) {
  const auto source_pitch = source_bit_depth == 8 ? 64U : (source_bit_depth == 32 ? 256U : 128U);
  const auto destination_pitch = target_bit_depth == 8
                                    ? 64U
                                    : (target_bit_depth == 32 ? 256U : 128U);
  cases.push_back(make_rgb_to_y_case(
      matrix, source_full, destination_full, source_bit_depth, target_bit_depth, force_float,
      width, height, source_pitch, destination_pitch,
      Variant<RgbToYVariant>{"c", RgbToYVariant::C, IsaRequirement::Scalar}));
  cases.push_back(make_rgb_to_y_case(
      matrix, source_full, destination_full, source_bit_depth, target_bit_depth, force_float,
      width, height, source_pitch, destination_pitch,
      Variant<RgbToYVariant>{"sse2", RgbToYVariant::Sse2, IsaRequirement::Sse2}));
  cases.push_back(make_rgb_to_y_case(
      matrix, source_full, destination_full, source_bit_depth, target_bit_depth, force_float,
      width, height, source_pitch, destination_pitch,
      Variant<RgbToYVariant>{"avx2", RgbToYVariant::Avx2, IsaRequirement::Avx2}));
}

std::vector<RgbToYCase> rgb_to_y_cases() {
  std::vector<RgbToYCase> cases;
  add_rgb_to_y_variants(cases, AVS_MATRIX_BT709, true, false, 8, 8, false);
  add_rgb_to_y_variants(cases, AVS_MATRIX_BT709, true, false, 8, 8, true);
  add_rgb_to_y_variants(cases, AVS_MATRIX_BT709, true, false, 8, 10, false);
  add_rgb_to_y_variants(cases, AVS_MATRIX_BT470_BG, false, true, 10, 10, false);
  add_rgb_to_y_variants(cases, AVS_MATRIX_BT470_BG, false, true, 10, 10, true);
  add_rgb_to_y_variants(cases, AVS_MATRIX_BT470_BG, false, true, 10, 16, false);
  add_rgb_to_y_variants(cases, AVS_MATRIX_BT2020_NCL, false, false, 16, 8, false);
  add_rgb_to_y_variants(cases, AVS_MATRIX_BT2020_NCL, true, true, 16, 16, true);
  add_rgb_to_y_variants(cases, AVS_MATRIX_BT709, false, true, 32, 32, false);
  add_rgb_to_y_variants(cases, AVS_MATRIX_BT709, true, false, 32, 32, true);
  return cases;
}

class RgbToYLumaKernels : public ::testing::TestWithParam<RgbToYCase> {};

TEST_P(RgbToYLumaKernels, MatchesIndependentLumaReferenceAcrossInstructionSets) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_rgb_to_y_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, RgbToYLumaKernels, ::testing::ValuesIn(rgb_to_y_cases()),
                         [](const ::testing::TestParamInfo<RgbToYCase>& info) {
                           return info.param.name;
                         });

std::vector<PublicRgbToYCase> public_rgb_to_y_cases() {
  return {
      make_public_rgb_to_y_case(VideoInfo::CS_RGBP, 8, 8, true, false, false, 32, 3),
      make_public_rgb_to_y_case(VideoInfo::CS_RGBP, 8, 10, true, false, true, 32, 3),
      make_public_rgb_to_y_case(VideoInfo::CS_RGBP10, 10, 16, false, true, false, 32, 3),
      make_public_rgb_to_y_case(VideoInfo::CS_RGBP16, 16, 8, false, false, false, 32, 3),
      make_public_rgb_to_y_case(VideoInfo::CS_RGBPS, 32, 32, true, true, true, 32, 3),
  };
}

class PublicRgbToYLuma : public ::testing::TestWithParam<PublicRgbToYCase> {};

TEST_P(PublicRgbToYLuma, DispatchesToYOnlyAndAppliesTargetRangeOnce) {
  run_public_rgb_to_y_case(GetParam());
}

INSTANTIATE_TEST_SUITE_P(Dispatch, PublicRgbToYLuma,
                         ::testing::ValuesIn(public_rgb_to_y_cases()),
                         [](const ::testing::TestParamInfo<PublicRgbToYCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
