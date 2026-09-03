#include <gtest/gtest.h>

#include "convert_rgb_to_yuv444_test_helpers.h"

#include <vector>

namespace avsut::test {
namespace {

std::vector<PublicRgbToYuv444Case> public_rgb_to_yuv444_cases() {
  return {
      make_public_rgb_to_yuv444_case(VideoInfo::CS_RGBP, 8, 10, AVS_MATRIX_BT709,
                                     "709:limited", true, false, true, false, 11, 3),
      make_public_rgb_to_yuv444_case(VideoInfo::CS_RGBAP16, 16, 16, AVS_MATRIX_BT2020_NCL,
                                     "2020:full", false, true, false, true, 9, 3),
      make_public_rgb_to_yuv444_case(VideoInfo::CS_RGBAP16, 16, 10, AVS_MATRIX_BT709,
                                     "709:full", true, true, false, true, 11, 3),
      make_public_rgb_to_yuv444_case(VideoInfo::CS_RGBAPS, 32, 10, AVS_MATRIX_BT709,
                                     "709:full", true, true, false, true, 11, 3),
      make_public_rgb_to_yuv444_case(VideoInfo::CS_RGBPS, 32, 32, AVS_MATRIX_BT709,
                                     "709:full", true, true, true, false, 13, 3),
      make_public_rgb_to_yuv444_case(VideoInfo::CS_RGBAPS, 32, 32, AVS_MATRIX_BT709,
                                     "709:full", true, true, false, true, 13, 3),
  };
}

class PublicRgbToYuv444 : public ::testing::TestWithParam<PublicRgbToYuv444Case> {};

TEST_P(PublicRgbToYuv444, MatchesIndependentMatrixReferenceAndPreservesAlpha) {
  run_public_rgb_to_yuv444_color_case(GetParam());
}

INSTANTIATE_TEST_SUITE_P(
    Contracts, PublicRgbToYuv444, ::testing::ValuesIn(public_rgb_to_yuv444_cases()),
    [](const ::testing::TestParamInfo<PublicRgbToYuv444Case>& info) { return info.param.name; });

TEST(PublicRgbToYuv444, MapsGrayEndpointsAndKeepsChromaNeutral) {
  const auto test_case = make_public_rgb_to_yuv444_case(
      VideoInfo::CS_RGBP, 8, 10, AVS_MATRIX_BT709, "709:limited", true, false, true, false, 11,
      3);
  run_public_rgb_to_yuv444_grayscale_case(test_case);
}

TEST(PublicRgbToYuv444, KeepsRgbToYuvToRgbErrorBounded) {
  run_public_rgb_to_yuv444_roundtrip_case();
}

}  // namespace
}  // namespace avsut::test
