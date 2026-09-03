#include <gtest/gtest.h>
#include <avisynth.h>
#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

namespace avsut::test {
namespace {

TEST(GreyscaleFilter, RgbMatrixDoesNotOverflowOrCrash) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{64, 64, VideoInfo::CS_BGR32, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(vi);
  uint8_t* ptr = frame->GetWritePtr();
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64 * 4; x += 4) {
      ptr[y * frame->GetPitch() + x + 0] = 50;  // B
      ptr[y * frame->GetPitch() + x + 1] = 200; // G
      ptr[y * frame->GetPitch() + x + 2] = 100; // R
      ptr[y * frame->GetPitch() + x + 3] = 255; // A
    }
  }
  const PClip source(new StaticFrameClip(vi, frame));

  const AVSValue matrix("RGB");
  const AVSValue args[] = {source, matrix};
  PClip grey;
  try {
    grey = environment.get()->Invoke("GreyScale", AVSValue(args, 2)).AsClip();
  } catch (const ::AvisynthError& e) {
    FAIL() << "AvisynthError: " << e.msg;
  } catch (const std::exception& e) {
    FAIL() << "std::exception: " << e.what();
  } catch (...) {
    FAIL() << "Unknown non-std exception";
  }
  ASSERT_NE(grey, nullptr);
  PVideoFrame output = grey->GetFrame(0, environment.get());
  ASSERT_NE(output, nullptr);

  const uint8_t* out_ptr = output->GetReadPtr();
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64 * 4; x += 4) {
      EXPECT_EQ(out_ptr[y * output->GetPitch() + x + 0], 200) << "B at (" << x << "," << y << ")";
      EXPECT_EQ(out_ptr[y * output->GetPitch() + x + 1], 200) << "G at (" << x << "," << y << ")";
      EXPECT_EQ(out_ptr[y * output->GetPitch() + x + 2], 200) << "R at (" << x << "," << y << ")";
    }
  }
}

}  // namespace
}  // namespace avsut::test
