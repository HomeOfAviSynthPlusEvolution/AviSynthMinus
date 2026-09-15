#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_LOCAL_UNDEF_AVS_UNUSED
#endif
#include "filters/combine.h"
#include "filters/edit.h"
#include "filters/field.h"
#include "filters/fps.h"
#ifdef AVSUT_LOCAL_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_LOCAL_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

namespace avsut::test {
namespace {

struct VideoSource {
  VideoInfo video_info;
  PVideoFrame frame;
  StaticFrameClip* clip_impl;
  PClip clip;
  FrameSnapshot snapshot;
};

VideoSource make_y8_source(AviSynthEnvironment& environment) {
  const auto video_info = make_video_info(VideoInfoSpec{4, 4, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0x5a, PLANAR_Y);
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return VideoSource{video_info, frame, clip_impl, PClip(clip_impl), FrameSnapshot::capture(frame, video_info)};
}

struct FpsInputCase {
  const char* name;
  int numerator;
  int denominator;
};

void PrintTo(const FpsInputCase& test_case, std::ostream* output) {
  *output << test_case.name;
}

PClip create_assume_fps(PClip source, const FpsInputCase& test_case, IScriptEnvironment* environment) {
  const AVSValue args[4] = {source, test_case.numerator, test_case.denominator, false};
  return AssumeFPS::Create(AVSValue(args, 4), nullptr, environment).AsClip();
}

class AssumeFpsFactory : public ::testing::TestWithParam<FpsInputCase> {};

TEST_P(AssumeFpsFactory, RejectsNegativeIntegerRateParts) {
  AviSynthEnvironment environment;
  const VideoSource source = make_y8_source(environment);
  const auto& test_case = GetParam();

  EXPECT_THROW(create_assume_fps(source.clip, test_case, environment.get()), AvisynthError)
      << "AssumeFPS case=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot)
      << "AssumeFPS case=" << test_case.name << " modified its source";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty())
      << "AssumeFPS case=" << test_case.name << " requested a frame during construction";
}

INSTANTIATE_TEST_SUITE_P(BoundaryCases, AssumeFpsFactory,
                         ::testing::Values(FpsInputCase{"NegativeNumerator", -1, 1},
                                           FpsInputCase{"NegativeDenominator", 1, -1}),
                         [](const ::testing::TestParamInfo<FpsInputCase>& info) { return info.param.name; });

} // namespace
} // namespace avsut::test
