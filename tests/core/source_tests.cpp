#include <gtest/gtest.h>
#include <avisynth.h>
#include "support/avisynth_environment.h"
#include "support/compat_375.h"

namespace avsut::test {
namespace {

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
