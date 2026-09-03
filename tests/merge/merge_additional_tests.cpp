#include <gtest/gtest.h>

#include "merge_additional_test_helpers.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

std::vector<Yuy2MergeCase> yuy2_merge_cases() {
  return {
      make_yuy2_merge_case(Yuy2MergeOperation::WeightedChroma, 38, 7, 64, 80, 12345,
                           weighted_merge_chroma_yuy2_sse2, nullptr, "3a909c858b94e7b4"),
      make_yuy2_merge_case(Yuy2MergeOperation::WeightedLuma, 38, 7, 64, 80, 21845,
                           weighted_merge_luma_yuy2_sse2, nullptr, "cbeda666b7e96b07"),
      make_yuy2_merge_case(Yuy2MergeOperation::ReplaceLuma, 38, 7, 64, 80, 0, nullptr,
                           replace_luma_yuy2_sse2, "8e6b9cb2c59b8b31"),
      make_yuy2_merge_case(Yuy2MergeOperation::WeightedChroma, 62, 11, 80, 112, 9362,
                           weighted_merge_chroma_yuy2_sse2, nullptr, "4e05986fb408e05c",
                           0xF30F2401U),
      make_yuy2_merge_case(Yuy2MergeOperation::WeightedLuma, 62, 11, 80, 112, 24576,
                           weighted_merge_luma_yuy2_sse2, nullptr, "45e640f53da49d18",
                           0xF30F2401U),
      make_yuy2_merge_case(Yuy2MergeOperation::ReplaceLuma, 62, 11, 80, 112, 0, nullptr,
                           replace_luma_yuy2_sse2, "abf4a7ec690ce784", 0xF30F2401U),
  };
}

class MergeYuy2Kernels : public ::testing::TestWithParam<Yuy2MergeCase> {};

TEST_P(MergeYuy2Kernels, MatchesIndependentLayoutReference) {
  if (!CpuFeatures::detect().supports(IsaRequirement::Sse2)) {
    GTEST_SKIP() << "host does not support sse2";
  }
  run_yuy2_merge_case(GetParam());
}

INSTANTIATE_TEST_SUITE_P(Kernels, MergeYuy2Kernels, ::testing::ValuesIn(yuy2_merge_cases()),
                         [](const ::testing::TestParamInfo<Yuy2MergeCase>& info) {
                           return info.param.name;
                         });

std::vector<AveragePlaneCase> average_plane_cases() {
  return {
      make_average_plane_case(
          AverageSample::UInt8, 32, 3, 64, 96, 1,
          Variant<AveragePlaneFunc>{"sse2", average_plane_sse2<std::uint8_t>, IsaRequirement::Sse2},
          "1e58bdb8437075dd"),
      make_average_plane_case(
          AverageSample::UInt8, 32, 3, 64, 96, 1,
          Variant<AveragePlaneFunc>{"avx2", average_plane_avx2<std::uint8_t>, IsaRequirement::Avx2},
          "1e58bdb8437075dd"),
      make_average_plane_case(
          AverageSample::UInt8, 47, 5, 64, 80, 3,
          Variant<AveragePlaneFunc>{"sse2", average_plane_sse2<std::uint8_t>, IsaRequirement::Sse2},
          "bacbdf7c7b8f0899"),
      make_average_plane_case(
          AverageSample::UInt8, 47, 5, 64, 80, 3,
          Variant<AveragePlaneFunc>{"avx2", average_plane_avx2<std::uint8_t>, IsaRequirement::Avx2},
          "bacbdf7c7b8f0899"),
      make_average_plane_case(AverageSample::UInt16, 23, 5, 64, 80, 2,
                              Variant<AveragePlaneFunc>{"sse2", average_plane_sse2<std::uint16_t>,
                                                        IsaRequirement::Sse2},
                              "57bce2ac3dff058f"),
      make_average_plane_case(AverageSample::UInt16, 23, 5, 64, 80, 2,
                              Variant<AveragePlaneFunc>{"avx2", average_plane_avx2<std::uint16_t>,
                                                        IsaRequirement::Avx2},
                              "57bce2ac3dff058f"),
      make_average_plane_case(AverageSample::UInt16, 16, 3, 64, 96, 2,
                              Variant<AveragePlaneFunc>{"sse2", average_plane_sse2<std::uint16_t>,
                                                        IsaRequirement::Sse2},
                              "358fb6484b31ad2f"),
      make_average_plane_case(AverageSample::UInt16, 16, 3, 64, 96, 2,
                              Variant<AveragePlaneFunc>{"avx2", average_plane_avx2<std::uint16_t>,
                                                        IsaRequirement::Avx2},
                              "358fb6484b31ad2f"),
      make_average_plane_case(
          AverageSample::Float, 17, 5, 80, 96, 4,
          Variant<AveragePlaneFunc>{"sse2", average_plane_sse2_float, IsaRequirement::Sse2}),
      make_average_plane_case(
          AverageSample::Float, 16, 3, 64, 96, 4,
          Variant<AveragePlaneFunc>{"sse2", average_plane_sse2_float, IsaRequirement::Sse2}),
      make_average_plane_case(
          AverageSample::Float, 17, 5, 80, 96, 4,
          Variant<AveragePlaneFunc>{"avx2", average_plane_avx2_float, IsaRequirement::Avx2}),
      make_average_plane_case(
          AverageSample::Float, 16, 3, 64, 96, 4,
          Variant<AveragePlaneFunc>{"avx2", average_plane_avx2_float, IsaRequirement::Avx2}),
      make_average_plane_case(
          AverageSample::UInt8, 53, 7, 80, 112, 5,
          Variant<AveragePlaneFunc>{"sse2", average_plane_sse2<std::uint8_t>, IsaRequirement::Sse2},
          "1d1bad9bea2f878d", 0xF30F2402U),
      make_average_plane_case(
          AverageSample::UInt8, 53, 7, 80, 112, 5,
          Variant<AveragePlaneFunc>{"avx2", average_plane_avx2<std::uint8_t>, IsaRequirement::Avx2},
          "1d1bad9bea2f878d", 0xF30F2402U),
      make_average_plane_case(
          AverageSample::UInt16, 27, 7, 80, 96, 6,
          Variant<AveragePlaneFunc>{"sse2", average_plane_sse2<std::uint16_t>, IsaRequirement::Sse2},
          "2f7fdcfcf841734e", 0xF30F2403U),
      make_average_plane_case(
          AverageSample::UInt16, 27, 7, 80, 96, 6,
          Variant<AveragePlaneFunc>{"avx2", average_plane_avx2<std::uint16_t>, IsaRequirement::Avx2},
          "2f7fdcfcf841734e", 0xF30F2403U),
      make_average_plane_case(
          AverageSample::Float, 21, 7, 96, 112, 8,
          Variant<AveragePlaneFunc>{"sse2", average_plane_sse2_float, IsaRequirement::Sse2}, {},
          0xF30F2404U),
      make_average_plane_case(
          AverageSample::Float, 21, 7, 96, 112, 8,
          Variant<AveragePlaneFunc>{"avx2", average_plane_avx2_float, IsaRequirement::Avx2}, {},
          0xF30F2404U),
  };
}

class MergeAveragePlaneKernels : public ::testing::TestWithParam<AveragePlaneCase> {};

TEST_P(MergeAveragePlaneKernels, MatchesIndependentAverageReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_average_plane_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, MergeAveragePlaneKernels,
                         ::testing::ValuesIn(average_plane_cases()),
                         [](const ::testing::TestParamInfo<AveragePlaneCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
