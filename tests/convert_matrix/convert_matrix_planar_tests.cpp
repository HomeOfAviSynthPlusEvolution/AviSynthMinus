#include "convert/convert_helper.h"
#include "convert/convert_matrix.h"
#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace avsut::test {
namespace {
class PlanarMatrix : public ::testing::TestWithParam<int> {};
TEST_P(PlanarMatrix, PublicPixelsAlphaAndProperties) {
  const int p = GetParam(), depth_index = p % 6;
  const bool forward = (p / 6) % 2 == 0, scalar = p / 12 == 0;
  const int rgb[] = {VideoInfo::CS_RGBAP,   VideoInfo::CS_RGBAP10, VideoInfo::CS_RGBAP12,
                     VideoInfo::CS_RGBAP14, VideoInfo::CS_RGBAP16, VideoInfo::CS_RGBAPS};
  const int yuv[] = {VideoInfo::CS_YUVA444,    VideoInfo::CS_YUVA444P10, VideoInfo::CS_YUVA444P12,
                     VideoInfo::CS_YUVA444P14, VideoInfo::CS_YUVA444P16, VideoInfo::CS_YUVA444PS};
  AviSynthEnvironment environment;
  auto* env = environment.get();
  if (scalar)
    env->Invoke("SetMaxCPU", "none");
  const auto vi = make_video_info({37, 3, forward ? rgb[depth_index] : yuv[depth_index], 1, 25, 1});
  const int depth = vi.BitsPerComponent();
  const int src_planes[] = {forward ? PLANAR_B : PLANAR_Y, forward ? PLANAR_G : PLANAR_U, forward ? PLANAR_R : PLANAR_V,
                            PLANAR_A};
  const int dst_planes[] = {forward ? PLANAR_Y : PLANAR_B, forward ? PLANAR_U : PLANAR_G, forward ? PLANAR_V : PLANAR_R,
                            PLANAR_A};
  const int ids[] = {AVS_MATRIX_BT709, AVS_MATRIX_BT470_BG, AVS_MATRIX_BT2020_NCL};
  const char* names_matrix[] = {"709", "601", "2020"};
  for (int matrix_index = 0; matrix_index < 3; ++matrix_index)
    for (int source_range : {AVS_RANGE_FULL, AVS_RANGE_LIMITED})
      for (int destination_range : {AVS_RANGE_FULL, AVS_RANGE_LIMITED}) {
        // Reverse conversion names the input range. Limited RGB output is
        // available through :same only, so it requires limited YUV input.
        if (!forward && destination_range == AVS_RANGE_LIMITED && source_range == AVS_RANGE_FULL)
          continue;
        SCOPED_TRACE(::testing::Message() << matrix_index << '/' << source_range << '/' << destination_range);
        auto source_frame = env->NewVideoFrame(vi);
        auto sample = [&](int c, int x, int y) -> double {
          const int code = (x * (43 + 7 * c) + y * 61 + c * 37) % 257;
          if (depth == 32)
            return code / 256.0 - (!forward && c > 0 && c < 3 ? .5 : 0);
          return (int64_t(code) * ((1 << depth) - 1)) / 256;
        };
        for (int c = 0; c < 4; ++c) {
          auto value = [&](int x, int y) {
            return sample(c, x, y);
          };
          if (depth == 8)
            write_frame_plane<uint8_t>(source_frame, src_planes[c], value);
          else if (depth == 32)
            write_frame_plane<float>(source_frame, src_planes[c], value);
          else
            write_frame_plane<uint16_t>(source_frame, src_planes[c], value);
        }
        set_frame_property_int(env, source_frame, "_ColorRange", source_range);
        set_frame_property_int(env, source_frame, "_Matrix", forward ? AVS_MATRIX_RGB : ids[matrix_index]);
        set_frame_property_int(env, source_frame, "_ChromaLocation", 0);
        set_frame_property_int(env, source_frame, "MatrixTestMarker", 173);
        const auto snapshot = FrameSnapshot::capture(source_frame, vi);
        const PClip source = new StaticFrameClip(vi, source_frame);
        const std::string suffix =
            !forward && destination_range == AVS_RANGE_LIMITED
                ? ":same"
                : ((forward ? destination_range : source_range) == AVS_RANGE_FULL ? ":full" : ":limited");
        const std::string matrix_name = std::string(names_matrix[matrix_index]) + suffix;
        const AVSValue args[] = {source, matrix_name.c_str()};
        const char* arg_names[] = {nullptr, "matrix"};
        const PClip converted =
            env->Invoke(forward ? "ConvertToYUVA444" : "ConvertToPlanarRGBA", AVSValue(args, 2), arg_names).AsClip();
        const PClip gray_clip = forward ? env->Invoke("ConvertToY", AVSValue(args, 2), arg_names).AsClip() : PClip{};
        const PVideoFrame gray = forward ? gray_clip->GetFrame(0, env) : PVideoFrame{};
        const PVideoFrame gray_rgb =
            forward ? env->Invoke("Greyscale", AVSValue(args, 2), arg_names).AsClip()->GetFrame(0, env) : PVideoFrame{};
        if (forward) {
          const auto* gray_props = env->getFramePropsRO(gray_rgb);
          EXPECT_EQ(env->propGetInt(gray_props, "_ColorRange", 0, nullptr), destination_range);
          EXPECT_EQ(env->propGetInt(gray_props, "_Matrix", 0, nullptr), AVS_MATRIX_RGB);
          EXPECT_EQ(env->propGetInt(gray_props, "MatrixTestMarker", 0, nullptr), 173);
          EXPECT_EQ(env->propGetInt(env->getFramePropsRO(source_frame), "_ColorRange", 0, nullptr), source_range);
        }
        const auto output = converted->GetFrame(0, env);
        const auto props = env->getFramePropsRO(output);
        EXPECT_EQ(env->propGetInt(props, "_ColorRange", 0, nullptr), destination_range);
        EXPECT_EQ(env->propGetInt(props, "_Matrix", 0, nullptr), forward ? ids[matrix_index] : AVS_MATRIX_RGB);
        EXPECT_EQ(env->propGetInt(props, "MatrixTestMarker", 0, nullptr), 173);
        if (!forward)
          EXPECT_EQ(env->propNumElements(props, "_ChromaLocation"), -1);
        ConversionMatrix m{};
        const int precision = forward ? 15 : 13;
        ASSERT_TRUE(
            forward ? do_BuildMatrix_Rgb2Yuv(ids[matrix_index], source_range, destination_range, precision, depth, m)
                    : do_BuildMatrix_Yuv2Rgb(ids[matrix_index], source_range, destination_range, precision, depth, m));
        const int weights[3][3] = {{m.y_b, forward ? m.y_g : m.u_b, forward ? m.y_r : m.v_b},
                                   {forward ? m.u_b : m.y_g, m.u_g, forward ? m.u_r : m.v_g},
                                   {forward ? m.v_b : m.y_r, forward ? m.v_g : m.u_r, m.v_r}};
        const float fw[3][3] = {{m.y_b_f, forward ? m.y_g_f : m.u_b_f, forward ? m.y_r_f : m.v_b_f},
                                {forward ? m.u_b_f : m.y_g_f, m.u_g_f, forward ? m.u_r_f : m.v_g_f},
                                {forward ? m.v_b_f : m.y_r_f, forward ? m.v_g_f : m.u_r_f, m.v_r_f}};
        for (int y = 0; y < vi.height; ++y)
          for (int x = 0; x < vi.width; ++x)
            for (int c = 0; c < 4; ++c) {
              double expected = sample(c, x, y);
              double gray_expected = 0;
              if (c < 3 && depth == 32) {
                float a = float(sample(0, x, y)) + (forward ? m.offset_rgb_f : m.offset_y_f);
                float b = float(sample(1, x, y)) + (forward ? m.offset_rgb_f : 0.f);
                float r = float(sample(2, x, y)) + (forward ? m.offset_rgb_f : 0.f);
                float v = fw[c][0] * a + fw[c][1] * b + fw[c][2] * r;
                v += forward ? (c == 0 ? m.offset_y_f : 0.f) : m.offset_rgb_f;
                gray_expected = v;
                // Preserve reverse F32 excursions on both C and SIMD targets.
                expected = forward ? std::clamp(v, c > 0 ? -.5f : 0.f, c > 0 ? .5f : 1.f) : v;
              } else if (c < 3) {
                const double center = 1 << (depth - 1), scale = 1 << precision;
                const double a = sample(0, x, y) + (forward ? m.offset_rgb : m.offset_y);
                const double b = sample(1, x, y) + (forward ? m.offset_rgb : -center);
                const double r = sample(2, x, y) + (forward ? m.offset_rgb : -center);
                const double offset = forward ? (c == 0 ? m.offset_y : center) : m.offset_rgb;
                expected = std::clamp(
                    std::floor((weights[c][0] * a + weights[c][1] * b + weights[c][2] * r) / scale + .5) + offset, 0.,
                    double((1 << depth) - 1));
              }
              const auto* row = output->GetReadPtr(dst_planes[c]) + y * output->GetPitch(dst_planes[c]);
              const double actual = depth == 8    ? row[x]
                                    : depth == 32 ? reinterpret_cast<const float*>(row)[x]
                                                  : reinterpret_cast<const uint16_t*>(row)[x];
              EXPECT_NEAR(actual, expected, depth == 32 ? 2e-7 : 0) << c << '/' << x << '/' << y;
              if (forward && c == 0) {
                const auto* gray_row = gray->GetReadPtr() + y * gray->GetPitch();
                const double value = depth == 8    ? gray_row[x]
                                     : depth == 32 ? reinterpret_cast<const float*>(gray_row)[x]
                                                   : reinterpret_cast<const uint16_t*>(gray_row)[x];
                EXPECT_NEAR(value, depth == 32 ? gray_expected : expected, depth == 32 ? 2e-7 : 0);
                for (int plane : {PLANAR_R, PLANAR_G, PLANAR_B}) {
                  const auto* rgb_row = gray_rgb->GetReadPtr(plane) + y * gray_rgb->GetPitch(plane);
                  const double rgb_value = depth == 8    ? rgb_row[x]
                                           : depth == 32 ? reinterpret_cast<const float*>(rgb_row)[x]
                                                         : reinterpret_cast<const uint16_t*>(rgb_row)[x];
                  EXPECT_NEAR(rgb_value, depth == 32 ? gray_expected : expected, depth == 32 ? 2e-7 : 0);
                }
              }
            }
        if (forward)
          for (int y = 0; y < vi.height; ++y)
            EXPECT_EQ(std::memcmp(gray_rgb->GetReadPtr(PLANAR_A) + y * gray_rgb->GetPitch(PLANAR_A),
                                  source_frame->GetReadPtr(PLANAR_A) + y * source_frame->GetPitch(PLANAR_A),
                                  vi.width * vi.ComponentSize()),
                      0);
        EXPECT_EQ(FrameSnapshot::capture(source_frame, vi), snapshot);
      }
}
INSTANTIATE_TEST_SUITE_P(StorageDirectionAndCpu, PlanarMatrix, ::testing::Range(0, 24));
} // namespace
} // namespace avsut::test
