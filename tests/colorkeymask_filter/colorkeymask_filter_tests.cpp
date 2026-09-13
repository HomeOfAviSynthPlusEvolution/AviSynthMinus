#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_COLORKEYMASK_FILTER_UNDEF_AVS_UNUSED
#endif
#include "filters/layer.h"
#ifdef AVSUT_COLORKEYMASK_FILTER_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_COLORKEYMASK_FILTER_UNDEF_AVS_UNUSED
#endif

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace avsut::test {
namespace {

TEST(ColorKeyMaskFilter, FloatRejectsEachChannelOutsideTolerance) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{7, 1, VideoInfo::CS_RGBAPS, 1, 25, 1});
  constexpr float key = 128 / 255.0F;
  constexpr float alpha = 0.75F;
  const std::array<int, 3> planes{PLANAR_R, PLANAR_G, PLANAR_B};

  for (int tolerance : {0, 32}) {
    SCOPED_TRACE(tolerance);
    PVideoFrame source = environment.get()->NewVideoFrame(vi);
    for (int channel = 0; channel < 3; ++channel) {
      write_frame_plane<float>(source, planes[channel], [channel](int x, int) {
        if (x == 1 + channel) return 0.0F;
        if (x == 4 + channel) return 1.0F;
        return key;
      });
    }
    write_frame_plane<float>(source, PLANAR_A, [](int, int) { return alpha; });
    const PClip clip(new StaticFrameClip(vi, source));
    ColorKeyMask filter(clip, 0x808080, tolerance, tolerance, tolerance, environment.get());
    const PVideoFrame output = filter.GetFrame(0, environment.get());

    EXPECT_EQ(read_frame_plane_active<float>(output, PLANAR_A),
              (std::vector<float>{0.0F, alpha, alpha, alpha, alpha, alpha, alpha}));
  }
}

}  // namespace
}  // namespace avsut::test
