#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_LOCAL_UNDEF_AVS_UNUSED
#endif
#include "convolution/general_convolution.h"
using aif::filters::convolution::GeneralConvolution;
#ifdef AVSUT_LOCAL_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_LOCAL_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <array>
#include <cstdint>
#include <limits>
#include <ostream>
#include <vector>

namespace avsut::test {
namespace {

struct StaticVideoSource {
  VideoInfo video_info;
  PVideoFrame frame;
  StaticFrameClip* clip_impl;
  PClip clip;
  FrameSnapshot snapshot;
};

StaticVideoSource make_static_source(AviSynthEnvironment& environment, int pixel_type) {
  const auto video_info = make_video_info(VideoInfoSpec{8, 4, pixel_type, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  if (video_info.IsYUY2()) {
    fill_plane_full_pitch(frame, 0x60, DEFAULT_PLANE);
  } else {
    fill_plane_full_pitch(frame, 0x60, PLANAR_Y);
    fill_plane_full_pitch(frame, 0x80, PLANAR_U);
    fill_plane_full_pitch(frame, 0x80, PLANAR_V);
  }
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl), FrameSnapshot::capture(frame, video_info)};
}

struct ConvolutionInputCase {
  const char* name;
  double divisor;
  float bias;
  const char* matrix;
};

void PrintTo(const ConvolutionInputCase& test_case, std::ostream* output) {
  *output << test_case.name;
}

class GeneralConvolutionConstruction : public ::testing::TestWithParam<ConvolutionInputCase> {};

TEST_P(GeneralConvolutionConstruction, RejectsNonFiniteSetupValues) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_static_source(environment, VideoInfo::CS_Y8);
  const auto& test_case = GetParam();

  EXPECT_THROW(
      {
        GeneralConvolution filter(source.clip, test_case.divisor, test_case.bias, test_case.matrix, false, true, false,
                                  false, environment.get());
      },
      AvisynthError)
      << "GeneralConvolution case=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot)
      << "GeneralConvolution case=" << test_case.name << " modified its source";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty())
      << "GeneralConvolution case=" << test_case.name << " requested a frame during construction";
}

INSTANTIATE_TEST_SUITE_P(
    BoundaryCases, GeneralConvolutionConstruction,
    ::testing::Values(
        ConvolutionInputCase{"NaNDivisor", std::numeric_limits<double>::quiet_NaN(), 0.0F, "0 0 0 0 1 0 0 0 0"},
        ConvolutionInputCase{"InfiniteDivisor", std::numeric_limits<double>::infinity(), 0.0F, "0 0 0 0 1 0 0 0 0"},
        ConvolutionInputCase{"NaNBias", 1.0, std::numeric_limits<float>::quiet_NaN(), "0 0 0 0 1 0 0 0 0"},
        ConvolutionInputCase{"InfiniteMatrixCoefficient", 1.0, 0.0F, "0 0 0 0 inf 0 0 0 0"}),
    [](const ::testing::TestParamInfo<ConvolutionInputCase>& info) { return info.param.name; });

} // namespace
} // namespace avsut::test
