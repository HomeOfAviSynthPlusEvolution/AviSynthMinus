#include <gtest/gtest.h>

#include "convert_yuv444_rgb_test_helpers.h"

#include <vector>

namespace avsut::test {
namespace {

std::vector<PublicYuvToRgbCase> public_yuv_to_rgb_cases() {
  return {
      make_public_yuv_to_rgb_case(AVS_MATRIX_BT709, "709:limited", VideoInfo::CS_YV24, 8, 10,
                                  10, -1, false, false, true, false, 11, 3),
      make_public_yuv_to_rgb_case(AVS_MATRIX_BT2020_NCL, "2020:limited",
                                  VideoInfo::CS_YUVA444P16, 16, 16, -1, -2, false, false, true,
                                  true, 9, 3),
      make_public_yuv_to_rgb_case(AVS_MATRIX_BT2020_NCL, "2020:limited",
                                  VideoInfo::CS_YUVA444P16, 16, 10, 10, -2, false, false, true,
                                  true, 11, 3),
      make_public_yuv_to_rgb_case(AVS_MATRIX_BT709, "709:full", VideoInfo::CS_YUVA444PS, 32, 10,
                                  10, -2, true, true, true, true, 11, 3),
      make_public_yuv_to_rgb_case(AVS_MATRIX_BT709, "709:full", VideoInfo::CS_YUV444PS, 32, 32,
                                  -1, -1, true, true, true, false, 7, 2),
  };
}

class PublicYuv444ToRgb : public ::testing::TestWithParam<PublicYuvToRgbCase> {};

TEST_P(PublicYuv444ToRgb, MatchesIndependentReferenceAndPreservesSource) {
  run_public_yuv_to_rgb_case(GetParam());
}

INSTANTIATE_TEST_SUITE_P(
    Contracts, PublicYuv444ToRgb, ::testing::ValuesIn(public_yuv_to_rgb_cases()),
    [](const ::testing::TestParamInfo<PublicYuvToRgbCase>& info) { return info.param.name; });

}  // namespace
}  // namespace avsut::test
