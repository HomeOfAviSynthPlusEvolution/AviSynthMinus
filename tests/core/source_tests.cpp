#include <gtest/gtest.h>
#include <avisynth.h>
#include "support/avisynth_environment.h"
#include "support/compat_375.h"
#include <cmath>

namespace avsut::test {
namespace {

TEST(ToneFilter, WaveformsReleaseAfterAudioAndRejectedVideoUse) {
  AviSynthEnvironment environment;
  for (const char* waveform : {"Sine", "Noise", "Square", "Triangle", "Sawtooth", "Silence"}) {
    SCOPED_TRACE(waveform);
    const AVSValue args[] = {0.01, waveform};
    const char* names[] = {"length", "type"};
    PClip clip = environment.get()->Invoke("Tone", AVSValue(args, 2), names).AsClip();
    float samples[32];
    clip->GetAudio(samples, 0, 16, environment.get()); // Default is stereo float.
    for (float sample : samples)
      EXPECT_TRUE(std::isfinite(sample));
    const AVSValue fps_args[] = {clip, 25};
    EXPECT_THROW(environment.get()->Invoke("ChangeFPS", AVSValue(fps_args, 2)), AvisynthError);
    // The final clip reference is released each iteration. LeakSanitizer checks
    // generator ownership on both normal use and the rejected video path.
  }
}

TEST(BlankClipFilter, Yuy2FillPreservesHighChromaBytes) {
  AviSynthEnvironment environment;
  for (int chroma : {0, 127, 128, 255}) {
    // color_yuv encodes Y:U:V. V occupies the high byte of each YUYV word.
    const int color = (42 << 16) | (173 << 8) | chroma;
    const AVSValue args[] = {16, 8, "YUY2", color};
    const char* names[] = {"width", "height", "pixel_type", "color_yuv"};
    PClip clip = environment.get()->Invoke("BlankClip", AVSValue(args, 4), names).AsClip();
    PVideoFrame frame = clip->GetFrame(0, environment.get());
    const uint8_t expected[] = {42, 173, 42, static_cast<uint8_t>(chroma)};
    for (int y = 0; y < frame->GetHeight(); ++y) {
      const auto* row = frame->GetReadPtr() + y * frame->GetPitch();
      for (int x = 0; x < frame->GetRowSize(); ++x)
        ASSERT_EQ(row[x], expected[x % 4]) << "V=" << chroma;
    }
  }
}

TEST(ColorBarsFilter, FramePropertyMatrixIs601LikeForStandardYuv) {
  AviSynthEnvironment environment;
  const AVSValue pixel_type("YV12");
  const AVSValue args[] = {AVSValue(), AVSValue(), AVSValue(pixel_type)};
  const char* arg_names[] = {"width", "height", "pixel_type"};
  PClip clip = environment.get()->Invoke("ColorBars", AVSValue(args, 3), arg_names).AsClip();
  ASSERT_NE(clip, nullptr);
  PVideoFrame frame = clip->GetFrame(0, environment.get());
  ASSERT_NE(frame, nullptr);
  const AVSMap* props = environment.get()->getFramePropsRO(frame);
  ASSERT_NE(props, nullptr);
  int error = 0;
  int64_t matrix = environment.get()->propGetInt(props, "_Matrix", 0, &error);
  EXPECT_EQ(error, 0);
  EXPECT_EQ(matrix, Matrix_e::AVS_MATRIX_ST170_M);
}

TEST(ColorBarsFilter, NonStaticFrameCopiesAlphaCorrectly) {
  AviSynthEnvironment environment;
  const AVSValue pixel_type("YUVA444P8");
  const AVSValue static_frames(false);
  const AVSValue args[] = {AVSValue(), AVSValue(), AVSValue(pixel_type), AVSValue(static_frames)};
  const char* arg_names[] = {"width", "height", "pixel_type", "staticframes"};
  PClip clip = environment.get()->Invoke("ColorBars", AVSValue(args, 4), arg_names).AsClip();
  ASSERT_NE(clip, nullptr);
  PVideoFrame frame = clip->GetFrame(0, environment.get());
  ASSERT_NE(frame, nullptr);
  ASSERT_TRUE(clip->GetVideoInfo().IsYUVA());

  const uint8_t* alpha_ptr = frame->GetReadPtr(PLANAR_A);
  const int rowsize = frame->GetRowSize(PLANAR_A);
  const int height = frame->GetHeight(PLANAR_A);
  const int pitch = frame->GetPitch(PLANAR_A);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < rowsize; ++x) {
      ASSERT_EQ(alpha_ptr[y * pitch + x], 0) << "alpha modified at x=" << x << " y=" << y;
    }
  }
}

}  // namespace
}  // namespace avsut::test
