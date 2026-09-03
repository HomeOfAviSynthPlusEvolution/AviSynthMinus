#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B11_MISC_UNDEF_AVS_UNUSED
#endif
#include "filters/misc.h"
#ifdef AVSUT_FINDING_B11_MISC_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B11_MISC_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <array>
#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

struct Yuy2Source {
  VideoInfo video_info;
  std::vector<PVideoFrame> frames;
  FrameSequenceClip* clip_impl;
  PClip clip;
  std::vector<FrameSnapshot> snapshots;
};

Yuy2Source make_yuy2_source(AviSynthEnvironment& environment, int frame_count) {
  const VideoInfo video_info =
      make_video_info(VideoInfoSpec{8, 4, VideoInfo::CS_YUY2, frame_count, 25, 1});
  std::vector<PVideoFrame> frames;
  std::vector<FrameSnapshot> snapshots;
  frames.reserve(static_cast<std::size_t>(frame_count));
  snapshots.reserve(static_cast<std::size_t>(frame_count));
  for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
    fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0x30 + frame_index), DEFAULT_PLANE);
    snapshots.push_back(FrameSnapshot::capture(frame, video_info));
    frames.push_back(frame);
  }
  auto* clip_impl = new FrameSequenceClip(video_info, frames);
  return Yuy2Source{video_info, std::move(frames), clip_impl, PClip(clip_impl),
                    std::move(snapshots)};
}

void expect_source_unchanged(const Yuy2Source& source, const char* operation) {
  ASSERT_EQ(source.frames.size(), source.snapshots.size());
  for (std::size_t index = 0; index < source.frames.size(); ++index) {
    EXPECT_EQ(FrameSnapshot::capture(source.frames[index], source.video_info),
              source.snapshots[index])
        << operation << " modified source frame=" << index;
  }
}

TEST(FixLuminanceConstruction, RejectsZeroSlopeBeforeFrameEvaluation) {
  AviSynthEnvironment environment;
  const Yuy2Source source = make_yuy2_source(environment, 1);

  EXPECT_THROW(
      { FixLuminance filter(source.clip, 1, 0, environment.get()); }, AvisynthError)
      << "B11 FixLuminance slope=0";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty())
      << "B11 FixLuminance requested a frame during construction";
  expect_source_unchanged(source, "B11 FixLuminance");
}

TEST(PeculiarBlend, ServesLastAdvertisedFrameWithoutOutOfRangeChildRequest) {
  AviSynthEnvironment environment;
  const Yuy2Source source = make_yuy2_source(environment, 2);
  PeculiarBlend filter(source.clip, 2, environment.get());

  PVideoFrame output;
  ASSERT_NO_THROW(output = filter.GetFrame(1, environment.get()));
  ASSERT_NE(output, nullptr);
  for (const int request : source.clip_impl->frame_requests()) {
    EXPECT_GE(request, 0);
    EXPECT_LT(request, source.video_info.num_frames)
        << "B11 PeculiarBlend requested a frame past its advertised range";
  }
  EXPECT_NE(output->CheckMemory(), 1);
  expect_source_unchanged(source, "B11 PeculiarBlend");
}

TEST(SkewRowsConstruction, RejectsNegativeOutputWidth) {
  AviSynthEnvironment environment;
  const auto video_info = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0x5a, PLANAR_Y);
  const auto snapshot = FrameSnapshot::capture(frame, video_info);
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  const PClip source(clip_impl);

  EXPECT_THROW(
      { SkewRows filter(source, -video_info.width - 1, environment.get()); }, AvisynthError)
      << "B11 SkewRows width=" << video_info.width << " skew=" << -video_info.width - 1;
  EXPECT_TRUE(clip_impl->frame_requests().empty())
      << "B11 SkewRows requested a frame during construction";
  EXPECT_EQ(FrameSnapshot::capture(frame, video_info), snapshot)
      << "B11 SkewRows modified its source during construction";
}

}  // namespace
}  // namespace avsut::test
