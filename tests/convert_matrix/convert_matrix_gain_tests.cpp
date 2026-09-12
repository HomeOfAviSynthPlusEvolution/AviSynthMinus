#include "convert/convert_helper.h"
#include "convert/convert_matrix.h"
#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
namespace avsut::test {
namespace {
TEST(ConvertMatrixGain, NominalLimitedRgbSpanMapsToFullLuma) {
  for (int depth : {8, 10, 12, 14, 16})
    for (int precision : {13, 14, 15})
      for (int id : {AVS_MATRIX_BT709, AVS_MATRIX_BT470_BG, AVS_MATRIX_BT2020_NCL}) {
        SCOPED_TRACE(::testing::Message() << "depth=" << depth << " precision=" << precision << " matrix=" << id);
        ConversionMatrix m{};
        ASSERT_TRUE(do_BuildMatrix_Rgb2Yuv(id, AVS_RANGE_LIMITED, AVS_RANGE_FULL, precision, depth, m));
        const double span = 219 << (depth - 8), maximum = (1 << depth) - 1, scale = 1 << precision;
        EXPECT_NEAR(span * (m.y_b + m.y_g + m.y_r) / scale, maximum, span / (2 * scale) + 1e-8);
        EXPECT_NEAR(span * (double(m.y_b_f) + m.y_g_f + m.y_r_f), maximum, maximum * 2e-7);
        EXPECT_EQ(m.offset_rgb, -(16 << (depth - 8)));
      }
}
TEST(ConvertMatrixGain, LimitedRgbDestinationUsesNominalIntegerSpan) {
  for (int depth : {8, 10, 12, 14, 16}) {
    ConversionMatrix m{};
    ASSERT_TRUE(do_BuildMatrix_Yuv2Rgb(AVS_MATRIX_BT709, AVS_RANGE_FULL, AVS_RANGE_LIMITED, 13, depth, m));
    const double maximum = (1 << depth) - 1, span = 219 << (depth - 8);
    EXPECT_NEAR(maximum * m.y_b / 8192., span, maximum / 16384.);
    EXPECT_NEAR(maximum * m.y_b_f, span, span * 2e-7);
  }
}
TEST(ConvertMatrixGain, PublicLimitedRgbEndpoints) {
  for (bool scalar : {true, false})
    for (const int format :
         {VideoInfo::CS_RGBP8, VideoInfo::CS_RGBP10, VideoInfo::CS_RGBP12, VideoInfo::CS_RGBP14, VideoInfo::CS_RGBP16,
          VideoInfo::CS_RGBPS, VideoInfo::CS_BGR24, VideoInfo::CS_BGR32, VideoInfo::CS_BGR48, VideoInfo::CS_BGR64}) {
      AviSynthEnvironment environment;
      auto* env = environment.get();
      if (scalar)
        env->Invoke("SetMaxCPU", "none");
      const auto vi = make_video_info({35, 2, format, 1, 25, 1});
      const int depth = vi.BitsPerComponent();
      const double black = depth == 32 ? 16.0 / 255 : 16 << (depth - 8);
      const double white = depth == 32 ? 235.0 / 255 : 235 << (depth - 8);
      const double maximum = depth == 32 ? 1 : (1 << depth) - 1;
      SCOPED_TRACE(::testing::Message() << "format=" << format << " scalar=" << scalar);
      auto frame = env->NewVideoFrame(vi);
      auto value = [&](int x, int) {
        if (depth == 32)
          return x % 2 ? white : black;
        switch (x % 4) {
          case 0:
            return 0.0;
          case 1:
            return black;
          case 2:
            return white;
          default:
            return maximum;
        }
      };
      if (vi.IsPlanarRGB()) {
        for (int plane : {PLANAR_R, PLANAR_G, PLANAR_B}) {
          if (depth == 8)
            write_frame_plane<uint8_t>(frame, plane, value);
          else if (depth == 32)
            write_frame_plane<float>(frame, plane, value);
          else
            write_frame_plane<uint16_t>(frame, plane, value);
        }
      } else {
        const int components = vi.BytesFromPixels(1) / vi.ComponentSize();
        for (int y = 0; y < 2; ++y)
          for (int x = 0; x < 35; ++x)
            for (int c = 0; c < components; ++c) {
              auto* row = frame->GetWritePtr() + y * frame->GetPitch();
              if (depth == 8)
                row[x * components + c] = static_cast<uint8_t>(value(x, y));
              else
                reinterpret_cast<uint16_t*>(row)[x * components + c] = static_cast<uint16_t>(value(x, y));
            }
      }
      set_frame_property_int(env, frame, "_ColorRange", AVS_RANGE_LIMITED);
      const PClip source = new StaticFrameClip(vi, frame);
      for (const char* filter : {"ConvertToY", "ConvertToYUV444", "Greyscale"}) {
        const AVSValue args[] = {source, "709:full"};
        const char* names[] = {nullptr, "matrix"};
        const PClip result = env->Invoke(filter, AVSValue(args, 2), names).AsClip();
        const auto output = result->GetFrame(0, env);
        EXPECT_EQ(env->propGetInt(env->getFramePropsRO(output), "_ColorRange", 0, nullptr), AVS_RANGE_FULL);
        EXPECT_EQ(env->propGetInt(env->getFramePropsRO(frame), "_ColorRange", 0, nullptr), AVS_RANGE_LIMITED);
        for (int y = 0; y < 2; ++y)
          for (int x = 0; x < 35; ++x) {
            const auto* row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
            const int offset = std::strcmp(filter, "Greyscale") == 0 && !vi.IsPlanar()
                                   ? x * vi.BytesFromPixels(1) / vi.ComponentSize()
                                   : x;
            const double actual = depth == 8    ? row[offset]
                                  : depth == 32 ? reinterpret_cast<const float*>(row)[offset]
                                                : reinterpret_cast<const uint16_t*>(row)[offset];
            const bool bright = depth == 32 ? x % 2 != 0 : x % 4 >= 2;
            EXPECT_NEAR(actual, bright ? maximum : 0, depth == 32 ? 2e-7 : 1) << filter << " x=" << x;
          }
      }
    }
}
} // namespace
} // namespace avsut::test
