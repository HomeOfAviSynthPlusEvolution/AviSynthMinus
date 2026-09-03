#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_LIMITER_UNDEF_AVS_UNUSED
#endif
#include "filters/limiter.h"
#ifdef AVSUT_LIMITER_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_LIMITER_UNDEF_AVS_UNUSED
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

template <typename Pixel>
void expect_clamped_plane(const PVideoFrame& source, const PVideoFrame& output, int plane,
                          int width, int height, Pixel min_value, Pixel max_value) {
  const int source_pitch = source->GetPitch(plane);
  const int output_pitch = output->GetPitch(plane);
  const auto* source_base = source->GetReadPtr(plane);
  const auto* output_base = output->GetReadPtr(plane);
  for (int y = 0; y < height; ++y) {
    const auto* source_row = reinterpret_cast<const Pixel*>(source_base + y * source_pitch);
    const auto* output_row = reinterpret_cast<const Pixel*>(output_base + y * output_pitch);
    for (int x = 0; x < width; ++x) {
      const Pixel expected = std::clamp(source_row[x], min_value, max_value);
      EXPECT_EQ(output_row[x], expected) << "plane=" << plane << " x=" << x << " y=" << y;
    }
  }
}

TEST(Limiter, ClampsEightBitLumaAndChromaForYuv444) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(source, 0xb2, PLANAR_U);
  fill_plane_full_pitch(source, 0xc3, PLANAR_V);
  const std::array<std::uint8_t, 8> luma_values{0, 15, 16, 64, 128, 235, 236, 255};
  const std::array<std::uint8_t, 8> chroma_values{0, 15, 16, 64, 128, 240, 241, 255};
  write_values<std::uint8_t>(source, PLANAR_Y, vi.width, vi.height, luma_values);
  write_values<std::uint8_t>(source, PLANAR_U, vi.width, vi.height, chroma_values);
  write_values<std::uint8_t>(source, PLANAR_V, vi.width, vi.height, chroma_values);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Limiter filter(clip, 16.0f, 235.0f, 16.0f, 240.0f, 0, false, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_clamped_plane<std::uint8_t>(source, output, PLANAR_Y, vi.width, vi.height, 16, 235);
  expect_clamped_plane<std::uint8_t>(source, output, PLANAR_U, vi.width, vi.height, 16, 240);
  expect_clamped_plane<std::uint8_t>(source, output, PLANAR_V, vi.width, vi.height, 16, 240);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Limiter, ScalesLimitsForTenBitYuv444WhenParamScaleIsEnabled) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_YUV444P10, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xd4, PLANAR_Y);
  fill_plane_full_pitch(source, 0xe5, PLANAR_U);
  fill_plane_full_pitch(source, 0xf6, PLANAR_V);
  const std::array<std::uint16_t, 8> luma_values{0, 63, 64, 256, 512, 940, 941, 1023};
  const std::array<std::uint16_t, 8> chroma_values{0, 63, 64, 256, 512, 960, 961, 1023};
  write_values<std::uint16_t>(source, PLANAR_Y, vi.width, vi.height, luma_values);
  write_values<std::uint16_t>(source, PLANAR_U, vi.width, vi.height, chroma_values);
  write_values<std::uint16_t>(source, PLANAR_V, vi.width, vi.height, chroma_values);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Limiter filter(clip, 16.0f, 235.0f, 16.0f, 240.0f, 0, true, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_clamped_plane<std::uint16_t>(source, output, PLANAR_Y, vi.width, vi.height, 64, 940);
  expect_clamped_plane<std::uint16_t>(source, output, PLANAR_U, vi.width, vi.height, 64, 960);
  expect_clamped_plane<std::uint16_t>(source, output, PLANAR_V, vi.width, vi.height, 64, 960);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Limiter, ScalesLimitsForSixteenBitYuv422WhenParamScaleIsEnabled) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 5;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUV422P16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x41 + plane * 0x15), plane);
    write_frame_plane<std::uint16_t>(source, plane, [](int x, int y) {
      return x * 8192 + y * 4096;
    });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Limiter filter(clip, 16.0f, 235.0f, 16.0f, 240.0f, 0, true, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int plane_width = output->GetRowSize(plane) / static_cast<int>(sizeof(std::uint16_t));
    const int plane_height = output->GetHeight(plane);
    const auto min_value = static_cast<std::uint16_t>(4096);
    const auto max_value = static_cast<std::uint16_t>(plane == PLANAR_Y ? 60160 : 61440);
    expect_clamped_plane<std::uint16_t>(source, output, plane, plane_width, plane_height,
                                        min_value, max_value);
  }
  EXPECT_EQ(output->GetRowSize(PLANAR_U), width / 2 * static_cast<int>(sizeof(std::uint16_t)));
  EXPECT_EQ(output->GetHeight(PLANAR_U), height);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Limiter, ClampsYuva420AndPreservesTheAlphaPlane) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x62 + plane * 0x17), plane);
    write_frame_plane<std::uint8_t>(source, plane, [plane](int x, int y) {
      return 3 + plane * 29 + x * 31 + y * 7;
    });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  const auto alpha_before = read_frame_plane_active<std::uint8_t>(source, PLANAR_A);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Limiter filter(clip, 32.0f, 220.0f, 40.0f, 210.0f, 0, false, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_clamped_plane<std::uint8_t>(source, output, PLANAR_Y, width, height, 32, 220);
  expect_clamped_plane<std::uint8_t>(source, output, PLANAR_U, width / 2, height / 2, 40, 210);
  expect_clamped_plane<std::uint8_t>(source, output, PLANAR_V, width / 2, height / 2, 40, 210);
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_A), alpha_before);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

}  // namespace
