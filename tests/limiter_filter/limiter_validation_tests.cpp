#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_LOCAL_UNDEF_AVS_UNUSED
#endif
#include "limiter/limiter.h"
using aif::filters::limiter::Limiter;
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

struct LimiterInputCase {
  const char* name;
  float min_luma;
  float max_luma;
  float min_chroma;
  float max_chroma;
};

void PrintTo(const LimiterInputCase& test_case, std::ostream* output) {
  *output << test_case.name;
}

class LimiterConstruction : public ::testing::TestWithParam<LimiterInputCase> {};

TEST_P(LimiterConstruction, RejectsNonFiniteIntervals) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_static_source(environment, VideoInfo::CS_YV24);
  const auto& test_case = GetParam();

  EXPECT_THROW(
      {
        Limiter filter(source.clip, test_case.min_luma, test_case.max_luma, test_case.min_chroma, test_case.max_chroma,
                       0, false, environment.get());
      },
      AvisynthError)
      << "Limiter case=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot)
      << "Limiter case=" << test_case.name << " modified its source";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty())
      << "Limiter case=" << test_case.name << " requested a frame during construction";
}

INSTANTIATE_TEST_SUITE_P(BoundaryCases, LimiterConstruction,
                         ::testing::Values(LimiterInputCase{"NaNMinimumLuma", std::numeric_limits<float>::quiet_NaN(),
                                                            235.0F, 16.0F, 240.0F},
                                           LimiterInputCase{"InfiniteMaximumChroma", 16.0F, 235.0F, 16.0F,
                                                            std::numeric_limits<float>::infinity()}),
                         [](const ::testing::TestParamInfo<LimiterInputCase>& info) { return info.param.name; });

} // namespace
} // namespace avsut::test
