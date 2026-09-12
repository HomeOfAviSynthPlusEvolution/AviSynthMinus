#include "support/video_filter_test_support.h"
#include <array>
#include <avisynth.h>
#include "convert/convert_planar.h"
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <tuple>
#include <vector>
namespace {
using namespace avsut::test;
std::vector<int> Planes(const VideoInfo& vi) {
  return !vi.IsPlanar() || vi.IsY() ? std::vector<int>{DEFAULT_PLANE} : video_frame_planes(vi);
}
class ResizeModule : public testing::TestWithParam<std::tuple<const char*, int>> {};
TEST_P(ResizeModule, PublicNativeMatchesNoneAcrossBothAxisOrders) {
  const auto [name, pixel_type] = GetParam();
  for (const auto size : {std::array<int, 2>{66, 52}, std::array<int, 2>{194, 18}}) {
    std::vector<double> expected;
    for (bool scalar : {true, false}) {
      AviSynthEnvironment environment;
      auto* env = environment.get();
      if (scalar)
        env->Invoke("SetMaxCPU", "none");
      const auto vi = make_video_info({130, 34, pixel_type, 1, 25, 1});
      auto frame = env->NewVideoFrame(vi);
      int channel = 0;
      for (int plane : Planes(vi)) {
        for (int y = 0; y < frame->GetHeight(plane); ++y) {
          auto* row = frame->GetWritePtr(plane) + ptrdiff_t(y) * frame->GetPitch(plane);
          for (int x = 0; x < frame->GetRowSize(plane) / vi.ComponentSize(); ++x) {
            const unsigned value = unsigned(x * 139 + y * 997 + channel * 1999);
            if (vi.ComponentSize() == 1)
              row[x] = uint8_t(value);
            else if (vi.ComponentSize() == 2)
              reinterpret_cast<uint16_t*>(row)[x] = uint16_t(value & ((1u << vi.BitsPerComponent()) - 1));
            else
              reinterpret_cast<float*>(row)[x] = float(int(value % 2049) - 512) / 1024;
          }
        }
        ++channel;
      }
      set_frame_property_int(env, frame, "MigrationMarker", 77);
      set_frame_property_int(env, frame, "_ChromaLocation", 1);
      set_frame_property_int(env, frame, "_ColorRange", 0);
      const PClip source = new StaticFrameClip(vi, frame);
      const AVSValue args[] = {source, size[0], size[1]};
      const PClip resized = env->Invoke(name, AVSValue(args, 3)).AsClip();
      EXPECT_EQ(resized->GetVideoInfo().width, size[0]);
      EXPECT_EQ(resized->GetVideoInfo().height, size[1]);
      EXPECT_EQ(resized->GetVideoInfo().pixel_type, pixel_type);
      const auto output = resized->GetFrame(0, env);
      EXPECT_EQ(get_frame_property_int(env, output, "MigrationMarker"), 77);
      EXPECT_EQ(get_frame_property_int(env, output, "_ColorRange"), 0);
      EXPECT_NE(output->CheckMemory(), 1);
      if (vi.IsYUY2()) {
        // Independent full-frame layout route used by the former wrapper chain.
        const PClip planar = new ConvertYUY2ToYV16(source, env);
        // YUY2 Resize historically uses the default centered horizontal grid;
        // the generic planar conversion scripts may instead alter chroma placement.
        const AVSValue planar_args[] = {planar, size[0], size[1], "center"};
        const char* arg_names[] = {nullptr, nullptr, nullptr, "placement"};
        const PClip planar_resize = env->Invoke(name, AVSValue(planar_args, 4), arg_names).AsClip();
        const PClip repacked = new ConvertYV16ToYUY2(planar_resize, env);
        const auto reference = repacked->GetFrame(0, env);
        for (int y = 0; y < output->GetHeight(); ++y)
          EXPECT_EQ(std::memcmp(output->GetReadPtr() + ptrdiff_t(y) * output->GetPitch(),
                                reference->GetReadPtr() + ptrdiff_t(y) * reference->GetPitch(), output->GetRowSize()),
                    0)
              << name << " row " << y;
      }
      std::vector<double> samples;
      for (int plane : Planes(vi)) {
        for (int y = 0; y < output->GetHeight(plane); ++y) {
          const auto* row = output->GetReadPtr(plane) + ptrdiff_t(y) * output->GetPitch(plane);
          for (int x = 0; x < output->GetRowSize(plane) / vi.ComponentSize(); ++x)
            samples.push_back(vi.ComponentSize() == 1   ? double(row[x])
                              : vi.ComponentSize() == 2 ? double(reinterpret_cast<const uint16_t*>(row)[x])
                                                        : double(reinterpret_cast<const float*>(row)[x]));
        }
      }
      if (scalar)
        expected = std::move(samples);
      else
        EXPECT_EQ(expected, samples) << name << " " << size[0] << "x" << size[1];
    }
  }
}
INSTANTIATE_TEST_SUITE_P(
    Formats, ResizeModule,
    testing::Combine(testing::Values("PointResize", "BilinearResize", "BicubicResize", "LanczosResize",
                                     "Lanczos4Resize", "BlackmanResize", "Spline16Resize", "Spline36Resize",
                                     "Spline64Resize", "GaussResize", "SincResize", "SinPowerResize", "SincLin2Resize",
                                     "UserDefined2Resize"),
                     testing::Values(VideoInfo::CS_Y8, VideoInfo::CS_Y16, VideoInfo::CS_Y32, VideoInfo::CS_YUV420P10,
                                     VideoInfo::CS_YUVA444, VideoInfo::CS_RGBAPS, VideoInfo::CS_YUY2,
                                     VideoInfo::CS_BGR24, VideoInfo::CS_BGR32, VideoInfo::CS_BGR48,
                                     VideoInfo::CS_BGR64)));
} // namespace
