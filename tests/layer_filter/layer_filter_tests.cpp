#include "invert/invert.h"
using aif::filters::invert::Invert;
#include "channel_display/show_channel.h"
using aif::filters::channel_display::ShowChannel;
#include "rgb_merge/merge_rgb.h"
using aif::filters::rgb_merge::MergeRGB;
#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_LAYER_FILTER_UNDEF_AVS_UNUSED
#endif
#include "filters/layer.h"
#ifdef AVSUT_LAYER_FILTER_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_LAYER_FILTER_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_helper.h"

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <utility>
#include <vector>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSequenceClip;
using avsut::test::FrameSnapshot;
using avsut::test::get_frame_property_int;
using avsut::test::make_video_info;
using avsut::test::read_frame_plane_active;
using avsut::test::set_frame_property_int;
using avsut::test::StaticFrameClip;
using avsut::test::VideoInfoSpec;
using avsut::test::video_frame_planes;
using avsut::test::write_frame_plane;

// Fresh output frames need only agree on active pixels, not allocator padding.
std::vector<std::vector<std::uint8_t>> active_bytes(const PVideoFrame& frame, const VideoInfo& vi) {
  std::vector<std::vector<std::uint8_t>> result;
  for (int plane : video_frame_planes(vi))
    result.push_back(read_frame_plane_active<std::uint8_t>(frame, plane));
  return result;
}

struct Rgb32Pixel {
  std::uint8_t blue;
  std::uint8_t green;
  std::uint8_t red;
  std::uint8_t alpha;
};

struct Rgb64Pixel {
  std::uint16_t blue;
  std::uint16_t green;
  std::uint16_t red;
  std::uint16_t alpha;
};

void write_rgb32(PVideoFrame& frame, const std::vector<Rgb32Pixel>& pixels) {
  const int pitch = frame->GetPitch();
  const int width = frame->GetRowSize() / 4;
  ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width));
  for (int y = 0; y < frame->GetHeight(); ++y) {
    auto* row = frame->GetWritePtr() + y * pitch;
    for (int x = 0; x < width; ++x) {
      const auto& pixel = pixels[static_cast<std::size_t>(x)];
      row[4 * x + 0] = pixel.blue;
      row[4 * x + 1] = pixel.green;
      row[4 * x + 2] = pixel.red;
      row[4 * x + 3] = pixel.alpha;
    }
  }
}

void write_bgr64(PVideoFrame& frame, const std::vector<Rgb64Pixel>& pixels) {
  const int pitch = frame->GetPitch();
  const int width = frame->GetRowSize() / static_cast<int>(sizeof(Rgb64Pixel));
  ASSERT_EQ(pixels.size(), static_cast<std::size_t>(width));
  for (int y = 0; y < frame->GetHeight(); ++y) {
    auto* row = reinterpret_cast<Rgb64Pixel*>(frame->GetWritePtr() + y * pitch);
    for (int x = 0; x < width; ++x) {
      row[x] = pixels[static_cast<std::size_t>(x)];
    }
  }
}

std::uint8_t rec601_luma(const Rgb32Pixel& pixel) {
  return static_cast<std::uint8_t>(
      (3736 * pixel.blue + 19234 * pixel.green + 9798 * pixel.red + 16384) >> 15);
}

TEST(MaskFilter, WritesRec601AlphaAndUsesLastMaskFrame) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 2;
  const auto vi_two_frames =
      make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR32, 2, 25, 1});
  const auto vi_one_frame =
      make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR32, 1, 25, 1});

  PVideoFrame source_frame0 = environment.get()->NewVideoFrame(vi_two_frames);
  PVideoFrame source_frame1 = environment.get()->NewVideoFrame(vi_two_frames);
  fill_plane_full_pitch(source_frame0, 0xa1, DEFAULT_PLANE);
  fill_plane_full_pitch(source_frame1, 0xa2, DEFAULT_PLANE);
  write_rgb32(source_frame0, {{1, 2, 3, 4},
                              {10, 20, 30, 40},
                              {50, 60, 70, 80},
                              {90, 100, 110, 120},
                              {130, 140, 150, 160},
                              {170, 180, 190, 200},
                              {220, 230, 240, 250}});
  write_rgb32(source_frame1, {{9, 8, 7, 6},
                              {19, 29, 39, 49},
                              {59, 69, 79, 89},
                              {99, 109, 119, 129},
                              {139, 149, 159, 169},
                              {179, 189, 199, 209},
                              {229, 239, 249, 255}});

  PVideoFrame mask_frame = environment.get()->NewVideoFrame(vi_one_frame);
  fill_plane_full_pitch(mask_frame, 0xb3, DEFAULT_PLANE);
  const std::vector<Rgb32Pixel> mask_pixels{
      {40, 120, 200, 0},  {37, 115, 193, 1}, {43, 125, 207, 2}, {0, 0, 0, 3},
      {255, 255, 255, 4}, {80, 160, 32, 5},  {200, 100, 40, 6}};
  write_rgb32(mask_frame, mask_pixels);

  const auto source_before = FrameSnapshot::capture(source_frame1, vi_two_frames);
  const auto mask_before = FrameSnapshot::capture(mask_frame, vi_one_frame);
  auto* source_clip = new FrameSequenceClip(vi_two_frames, {source_frame0, source_frame1});
  auto* mask_clip = new StaticFrameClip(vi_one_frame, mask_frame);
  const PClip source(source_clip);
  const PClip mask(mask_clip);

  Mask filter(source, mask, environment.get());
  const PVideoFrame output = filter.GetFrame(1, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* row = output->GetReadPtr() + y * output->GetPitch();
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(row[4 * x + 3], rec601_luma(mask_pixels[static_cast<std::size_t>(x)]))
          << "x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(mask_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source_frame1, vi_two_frames), source_before);
  EXPECT_EQ(FrameSnapshot::capture(mask_frame, vi_one_frame), mask_before);
}

TEST(ColorKeyMaskFilter, ClearsAlphaOnlyForInclusiveRgbToleranceMatches) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 2;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR32, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xc4, DEFAULT_PLANE);
  const std::vector<Rgb32Pixel> pixels{{40, 120, 200, 17}, {37, 115, 193, 31}, {36, 120, 200, 45},
                                       {40, 125, 200, 59}, {40, 126, 200, 73}, {40, 120, 207, 87},
                                       {40, 120, 208, 101}};
  write_rgb32(source, pixels);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  ColorKeyMask filter(clip, 0xc87828, 3, 5, 7, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* row = output->GetReadPtr() + y * output->GetPitch();
    for (int x = 0; x < width; ++x) {
      const auto& pixel = pixels[static_cast<std::size_t>(x)];
      const bool matches = std::abs(static_cast<int>(pixel.blue) - 40) <= 3 &&
                           std::abs(static_cast<int>(pixel.green) - 120) <= 5 &&
                           std::abs(static_cast<int>(pixel.red) - 200) <= 7;
      EXPECT_EQ(row[4 * x + 0], pixel.blue) << "blue x=" << x << " y=" << y;
      EXPECT_EQ(row[4 * x + 1], pixel.green) << "green x=" << x << " y=" << y;
      EXPECT_EQ(row[4 * x + 2], pixel.red) << "red x=" << x << " y=" << y;
      EXPECT_EQ(row[4 * x + 3], matches ? 0 : pixel.alpha) << "alpha x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(ResetMaskFilter, WritesPackedAlphaFromMaskValue) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 2;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR32, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xc9, DEFAULT_PLANE);
  for (int y = 0; y < height; ++y) {
    auto* row = source->GetWritePtr() + y * source->GetPitch();
    for (int x = 0; x < width; ++x) {
      row[4 * x + 0] = static_cast<std::uint8_t>(11 + x * 13 + y * 7);
      row[4 * x + 1] = static_cast<std::uint8_t>(23 + x * 17 + y * 5);
      row[4 * x + 2] = static_cast<std::uint8_t>(37 + x * 19 + y * 3);
      row[4 * x + 3] = static_cast<std::uint8_t>(191 - x * 11 - y * 9);
    }
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  ResetMask filter(clip, 37.0f, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* source_row = source->GetReadPtr() + y * source->GetPitch();
    const auto* output_row = output->GetReadPtr() + y * output->GetPitch();
    for (int x = 0; x < width; ++x) {
      for (int component = 0; component < 3; ++component) {
        EXPECT_EQ(output_row[4 * x + component], source_row[4 * x + component])
            << "component=" << component << " x=" << x << " y=" << y;
      }
      EXPECT_EQ(output_row[4 * x + 3], 37) << "alpha x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(ResetMaskFilter, WritesExplicitMaskToPlanarYuvaAlpha) {
  AviSynthEnvironment environment;
  constexpr int width = 6;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(source, 0xb2, PLANAR_U);
  fill_plane_full_pitch(source, 0xc3, PLANAR_V);
  fill_plane_full_pitch(source, 0xd4, PLANAR_A);
  write_frame_plane<std::uint8_t>(source, PLANAR_Y,
                                  [](int x, int y) { return 17 + x * 9 + y * 13; });
  write_frame_plane<std::uint8_t>(source, PLANAR_U,
                                  [](int x, int y) { return 61 + x * 7 + y * 11; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V,
                                  [](int x, int y) { return 193 - x * 5 - y * 17; });
  write_frame_plane<std::uint8_t>(source, PLANAR_A,
                                  [](int x, int y) { return 23 + x * 3 + y * 19; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  ResetMask filter(clip, 128.0F, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, plane),
              read_frame_plane_active<std::uint8_t>(source, plane))
        << "plane=" << plane;
  }
  for (int y = 0; y < output->GetHeight(PLANAR_A); ++y) {
    const auto* row = output->GetReadPtr(PLANAR_A) + y * output->GetPitch(PLANAR_A);
    for (int x = 0; x < output->GetRowSize(PLANAR_A); ++x) {
      EXPECT_EQ(row[x], 128) << "alpha x=" << x << " y=" << y;
    }
  }
  const auto output_before = active_bytes(output, vi);
  const PVideoFrame repeat = filter.GetFrame(0, environment.get());
  EXPECT_EQ(active_bytes(repeat, vi), output_before);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_NE(repeat->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(ResetMaskFilter, RejectsMissingAlphaAndAcceptsDefaultMaskBeforeFrameRequest) {
  AviSynthEnvironment environment;
  constexpr int width = 4;
  constexpr int height = 2;
  const auto no_alpha_vi =
      make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  const auto alpha_vi =
      make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUVA444, 1, 25, 1});
  PVideoFrame no_alpha = environment.get()->NewVideoFrame(no_alpha_vi);
  PVideoFrame alpha = environment.get()->NewVideoFrame(alpha_vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(no_alpha, static_cast<std::uint8_t>(0x51 + plane), plane);
    fill_plane_full_pitch(alpha, static_cast<std::uint8_t>(0x61 + plane), plane);
  }
  fill_plane_full_pitch(alpha, 0xa4, PLANAR_A);
  const auto no_alpha_before = FrameSnapshot::capture(no_alpha, no_alpha_vi);
  const auto alpha_before = FrameSnapshot::capture(alpha, alpha_vi);
  auto* no_alpha_clip_impl = new StaticFrameClip(no_alpha_vi, no_alpha);
  auto* alpha_clip_impl = new StaticFrameClip(alpha_vi, alpha);
  const PClip no_alpha_clip(no_alpha_clip_impl);
  const PClip alpha_clip(alpha_clip_impl);

  EXPECT_THROW(
      { ResetMask filter(no_alpha_clip, -1.0F, environment.get()); },
      AvisynthError);
  EXPECT_NO_THROW({ ResetMask filter(alpha_clip, -1.0F, environment.get()); });
  EXPECT_TRUE(no_alpha_clip_impl->frame_requests().empty());
  EXPECT_TRUE(alpha_clip_impl->frame_requests().empty());
  EXPECT_EQ(FrameSnapshot::capture(no_alpha, no_alpha_vi), no_alpha_before);
  EXPECT_EQ(FrameSnapshot::capture(alpha, alpha_vi), alpha_before);
}

TEST(ShowChannelFilter, ExtractsPackedRedToYuvaAndPreservesAlpha) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR32, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xe5, DEFAULT_PLANE);
  for (int raw_y = 0; raw_y < height; ++raw_y) {
    auto* row = source->GetWritePtr() + raw_y * source->GetPitch();
    for (int x = 0; x < width; ++x) {
      row[4 * x + 0] = static_cast<std::uint8_t>(7 + x * 11 + raw_y * 17);
      row[4 * x + 1] = static_cast<std::uint8_t>(29 + x * 13 + raw_y * 19);
      row[4 * x + 2] = static_cast<std::uint8_t>(53 + x * 17 + raw_y * 23);
      row[4 * x + 3] = static_cast<std::uint8_t>(101 + x * 7 + raw_y * 5);
    }
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  ShowChannel filter(clip, "yuva444", 2, environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YUVA444);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const int source_y = height - 1 - y;
    const auto* source_row = source->GetReadPtr() + source_y * source->GetPitch();
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    const auto* output_a = output->GetReadPtr(PLANAR_A) + y * output->GetPitch(PLANAR_A);
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(output_y[x], source_row[4 * x + 2]) << "Y x=" << x << " y=" << y;
      EXPECT_EQ(output_u[x], 128) << "U x=" << x << " y=" << y;
      EXPECT_EQ(output_v[x], 128) << "V x=" << x << " y=" << y;
      EXPECT_EQ(output_a[x], source_row[4 * x + 3]) << "A x=" << x << " y=" << y;
    }
  }
  const auto output_before = active_bytes(output, filter.GetVideoInfo());
  const PVideoFrame repeat = filter.GetFrame(0, environment.get());
  EXPECT_EQ(active_bytes(repeat, filter.GetVideoInfo()), output_before);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_NE(repeat->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(ShowChannelFilter, RejectsInvalidChannelAndSubsampledOutputBeforeFrameRequest) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x61, PLANAR_Y);
  fill_plane_full_pitch(source, 0x72, PLANAR_U);
  fill_plane_full_pitch(source, 0x83, PLANAR_V);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  EXPECT_THROW(
      { ShowChannel filter(clip, "rgb", 0, environment.get()); }, AvisynthError);
  EXPECT_THROW(
      { ShowChannel filter(clip, "yuv420", 4, environment.get()); }, AvisynthError);
  EXPECT_TRUE(source_clip->frame_requests().empty());
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(MergeRgbFilter, AssemblesPlanarRgbapFromPlanarChannelSources) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBP8, 1, 25, 1});
  const auto alpha_vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_RGBAP8, 1, 25, 1});

  PVideoFrame blue = environment.get()->NewVideoFrame(vi);
  PVideoFrame green = environment.get()->NewVideoFrame(vi);
  PVideoFrame red = environment.get()->NewVideoFrame(vi);
  PVideoFrame alpha = environment.get()->NewVideoFrame(alpha_vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(blue, static_cast<std::uint8_t>(0x11 + plane), plane);
    fill_plane_full_pitch(green, static_cast<std::uint8_t>(0x21 + plane), plane);
    fill_plane_full_pitch(red, static_cast<std::uint8_t>(0x31 + plane), plane);
  }
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A}) {
    fill_plane_full_pitch(alpha, static_cast<std::uint8_t>(0x41 + plane), plane);
  }
  write_frame_plane<std::uint8_t>(blue, PLANAR_B,
                                  [](int x, int y) { return 11 + x * 7 + y * 13; });
  write_frame_plane<std::uint8_t>(green, PLANAR_G,
                                  [](int x, int y) { return 37 + x * 11 + y * 17; });
  write_frame_plane<std::uint8_t>(red, PLANAR_R,
                                  [](int x, int y) { return 71 + x * 13 + y * 19; });
  write_frame_plane<std::uint8_t>(alpha, PLANAR_A,
                                  [](int x, int y) { return 101 + x * 5 + y * 23; });

  const auto blue_before = FrameSnapshot::capture(blue, vi);
  const auto green_before = FrameSnapshot::capture(green, vi);
  const auto red_before = FrameSnapshot::capture(red, vi);
  const auto alpha_before = FrameSnapshot::capture(alpha, alpha_vi);
  auto* blue_clip_impl = new StaticFrameClip(vi, blue);
  auto* green_clip_impl = new StaticFrameClip(vi, green);
  auto* red_clip_impl = new StaticFrameClip(vi, red);
  auto* alpha_clip_impl = new StaticFrameClip(alpha_vi, alpha);
  const PClip blue_clip(blue_clip_impl);
  const PClip green_clip(green_clip_impl);
  const PClip red_clip(red_clip_impl);
  const PClip alpha_clip(alpha_clip_impl);

  MergeRGB filter(red_clip, blue_clip, green_clip, red_clip, alpha_clip, "rgbap",
                  environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_RGBAP8);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_G),
            read_frame_plane_active<std::uint8_t>(green, PLANAR_G));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_B),
            read_frame_plane_active<std::uint8_t>(blue, PLANAR_B));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_R),
            read_frame_plane_active<std::uint8_t>(red, PLANAR_R));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_A),
            read_frame_plane_active<std::uint8_t>(alpha, PLANAR_A));
  const auto output_before = active_bytes(output, filter.GetVideoInfo());
  const PVideoFrame repeat = filter.GetFrame(0, environment.get());
  EXPECT_EQ(active_bytes(repeat, filter.GetVideoInfo()), output_before);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_NE(repeat->CheckMemory(), 1);
  EXPECT_EQ(blue_clip_impl->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(green_clip_impl->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(red_clip_impl->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(alpha_clip_impl->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(FrameSnapshot::capture(blue, vi), blue_before);
  EXPECT_EQ(FrameSnapshot::capture(green, vi), green_before);
  EXPECT_EQ(FrameSnapshot::capture(red, vi), red_before);
  EXPECT_EQ(FrameSnapshot::capture(alpha, alpha_vi), alpha_before);
}

TEST(MergeRgbFilter, RejectsMismatchedChannelsBeforeFrameRequest) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBP8, 1, 25, 1});
  const auto wide_vi = make_video_info(
      VideoInfoSpec{width + 1, height, VideoInfo::CS_RGBP8, 1, 25, 1});

  auto make_source = [&](const VideoInfo& source_vi, std::uint8_t value) {
    PVideoFrame frame = environment.get()->NewVideoFrame(source_vi);
    for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
      fill_plane_full_pitch(frame, static_cast<std::uint8_t>(value + plane), plane);
    }
    return frame;
  };
  PVideoFrame blue = make_source(vi, 0x21);
  PVideoFrame green = make_source(vi, 0x31);
  PVideoFrame wide_green = make_source(wide_vi, 0x39);
  PVideoFrame red = make_source(vi, 0x41);
  PVideoFrame alpha = make_source(vi, 0x51);
  const auto blue_before = FrameSnapshot::capture(blue, vi);
  const auto green_before = FrameSnapshot::capture(green, vi);
  const auto wide_green_before = FrameSnapshot::capture(wide_green, wide_vi);
  const auto red_before = FrameSnapshot::capture(red, vi);
  const auto alpha_before = FrameSnapshot::capture(alpha, vi);
  auto* blue_clip_impl = new StaticFrameClip(vi, blue);
  auto* green_clip_impl = new StaticFrameClip(vi, green);
  auto* wide_green_clip_impl = new StaticFrameClip(wide_vi, wide_green);
  auto* red_clip_impl = new StaticFrameClip(vi, red);
  auto* alpha_clip_impl = new StaticFrameClip(vi, alpha);
  const PClip blue_clip(blue_clip_impl);
  const PClip green_clip(green_clip_impl);
  const PClip wide_green_clip(wide_green_clip_impl);
  const PClip red_clip(red_clip_impl);
  const PClip alpha_clip(alpha_clip_impl);

  EXPECT_THROW(
      {
        MergeRGB filter(red_clip, blue_clip, wide_green_clip, red_clip, PClip(), "rgb",
                        environment.get());
      },
      AvisynthError);
  EXPECT_THROW(
      {
        MergeRGB filter(red_clip, blue_clip, green_clip, red_clip, alpha_clip, "rgbap",
                        environment.get());
      },
      AvisynthError);
  EXPECT_TRUE(blue_clip_impl->frame_requests().empty());
  EXPECT_TRUE(green_clip_impl->frame_requests().empty());
  EXPECT_TRUE(wide_green_clip_impl->frame_requests().empty());
  EXPECT_TRUE(red_clip_impl->frame_requests().empty());
  EXPECT_TRUE(alpha_clip_impl->frame_requests().empty());
  EXPECT_EQ(FrameSnapshot::capture(blue, vi), blue_before);
  EXPECT_EQ(FrameSnapshot::capture(green, vi), green_before);
  EXPECT_EQ(FrameSnapshot::capture(wide_green, wide_vi), wide_green_before);
  EXPECT_EQ(FrameSnapshot::capture(red, vi), red_before);
  EXPECT_EQ(FrameSnapshot::capture(alpha, vi), alpha_before);
}

template <std::size_t N>
void write_plane_values(PVideoFrame& frame, int plane, const std::array<std::uint8_t, N>& values) {
  const int pitch = frame->GetPitch(plane);
  const int width = frame->GetRowSize(plane);
  const int height = frame->GetHeight(plane);
  for (int y = 0; y < height; ++y) {
    auto* row = frame->GetWritePtr(plane) + y * pitch;
    for (int x = 0; x < width; ++x) {
      row[x] = values[static_cast<std::size_t>(x) % values.size()];
    }
  }
}

TEST(InvertFilter, InvertsSelectedYuvPlanesAndCopiesUnselectedPlane) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xd5, PLANAR_Y);
  fill_plane_full_pitch(source, 0xe6, PLANAR_U);
  fill_plane_full_pitch(source, 0xf7, PLANAR_V);
  constexpr std::array<std::uint8_t, 7> y_values{0, 1, 16, 127, 128, 254, 255};
  constexpr std::array<std::uint8_t, 7> u_values{0, 1, 64, 127, 128, 254, 255};
  constexpr std::array<std::uint8_t, 7> v_values{3, 19, 47, 89, 137, 201, 251};
  write_plane_values(source, PLANAR_Y, y_values);
  write_plane_values(source, PLANAR_U, u_values);
  write_plane_values(source, PLANAR_V, v_values);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Invert filter(clip, "YU", environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < width; ++x) {
      const auto y_value = y_values[static_cast<std::size_t>(x)];
      const auto u_value = u_values[static_cast<std::size_t>(x)];
      EXPECT_EQ(output_y[x], static_cast<std::uint8_t>(255 - y_value)) << "Y x=" << x << " y=" << y;
      EXPECT_EQ(output_u[x],
                static_cast<std::uint8_t>(255 - static_cast<int>(u_value)))
          << "U x=" << x << " y=" << y;
      EXPECT_EQ(output_v[x], v_values[static_cast<std::size_t>(x)]) << "V x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

std::uint8_t subtract_reference(std::uint8_t first, std::uint8_t second, int bias) {
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(first) - static_cast<int>(second) + bias, 0, 255));
}

void write_yuv_frame(PVideoFrame& frame, const std::array<std::uint8_t, 7>& y_values,
                     const std::array<std::uint8_t, 7>& u_values,
                     const std::array<std::uint8_t, 7>& v_values) {
  fill_plane_full_pitch(frame, 0x11, PLANAR_Y);
  fill_plane_full_pitch(frame, 0x22, PLANAR_U);
  fill_plane_full_pitch(frame, 0x33, PLANAR_V);
  write_plane_values(frame, PLANAR_Y, y_values);
  write_plane_values(frame, PLANAR_U, u_values);
  write_plane_values(frame, PLANAR_V, v_values);
}

TEST(SubtractFilter, AppliesLumaAndChromaCenteredDifferencesAcrossFrameSequences) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 2;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 2, 25, 1});
  const std::array<std::uint8_t, 7> first_y{0, 10, 100, 126, 200, 250, 255};
  const std::array<std::uint8_t, 7> first_u{0, 20, 100, 128, 200, 250, 255};
  const std::array<std::uint8_t, 7> first_v{255, 200, 128, 100, 20, 10, 0};
  const std::array<std::uint8_t, 7> second_y{255, 0, 90, 126, 0, 10, 255};
  const std::array<std::uint8_t, 7> second_u{255, 0, 90, 128, 0, 10, 255};
  const std::array<std::uint8_t, 7> second_v{0, 10, 128, 100, 20, 200, 255};
  PVideoFrame first_frame0 = environment.get()->NewVideoFrame(vi);
  PVideoFrame first_frame1 = environment.get()->NewVideoFrame(vi);
  PVideoFrame second_frame0 = environment.get()->NewVideoFrame(vi);
  PVideoFrame second_frame1 = environment.get()->NewVideoFrame(vi);
  write_yuv_frame(first_frame0, first_y, first_u, first_v);
  write_yuv_frame(first_frame1, first_y, first_u, first_v);
  write_yuv_frame(second_frame0, second_y, second_u, second_v);
  write_yuv_frame(second_frame1, second_y, second_u, second_v);
  const auto first_before = FrameSnapshot::capture(first_frame1, vi);
  const auto second_before = FrameSnapshot::capture(second_frame1, vi);
  auto* first_clip = new FrameSequenceClip(vi, {first_frame0, first_frame1});
  auto* second_clip = new FrameSequenceClip(vi, {second_frame0, second_frame1});
  const PClip first(first_clip);
  const PClip second(second_clip);

  Subtract filter(first, second, environment.get());
  const PVideoFrame output = filter.GetFrame(1, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(x);
      EXPECT_EQ(output_y[x], subtract_reference(first_y[index], second_y[index], 126))
          << "Y x=" << x << " y=" << y;
      EXPECT_EQ(output_u[x], subtract_reference(first_u[index], second_u[index], 128))
          << "U x=" << x << " y=" << y;
      EXPECT_EQ(output_v[x], subtract_reference(first_v[index], second_v[index], 128))
          << "V x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(first_clip->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(second_clip->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(FrameSnapshot::capture(first_frame1, vi), first_before);
  EXPECT_EQ(FrameSnapshot::capture(second_frame1, vi), second_before);
}

} // namespace
