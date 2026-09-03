#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B11_HISTOGRAM_UNDEF_AVS_UNUSED
#endif
#include "filters/histogram.h"
#include "core/parser/script.h"
#ifdef AVSUT_FINDING_B11_HISTOGRAM_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B11_HISTOGRAM_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

namespace avsut::test {
namespace {

VideoInfo make_audio_video_info(const VideoInfoSpec& video_spec) {
  VideoInfo video_info = make_video_info(video_spec);
  video_info.audio_samples_per_second = 48000;
  video_info.sample_type = SAMPLE_INT16;
  video_info.num_audio_samples = 1920;
  video_info.nchannels = 2;
  video_info.SetChannelMask(false, 0);
  return video_info;
}

AVSValue script_generated_nan(IScriptEnvironment* environment) {
  const AVSValue negative_one(-1.0F);
  return Sqrt(AVSValue(&negative_one, 1), nullptr, environment);
}

TEST(HistogramAudioLevelsConstruction, RejectsWidthThatCannotHoldAudioBars) {
  AviSynthEnvironment environment;
  const VideoInfo video_info =
      make_audio_video_info(VideoInfoSpec{8, 32, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0x40, PLANAR_Y);
  fill_plane_full_pitch(frame, 0x80, PLANAR_U);
  fill_plane_full_pitch(frame, 0x80, PLANAR_V);
  const FrameSnapshot before = FrameSnapshot::capture(frame, video_info);
  auto* clip_impl = new FrameSequenceClip(video_info, std::vector<PVideoFrame>{frame});
  const PClip source(clip_impl);

  EXPECT_THROW(
      {
        Histogram filter(source, Histogram::ModeAudioLevels, AVSValue(), 8, true, true,
                         environment.get());
      },
      AvisynthError)
      << "B11 Histogram AudioLevels width=" << video_info.width
      << " channels=" << video_info.AudioChannels();
  EXPECT_EQ(FrameSnapshot::capture(frame, video_info), before)
      << "B11 Histogram AudioLevels modified its source during construction";
}

TEST(HistogramLevelsFactory, RejectsScriptGeneratedNanFactor) {
  AviSynthEnvironment environment;
  const VideoInfo video_info = make_video_info(VideoInfoSpec{8, 8, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0x40, PLANAR_Y);
  const FrameSnapshot before = FrameSnapshot::capture(frame, video_info);
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  const PClip source(clip_impl);
  const AVSValue factor = script_generated_nan(environment.get());
  ASSERT_TRUE(std::isnan(factor.AsFloat()));
  const std::array<AVSValue, 3> arguments{source, "levels", factor};

  EXPECT_THROW(
      environment.get()
          ->Invoke("Histogram", AVSValue(arguments.data(), static_cast<int>(arguments.size())))
          .AsClip(),
      AvisynthError)
      << "B11 Histogram Levels accepted a script-generated NaN factor";
  EXPECT_EQ(FrameSnapshot::capture(frame, video_info), before)
      << "B11 Histogram Levels modified its source during construction";
}

bool float_nan_levels_frame_is_safe() {
  AviSynthEnvironment environment;
  const VideoInfo video_info =
      make_video_info(VideoInfoSpec{8, 8, VideoInfo::CS_YUV444PS, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int pitch = frame->GetPitch(plane) / static_cast<int>(sizeof(float));
    const int height = frame->GetHeight(plane);
    auto* pixels = reinterpret_cast<float*>(frame->GetWritePtr(plane));
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < pitch; ++x) {
        pixels[y * pitch + x] = plane == PLANAR_Y ? 0.5F : 0.0F;
      }
    }
  }
  reinterpret_cast<float*>(frame->GetWritePtr(PLANAR_Y))[0] =
      std::numeric_limits<float>::quiet_NaN();
  const FrameSnapshot before = FrameSnapshot::capture(frame, video_info);
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  const PClip source(clip_impl);
  Histogram filter(source, Histogram::ModeLevels, AVSValue(100.0F), 8, false, true,
                   environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  return output != nullptr && output->CheckMemory() != 1 &&
         FrameSnapshot::capture(frame, video_info) == before;
}

TEST(HistogramLevels, HandlesOneFloatNanWithoutFrameMemoryCorruption) {
  EXPECT_EXIT(
      { std::_Exit(float_nan_levels_frame_is_safe() ? EXIT_SUCCESS : EXIT_FAILURE); },
      ::testing::ExitedWithCode(EXIT_SUCCESS), "")
      << "B11 Histogram Levels float NaN source";
}

}  // namespace
}  // namespace avsut::test
