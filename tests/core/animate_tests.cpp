#include <gtest/gtest.h>
#include <avisynth.h>
#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

namespace avsut::test {
namespace {

TEST(AnimateFilter, ClampsInterpolatedValuesBetweenStartAndEnd) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{64, 64, VideoInfo::CS_BGR32, 11, 25, 1});
  std::vector<PVideoFrame> frames;
  for (int i = 0; i < 11; ++i) {
    frames.push_back(environment.get()->NewVideoFrame(vi));
  }
  auto* clip_impl = new FrameSequenceClip(vi, frames);
  const PClip source(clip_impl);

  const AVSValue clip_val(source);
  const AVSValue start_frame(0);
  const AVSValue end_frame(10);
  const AVSValue filter_name("BilinearResize");
  const AVSValue target_w(64), target_h(64);
  const AVSValue start_src_left(0.0), start_src_top(0.0);
  const AVSValue end_src_left(10.0), end_src_top(10.0);
  const AVSValue args[] = {clip_val, start_frame, end_frame, filter_name, target_w, target_h, start_src_left, start_src_top, target_w, target_h, end_src_left, end_src_top};
  PClip animated;
  try {
    animated = environment.get()->Invoke("Animate", AVSValue(args, 12)).AsClip();
  } catch (const AvisynthError& e) {
    FAIL() << "AvisynthError: " << e.msg;
  }
  ASSERT_NE(animated, nullptr);

  for (int n = 0; n <= 10; ++n) {
    PVideoFrame f = animated->GetFrame(n, environment.get());
    EXPECT_NE(f, nullptr);
    EXPECT_EQ(f->GetRowSize(), 64 * 4);
    EXPECT_EQ(f->GetHeight(), 64);
  }
}

}  // namespace
}  // namespace avsut::test
