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

}  // namespace
}  // namespace avsut::test
