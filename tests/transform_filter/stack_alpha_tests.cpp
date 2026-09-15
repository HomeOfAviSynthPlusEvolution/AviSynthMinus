#include "stack/show_five_versions.h"
using aif::filters::stack::ShowFiveVersions;
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

VideoSource make_yuva_source(AviSynthEnvironment& environment, std::uint8_t alpha) {
  const auto video_info = make_video_info(VideoInfoSpec{4, 4, VideoInfo::CS_YUVA444, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0x10, PLANAR_Y);
  fill_plane_full_pitch(frame, 0x80, PLANAR_U);
  fill_plane_full_pitch(frame, 0x80, PLANAR_V);
  fill_plane_full_pitch(frame, alpha, PLANAR_A);
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return VideoSource{video_info, frame, clip_impl, PClip(clip_impl), FrameSnapshot::capture(frame, video_info)};
}

TEST(ShowFiveVersions, CopiesAlphaForEachSourcePanel) {
  AviSynthEnvironment environment;
  constexpr std::array<std::uint8_t, 5> kAlphaValues{13, 47, 89, 131, 211};
  std::vector<VideoSource> sources;
  sources.reserve(kAlphaValues.size());
  for (const auto alpha : kAlphaValues) {
    sources.push_back(make_yuva_source(environment, alpha));
  }

  std::array<PClip, 5> children{};
  for (std::size_t index = 0; index < children.size(); ++index) {
    children[index] = sources[index].clip;
  }
  ShowFiveVersions filter(children.data(), environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_TRUE(filter.GetVideoInfo().IsYUVA());
  ASSERT_NE(output->GetReadPtr(PLANAR_A), nullptr);
  constexpr std::array<int, 5> kPanelX{0, 2, 4, 6, 8};
  constexpr std::array<int, 5> kPanelY{0, 4, 0, 4, 0};
  const auto* alpha = output->GetReadPtr(PLANAR_A);
  const int alpha_pitch = output->GetPitch(PLANAR_A);
  for (std::size_t index = 0; index < kAlphaValues.size(); ++index) {
    EXPECT_EQ(static_cast<int>(alpha[kPanelY[index] * alpha_pitch + kPanelX[index]]),
              static_cast<int>(kAlphaValues[index]))
        << "ShowFiveVersions panel=" << index;
    EXPECT_EQ(FrameSnapshot::capture(sources[index].frame, sources[index].video_info), sources[index].snapshot)
        << "ShowFiveVersions modified source=" << index;
    EXPECT_EQ(sources[index].clip_impl->frame_requests(), std::vector<int>{0})
        << "ShowFiveVersions source request=" << index;
  }
  EXPECT_NE(output->CheckMemory(), 1);
}

} // namespace
} // namespace avsut::test
