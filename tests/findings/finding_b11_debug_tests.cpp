#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B11_DEBUG_UNDEF_AVS_UNUSED
#endif
#include "filters/debug.h"
#ifdef AVSUT_FINDING_B11_DEBUG_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B11_DEBUG_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <array>
#include <vector>

namespace avsut::test {
namespace {

class ThrowingVideoClip final : public IClip {
 public:
  explicit ThrowingVideoClip(VideoInfo video_info) : video_info_(video_info) {}

  PVideoFrame __stdcall GetFrame(int, IScriptEnvironment*) override {
    throw AvisynthError("B11 test source frame failure");
  }

  bool __stdcall GetParity(int) override { return false; }

  void __stdcall GetAudio(void*, int64_t, int64_t, IScriptEnvironment*) override {
    throw AvisynthError("B11 test source audio failure");
  }

  int __stdcall SetCacheHints(int, int) override { return 0; }

  const VideoInfo& __stdcall GetVideoInfo() override { return video_info_; }

 private:
  VideoInfo video_info_{};
};

PClip set_planar_legacy_alignment(PClip source, bool legacy, IScriptEnvironment* environment) {
  const std::array<AVSValue, 2> arguments{source, legacy};
  return environment
      ->Invoke("SetPlanarLegacyAlignment",
               AVSValue(arguments.data(), static_cast<int>(arguments.size())))
      .AsClip();
}

TEST(PlanarLegacyAlignment, RestoresEnvironmentStateWhenChildThrows) {
  AviSynthEnvironment environment;
  const VideoInfo video_info = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_YV12, 1, 25, 1});
  const PClip source(new ThrowingVideoClip(video_info));
  environment.get()->PlanarChromaAlignment(IScriptEnvironment::PlanarChromaAlignmentOn);
  ASSERT_TRUE(
      environment.get()->PlanarChromaAlignment(IScriptEnvironment::PlanarChromaAlignmentTest));
  const PClip filter = set_planar_legacy_alignment(source, true, environment.get());

  EXPECT_THROW(filter->GetFrame(0, environment.get()), AvisynthError);
  EXPECT_TRUE(
      environment.get()->PlanarChromaAlignment(IScriptEnvironment::PlanarChromaAlignmentTest))
      << "B11 SetPlanarLegacyAlignment left the environment in legacy mode after a child error";
}

TEST(NullFilter, AcceptsSmallFrameWithoutInternalPitchMismatch) {
  AviSynthEnvironment environment;
  const VideoInfo video_info = make_video_info(VideoInfoSpec{8, 8, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0x5a, PLANAR_Y);
  const auto source_before = FrameSnapshot::capture(frame, video_info);
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  const PClip source(clip_impl);
  Null filter(source, "", environment.get());

  PVideoFrame output;
  ASSERT_NO_THROW(output = filter.GetFrame(0, environment.get()));
  ASSERT_NE(output, nullptr);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(frame, video_info), source_before)
      << "B11 Null modified its source";
  EXPECT_EQ(clip_impl->frame_requests(), std::vector<int>{0});
}

}  // namespace
}  // namespace avsut::test
