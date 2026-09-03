#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_COLOR_UNDEF_AVS_UNUSED
#endif
#include "filters/color.h"
#ifdef AVSUT_COLOR_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_COLOR_UNDEF_AVS_UNUSED
#endif

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSnapshot;
using avsut::test::make_video_info;
using avsut::test::StaticFrameClip;
using avsut::test::VideoInfoSpec;
using avsut::test::read_frame_plane_active;
using avsut::test::write_frame_plane;

template <typename Pixel>
void write_values(PVideoFrame& frame, int plane, int width, int height,
                  const std::array<Pixel, 8>& values) {
  const int pitch = frame->GetPitch(plane);
  for (int y = 0; y < height; ++y) {
    auto* row = reinterpret_cast<Pixel*>(frame->GetWritePtr(plane) + y * pitch);
    for (int x = 0; x < width; ++x) {
      row[x] = values[static_cast<std::size_t>(x) % values.size()];
    }
  }
}

ColorYUV make_offset_filter(const PClip& clip, IScriptEnvironment* environment) {
  return ColorYUV(clip, 0.0, 16.0, 0.0, 0.0, 0.0, -16.0, 0.0, 0.0, 0.0, 32.0, 0.0, 0.0, "", "",
                  false, false, false, false, false, 0, false, false, "", false, environment);
}

TEST(ColorYUV, AppliesIndependentIntegerOffsetsToYuvPlanes) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x91, PLANAR_Y);
  fill_plane_full_pitch(source, 0x82, PLANAR_U);
  fill_plane_full_pitch(source, 0x73, PLANAR_V);
  const std::array<std::uint8_t, 8> values{0, 16, 64, 96, 128, 192, 240, 255};
  write_values<std::uint8_t>(source, PLANAR_Y, vi.width, vi.height, values);
  write_values<std::uint8_t>(source, PLANAR_U, vi.width, vi.height, values);
  write_values<std::uint8_t>(source, PLANAR_V, vi.width, vi.height, values);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  ColorYUV filter = make_offset_filter(clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int pitch = output->GetPitch(plane);
    const int source_pitch = source->GetPitch(plane);
    const auto* output_base = output->GetReadPtr(plane);
    const auto* source_base = source->GetReadPtr(plane);
    const int offset = plane == PLANAR_Y ? 16 : plane == PLANAR_U ? -16 : 32;
    for (int y = 0; y < vi.height; ++y) {
      const auto* output_row = output_base + y * pitch;
      const auto* source_row = source_base + y * source_pitch;
      for (int x = 0; x < vi.width; ++x) {
        const int expected = std::clamp(static_cast<int>(source_row[x]) + offset, 0, 255);
        EXPECT_EQ(output_row[x], expected) << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  const std::vector<int> expected_requests{0, 0};
  EXPECT_EQ(source_clip->frame_requests(), expected_requests);
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(ColorYUV, ConvertsLimitedLumaToFullRange) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xb4, PLANAR_Y);
  write_values<std::uint8_t>(source, PLANAR_Y, vi.width, vi.height,
                             std::array<std::uint8_t, 8>{0, 16, 32, 128, 200, 235, 240, 255});
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  ColorYUV filter(clip, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, "TV->PC", "",
                  false, false, false, false, false, 0, false, false, "", false, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetRowSize(PLANAR_Y), vi.width);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), vi.height);
  for (int y = 0; y < vi.height; ++y) {
    const auto* output_row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* source_row = source->GetReadPtr(PLANAR_Y) + y * source->GetPitch(PLANAR_Y);
    for (int x = 0; x < vi.width; ++x) {
      const int expected =
          std::clamp(static_cast<int>((source_row[x] - 16) * 255.0 / 219.0 + 0.5), 0, 255);
      EXPECT_EQ(output_row[x], expected) << "x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  const std::vector<int> expected_requests{0, 0};
  EXPECT_EQ(source_clip->frame_requests(), expected_requests);
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(ColorYUV, AppliesOffsetsToYuva420AndCopiesAlpha) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x44 + plane * 0x15), plane);
    write_frame_plane<std::uint8_t>(source, plane, [plane](int x, int y) {
      return 5 + plane * 23 + x * 17 + y * 11;
    });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  const auto alpha_before = read_frame_plane_active<std::uint8_t>(source, PLANAR_A);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  ColorYUV filter = make_offset_filter(clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int plane_width = output->GetRowSize(plane);
    const int plane_height = output->GetHeight(plane);
    const int offset = plane == PLANAR_Y ? 16 : plane == PLANAR_U ? -16 : 32;
    for (int y = 0; y < plane_height; ++y) {
      const auto* source_row = source->GetReadPtr(plane) + y * source->GetPitch(plane);
      const auto* output_row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int x = 0; x < plane_width; ++x) {
        const int expected = std::clamp(static_cast<int>(source_row[x]) + offset, 0, 255);
        EXPECT_EQ(output_row[x], expected) << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_A), alpha_before);
  EXPECT_EQ(output->GetRowSize(PLANAR_U), width / 2);
  EXPECT_EQ(output->GetHeight(PLANAR_U), height / 2);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

}  // namespace
