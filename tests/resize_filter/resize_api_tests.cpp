// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#include "support/video_filter_test_support.h"
#include <gtest/gtest.h>
#include <algorithm>

namespace avsut::test {
namespace {
int ResizePlane(const VideoInfo& vi, int c) {
  const int rgb[] = {PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A};
  const int yuv[] = {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A};
  return vi.IsPlanar() ? (vi.IsRGB() ? rgb[c] : yuv[c]) : DEFAULT_PLANE;
}
double Pattern(const VideoInfo& vi, int c, int x, int y) {
  // Multiples of 16 make the quarter-step bilinear reference exact in integers.
  const int value = ((x * 3 + y * 5 + c * 7) % 14) * 16;
  return vi.ComponentSize() == 4 ? value / 256.0 : value;
}
template<bool write>
double Sample(PVideoFrame& frame, const VideoInfo& vi, int c, int x, int y, double value = 0) {
  const int plane = ResizePlane(vi, c);
  const int row = vi.IsPlanar() ? y : vi.height - 1 - y;
  const int offset = vi.IsPlanar() ? x : x * vi.NumComponents() + c;
  auto* bytes = (write ? frame->GetWritePtr(plane) : const_cast<BYTE*>(frame->GetReadPtr(plane)))
    + ptrdiff_t(row) * frame->GetPitch(plane);
  if (vi.ComponentSize() == 1) {
    if constexpr (write) bytes[offset] = uint8_t(value);
    return bytes[offset];
  }
  if (vi.ComponentSize() == 2) {
    auto* words = reinterpret_cast<uint16_t*>(bytes);
    if constexpr (write) words[offset] = uint16_t(value);
    return words[offset];
  }
  auto* floats = reinterpret_cast<float*>(bytes);
  if constexpr (write) floats[offset] = float(value);
  return floats[offset];
}
class ResizePublic : public testing::TestWithParam<int> {};
TEST_P(ResizePublic, IndependentCoordinatesPreserveAlphaPropertiesAndSource) {
  for (bool scalar : {true, false}) {
    AviSynthEnvironment environment; auto* env = environment.get();
    if (scalar) env->Invoke("SetMaxCPU", "none");
    const auto vi = make_video_info({34, 18, GetParam(), 3, 25, 1});
    auto frame = env->NewVideoFrame(vi);
    for (int c = 0; c < vi.NumComponents(); ++c)
      for (int y = 0; y < vi.height; ++y)
        for (int x = 0; x < vi.width; ++x)
          Sample<true>(frame, vi, c, x, y, Pattern(vi, c, x, y));
    set_frame_property_int(env, frame, "ResizeMarker", 91);
    const auto snapshot = FrameSnapshot::capture(frame, vi);
    const PClip source = new FrameSequenceClip(vi, {frame, frame, frame});
    // Point: crop and shrink/enlarge. Bilinear: enlarge on both axes.
    for (int scenario : {0, 1, 2}) {
      const bool linear = scenario == 2;
      const int width = linear ? 68 : scenario == 0 ? 14 : 56;
      const int height = linear ? 36 : scenario == 0 ? 6 : 24;
      const AVSValue args[] = {source, width, height, linear ? 0 : 2,
                              linear ? 0 : 2, linear ? 34 : 28, linear ? 18 : 12};
      const auto clip = env->Invoke(linear ? "BilinearResize" : "PointResize", AVSValue(args, 7)).AsClip();
      const auto& out_vi = clip->GetVideoInfo();
      ASSERT_EQ(out_vi.width, width); ASSERT_EQ(out_vi.height, height);
      EXPECT_EQ(out_vi.pixel_type, vi.pixel_type);
      EXPECT_EQ(out_vi.num_frames, vi.num_frames);
      EXPECT_EQ(out_vi.fps_numerator, vi.fps_numerator);
      EXPECT_EQ(out_vi.fps_denominator, vi.fps_denominator);
      for (int n : {2, 0, 1}) {
        SCOPED_TRACE(::testing::Message() << GetParam() << '/' << scalar << '/' << scenario << '/' << n);
        auto output = clip->GetFrame(n, env);
        for (int c = 0; c < vi.NumComponents(); ++c)
          for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
              double expected;
              if (!linear) {
                // Point uses the legacy boundary grid. Packed RGB evaluates
                // vertical coordinates bottom-up, including its sampling phase.
                const int sy = vi.IsPlanar() ? 2 + y * 12 / height
                  : 13 - (height - 1 - y) * 12 / height;
                expected = Pattern(vi, c, 2 + x * 28 / width, sy);
              } else {
                const double sx = std::clamp((x + .5) / 2 - .5, 0.0, 33.0);
                const double sy = std::clamp((y + .5) / 2 - .5, 0.0, 17.0);
                const int x0 = int(sx), y0 = int(sy);
                const int x1 = std::min(x0 + 1, 33), y1 = std::min(y0 + 1, 17);
                const double fx = sx - x0, fy = sy - y0;
                expected = (1 - fy) * ((1 - fx) * Pattern(vi,c,x0,y0) + fx * Pattern(vi,c,x1,y0))
                         + fy * ((1 - fx) * Pattern(vi,c,x0,y1) + fx * Pattern(vi,c,x1,y1));
              }
              ASSERT_EQ(Sample<false>(output, out_vi, c, x, y), expected) << c << '/' << x << '/' << y;
            }
        EXPECT_EQ(get_frame_property_int(env, output, "ResizeMarker"), 91);
        EXPECT_NE(output->CheckMemory(), 1);
      }
      EXPECT_EQ(FrameSnapshot::capture(frame, vi), snapshot);
    }
  }
}
INSTANTIATE_TEST_SUITE_P(Formats, ResizePublic, testing::Values(
  VideoInfo::CS_Y8, VideoInfo::CS_Y16, VideoInfo::CS_Y32,
  VideoInfo::CS_YUVA444, VideoInfo::CS_YUVA444P10, VideoInfo::CS_YUVA444P16,
  VideoInfo::CS_YUVA444PS, VideoInfo::CS_RGBAPS,
  VideoInfo::CS_BGR24, VideoInfo::CS_BGR32, VideoInfo::CS_BGR48, VideoInfo::CS_BGR64));
} // namespace
} // namespace avsut::test
