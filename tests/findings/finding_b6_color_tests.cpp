#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B6_UNDEF_AVS_UNUSED
#endif
#include "filters/convolution.h"
#include "filters/focus.h"
#include "filters/limiter.h"
#ifdef AVSUT_FINDING_B6_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B6_UNDEF_AVS_UNUSED
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
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

TEST(SpatialSoftenConstruction, RejectsRadiusBeyondFixedRowPointerStorage) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_static_source(environment, VideoInfo::CS_YUY2);

  EXPECT_THROW(
      { SpatialSoften filter(source.clip, 33, 0, 0, environment.get()); }, AvisynthError)
      << "B6 SpatialSoften radius=33";
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot)
      << "B6 SpatialSoften radius=33 modified its source";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty())
      << "B6 SpatialSoften radius=33 requested a frame during construction";
}

struct LimiterInputCase {
  const char* name;
  float min_luma;
  float max_luma;
  float min_chroma;
  float max_chroma;
};

void PrintTo(const LimiterInputCase& test_case, std::ostream* output) { *output << test_case.name; }

class LimiterConstruction : public ::testing::TestWithParam<LimiterInputCase> {};

TEST_P(LimiterConstruction, RejectsInvertedOrNonFiniteIntervals) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_static_source(environment, VideoInfo::CS_YV24);
  const auto& test_case = GetParam();

  EXPECT_THROW(
      {
        Limiter filter(source.clip, test_case.min_luma, test_case.max_luma, test_case.min_chroma,
                       test_case.max_chroma, 0, false, environment.get());
      },
      AvisynthError)
      << "B6 Limiter case=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot)
      << "B6 Limiter case=" << test_case.name << " modified its source";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty())
      << "B6 Limiter case=" << test_case.name << " requested a frame during construction";
}

INSTANTIATE_TEST_SUITE_P(
    B6, LimiterConstruction,
    ::testing::Values(LimiterInputCase{"InvertedLuma", 200.0F, 100.0F, 16.0F, 240.0F},
                      LimiterInputCase{"InvertedChroma", 16.0F, 235.0F, 200.0F, 100.0F},
                      LimiterInputCase{"NaNMinimumLuma", std::numeric_limits<float>::quiet_NaN(),
                                       235.0F, 16.0F, 240.0F},
                      LimiterInputCase{"InfiniteMaximumChroma", 16.0F, 235.0F, 16.0F,
                                       std::numeric_limits<float>::infinity()}),
    [](const ::testing::TestParamInfo<LimiterInputCase>& info) { return info.param.name; });

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
        GeneralConvolution filter(source.clip, test_case.divisor, test_case.bias, test_case.matrix,
                                  false, true, false, false, environment.get());
      },
      AvisynthError)
      << "B6 GeneralConvolution case=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot)
      << "B6 GeneralConvolution case=" << test_case.name << " modified its source";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty())
      << "B6 GeneralConvolution case=" << test_case.name
      << " requested a frame during construction";
}

INSTANTIATE_TEST_SUITE_P(
    B6, GeneralConvolutionConstruction,
    ::testing::Values(
        ConvolutionInputCase{"NaNDivisor", std::numeric_limits<double>::quiet_NaN(), 0.0F,
                             "0 0 0 0 1 0 0 0 0"},
        ConvolutionInputCase{"InfiniteDivisor", std::numeric_limits<double>::infinity(), 0.0F,
                             "0 0 0 0 1 0 0 0 0"},
        ConvolutionInputCase{"NaNBias", 1.0, std::numeric_limits<float>::quiet_NaN(),
                             "0 0 0 0 1 0 0 0 0"},
        ConvolutionInputCase{"InfiniteMatrixCoefficient", 1.0, 0.0F, "0 0 0 0 inf 0 0 0 0"}),
    [](const ::testing::TestParamInfo<ConvolutionInputCase>& info) { return info.param.name; });

struct TemporalSource {
  VideoInfo video_info;
  std::vector<PVideoFrame> frames;
  FrameSequenceClip* clip_impl;
  PClip clip;
  std::vector<FrameSnapshot> snapshots;
};

TemporalSource make_temporal_source(AviSynthEnvironment& environment) {
  const auto video_info = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_YV24, 3, 25, 1});
  constexpr std::array<std::uint8_t, 3> kLumaValues{20, 100, 100};
  std::vector<PVideoFrame> frames;
  std::vector<FrameSnapshot> snapshots;
  frames.reserve(kLumaValues.size());
  snapshots.reserve(kLumaValues.size());
  for (const auto luma : kLumaValues) {
    PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
    fill_plane_full_pitch(frame, luma, PLANAR_Y);
    fill_plane_full_pitch(frame, 0x80, PLANAR_U);
    fill_plane_full_pitch(frame, 0x80, PLANAR_V);
    snapshots.push_back(FrameSnapshot::capture(frame, video_info));
    frames.push_back(frame);
  }
  auto* clip_impl = new FrameSequenceClip(video_info, frames);
  return TemporalSource{video_info, std::move(frames), clip_impl, PClip(clip_impl),
                        std::move(snapshots)};
}

TEST(TemporalSoften, NormalizesAboveMaximumThresholdBeforePlaneProcessing) {
  AviSynthEnvironment environment;
  const TemporalSource source = make_temporal_source(environment);

  TemporalSoften filter(source.clip, 1, 256, 0, 0, environment.get());
  const PVideoFrame output = filter.GetFrame(1, environment.get());

  constexpr std::uint8_t kExpectedAverage = 73;
  for (int y = 0; y < output->GetHeight(PLANAR_Y); ++y) {
    const auto* row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < output->GetRowSize(PLANAR_Y); ++x) {
      EXPECT_EQ(row[x], kExpectedAverage) << "B6 TemporalSoften x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source.clip_impl->frame_requests(), std::vector<int>({0, 1, 2}));
  ASSERT_EQ(source.snapshots.size(), source.frames.size());
  for (std::size_t index = 0; index < source.frames.size(); ++index) {
    EXPECT_EQ(FrameSnapshot::capture(source.frames[index], source.video_info),
              source.snapshots[index])
        << "B6 TemporalSoften modified source frame=" << index;
  }
}

}  // namespace
}  // namespace avsut::test
