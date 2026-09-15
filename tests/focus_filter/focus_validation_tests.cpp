#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_LOCAL_UNDEF_AVS_UNUSED
#endif
#include "focus/temporal_soften.h"
using aif::filters::focus::TemporalSoften;
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
  return TemporalSource{video_info, std::move(frames), clip_impl, PClip(clip_impl), std::move(snapshots)};
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
      EXPECT_EQ(row[x], kExpectedAverage) << "TemporalSoften x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source.clip_impl->frame_requests(), std::vector<int>({0, 1, 2}));
  ASSERT_EQ(source.snapshots.size(), source.frames.size());
  for (std::size_t index = 0; index < source.frames.size(); ++index) {
    EXPECT_EQ(FrameSnapshot::capture(source.frames[index], source.video_info), source.snapshots[index])
        << "TemporalSoften modified source frame=" << index;
  }
}

} // namespace
} // namespace avsut::test
