#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_CONVERT_RGB_FILTER_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_rgb.h"
#ifdef AVSUT_CONVERT_RGB_FILTER_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_CONVERT_RGB_FILTER_UNDEF_AVS_UNUSED
#endif

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSnapshot;
using avsut::test::make_video_info;
using avsut::test::StaticFrameClip;
using avsut::test::VideoInfoSpec;
using avsut::test::write_frame_plane;

template <typename Pixel>
void fill_packed_rgb_source(PVideoFrame& frame, int components) {
  fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0xa0 + components), DEFAULT_PLANE);
  write_frame_plane<Pixel>(frame, DEFAULT_PLANE, [components](int component, int y) {
    const int channel = component % components;
    const int pixel = component / components;
    return static_cast<Pixel>(11 + pixel * 37 + y * 53 + channel * 71);
  });
}

template <typename Pixel>
void fill_planar_rgb_source(PVideoFrame& frame, bool has_alpha) {
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0x40 + plane * 0x17), plane);
    write_frame_plane<Pixel>(frame, plane, [plane](int x, int y) {
      return static_cast<Pixel>(13 + plane * 79 + x * 31 + y * 47);
    });
  }
  if (has_alpha) {
    fill_plane_full_pitch(frame, 0xd7, PLANAR_A);
    write_frame_plane<Pixel>(frame, PLANAR_A, [](int x, int y) {
      return static_cast<Pixel>(23 + x * 43 + y * 59);
    });
  }
}

template <typename Pixel>
void expect_packed_conversion(const PVideoFrame& output, const PVideoFrame& source,
                              int source_components, int destination_components,
                              bool preserve_alpha) {
  const int width = source->GetRowSize() / static_cast<int>(sizeof(Pixel)) / source_components;
  ASSERT_EQ(output->GetRowSize(), width * destination_components * static_cast<int>(sizeof(Pixel)));
  ASSERT_EQ(output->GetHeight(), source->GetHeight());
  for (int y = 0; y < source->GetHeight(); ++y) {
    const auto* source_row = reinterpret_cast<const Pixel*>(source->GetReadPtr() + y * source->GetPitch());
    const auto* output_row = reinterpret_cast<const Pixel*>(output->GetReadPtr() + y * output->GetPitch());
    for (int x = 0; x < width; ++x) {
      for (int channel = 0; channel < 3; ++channel) {
        EXPECT_EQ(output_row[x * destination_components + channel],
                  source_row[x * source_components + channel])
            << "channel=" << channel << " x=" << x << " y=" << y;
      }
      if (destination_components == 4) {
        const Pixel expected_alpha = preserve_alpha
                                          ? source_row[x * source_components + 3]
                                          : std::numeric_limits<Pixel>::max();
        EXPECT_EQ(output_row[x * destination_components + 3], expected_alpha)
            << "alpha x=" << x << " y=" << y;
      }
    }
  }
}

template <typename Pixel>
void expect_packed_to_planar(const PVideoFrame& source, const PVideoFrame& output,
                             int source_components, bool source_has_alpha, bool target_has_alpha) {
  const int width = output->GetRowSize(PLANAR_G) / static_cast<int>(sizeof(Pixel));
  const int height = output->GetHeight(PLANAR_G);
  const std::array<int, 3> output_planes{PLANAR_G, PLANAR_B, PLANAR_R};
  const std::array<int, 3> source_channels{1, 0, 2};
  for (int y = 0; y < height; ++y) {
    const int source_y = height - 1 - y;
    const auto* source_row = reinterpret_cast<const Pixel*>(source->GetReadPtr() +
                                                              source_y * source->GetPitch());
    for (std::size_t plane_index = 0; plane_index < output_planes.size(); ++plane_index) {
      const auto* output_row = reinterpret_cast<const Pixel*>(
          output->GetReadPtr(output_planes[plane_index]) + y * output->GetPitch(output_planes[plane_index]));
      for (int x = 0; x < width; ++x) {
        EXPECT_EQ(output_row[x], source_row[x * source_components + source_channels[plane_index]])
            << "plane=" << output_planes[plane_index] << " x=" << x << " y=" << y;
      }
    }
    if (target_has_alpha) {
      const auto* output_row = reinterpret_cast<const Pixel*>(
          output->GetReadPtr(PLANAR_A) + y * output->GetPitch(PLANAR_A));
      for (int x = 0; x < width; ++x) {
        const Pixel expected_alpha = source_has_alpha
                                          ? source_row[x * source_components + 3]
                                          : std::numeric_limits<Pixel>::max();
        EXPECT_EQ(output_row[x], expected_alpha) << "alpha x=" << x << " y=" << y;
      }
    }
  }
}

template <typename Pixel>
void expect_planar_to_packed(const PVideoFrame& source, const PVideoFrame& output,
                             bool source_has_alpha, bool target_has_alpha) {
  const int width = source->GetRowSize(PLANAR_G) / static_cast<int>(sizeof(Pixel));
  const int height = source->GetHeight(PLANAR_G);
  for (int y = 0; y < height; ++y) {
    const int source_y = height - 1 - y;
    const auto* source_g = reinterpret_cast<const Pixel*>(source->GetReadPtr(PLANAR_G) +
                                                           source_y * source->GetPitch(PLANAR_G));
    const auto* source_b = reinterpret_cast<const Pixel*>(source->GetReadPtr(PLANAR_B) +
                                                           source_y * source->GetPitch(PLANAR_B));
    const auto* source_r = reinterpret_cast<const Pixel*>(source->GetReadPtr(PLANAR_R) +
                                                           source_y * source->GetPitch(PLANAR_R));
    const auto* source_a = source_has_alpha
                               ? reinterpret_cast<const Pixel*>(source->GetReadPtr(PLANAR_A) +
                                                                 source_y * source->GetPitch(PLANAR_A))
                               : nullptr;
    const auto* output_row = reinterpret_cast<const Pixel*>(output->GetReadPtr() + y * output->GetPitch());
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(output_row[x * (target_has_alpha ? 4 : 3) + 0], source_b[x])
          << "blue x=" << x << " y=" << y;
      EXPECT_EQ(output_row[x * (target_has_alpha ? 4 : 3) + 1], source_g[x])
          << "green x=" << x << " y=" << y;
      EXPECT_EQ(output_row[x * (target_has_alpha ? 4 : 3) + 2], source_r[x])
          << "red x=" << x << " y=" << y;
      if (target_has_alpha) {
        const Pixel expected_alpha = source_has_alpha ? source_a[x] : std::numeric_limits<Pixel>::max();
        EXPECT_EQ(output_row[x * 4 + 3], expected_alpha) << "alpha x=" << x << " y=" << y;
      }
    }
  }
}

template <typename Pixel>
void run_rgb_to_rgba_case(int source_pixel_type) {
  constexpr int width = 5;
  constexpr int height = 3;
  const auto source_vi = make_video_info(
      VideoInfoSpec{width, height, source_pixel_type, 1, 25, 1});
  AviSynthEnvironment environment;
  PVideoFrame source = environment.get()->NewVideoFrame(source_vi);
  fill_packed_rgb_source<Pixel>(source, 3);
  const auto source_before = FrameSnapshot::capture(source, source_vi);
  auto* source_impl = new StaticFrameClip(source_vi, source);
  const PClip clip(source_impl);

  RGBtoRGBA filter(clip);
  EXPECT_EQ(filter.GetVideoInfo().pixel_type,
            sizeof(Pixel) == 1 ? VideoInfo::CS_BGR32 : VideoInfo::CS_BGR64);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  expect_packed_conversion<Pixel>(output, source, 3, 4, false);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, source_vi), source_before);
}

template <typename Pixel>
void run_rgba_to_rgb_case(int source_pixel_type) {
  constexpr int width = 5;
  constexpr int height = 3;
  const auto source_vi = make_video_info(
      VideoInfoSpec{width, height, source_pixel_type, 1, 25, 1});
  AviSynthEnvironment environment;
  PVideoFrame source = environment.get()->NewVideoFrame(source_vi);
  fill_packed_rgb_source<Pixel>(source, 4);
  const auto source_before = FrameSnapshot::capture(source, source_vi);
  auto* source_impl = new StaticFrameClip(source_vi, source);
  const PClip clip(source_impl);

  RGBAtoRGB filter(clip);
  EXPECT_EQ(filter.GetVideoInfo().pixel_type,
            sizeof(Pixel) == 1 ? VideoInfo::CS_BGR24 : VideoInfo::CS_BGR48);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  expect_packed_conversion<Pixel>(output, source, 4, 3, false);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, source_vi), source_before);
}

template <typename Pixel>
void run_packed_to_planar_case(int source_pixel_type, bool source_has_alpha,
                               bool target_has_alpha, int target_pixel_type) {
  constexpr int width = 5;
  constexpr int height = 3;
  const auto source_vi = make_video_info(
      VideoInfoSpec{width, height, source_pixel_type, 1, 25, 1});
  AviSynthEnvironment environment;
  PVideoFrame source = environment.get()->NewVideoFrame(source_vi);
  fill_packed_rgb_source<Pixel>(source, source_has_alpha ? 4 : 3);
  const auto source_before = FrameSnapshot::capture(source, source_vi);
  auto* source_impl = new StaticFrameClip(source_vi, source);
  const PClip clip(source_impl);

  PackedRGBtoPlanarRGB filter(clip, source_has_alpha, target_has_alpha);
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, target_pixel_type);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  expect_packed_to_planar<Pixel>(source, output, source_has_alpha ? 4 : 3,
                                 source_has_alpha, target_has_alpha);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, source_vi), source_before);
}

template <typename Pixel>
void run_planar_to_packed_case(int source_pixel_type, bool source_has_alpha,
                               bool target_has_alpha, int target_pixel_type) {
  constexpr int width = 5;
  constexpr int height = 3;
  const auto source_vi = make_video_info(
      VideoInfoSpec{width, height, source_pixel_type, 1, 25, 1});
  AviSynthEnvironment environment;
  PVideoFrame source = environment.get()->NewVideoFrame(source_vi);
  fill_planar_rgb_source<Pixel>(source, source_has_alpha);
  const auto source_before = FrameSnapshot::capture(source, source_vi);
  auto* source_impl = new StaticFrameClip(source_vi, source);
  const PClip clip(source_impl);

  PlanarRGBtoPackedRGB filter(clip, target_has_alpha);
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, target_pixel_type);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  expect_planar_to_packed<Pixel>(source, output, source_has_alpha, target_has_alpha);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, source_vi), source_before);
}

TEST(ConvertRgbFilter, AddsOpaqueAlphaToBgr24) {
  run_rgb_to_rgba_case<std::uint8_t>(VideoInfo::CS_BGR24);
}

TEST(ConvertRgbFilter, AddsOpaqueAlphaToBgr48) {
  run_rgb_to_rgba_case<std::uint16_t>(VideoInfo::CS_BGR48);
}

TEST(ConvertRgbFilter, DropsAlphaFromBgr32) {
  run_rgba_to_rgb_case<std::uint8_t>(VideoInfo::CS_BGR32);
}

TEST(ConvertRgbFilter, DropsAlphaFromBgr64) {
  run_rgba_to_rgb_case<std::uint16_t>(VideoInfo::CS_BGR64);
}

TEST(ConvertRgbFilter, ConvertsBgr24ToRgbapWithOpaqueAlpha) {
  run_packed_to_planar_case<std::uint8_t>(VideoInfo::CS_BGR24, false, true, VideoInfo::CS_RGBAP);
}

TEST(ConvertRgbFilter, ConvertsBgr32ToRgbpWithoutAlpha) {
  run_packed_to_planar_case<std::uint8_t>(VideoInfo::CS_BGR32, true, false, VideoInfo::CS_RGBP);
}

TEST(ConvertRgbFilter, ConvertsBgr64ToRgbap16WithSourceAlpha) {
  run_packed_to_planar_case<std::uint16_t>(VideoInfo::CS_BGR64, true, true, VideoInfo::CS_RGBAP16);
}

TEST(ConvertRgbFilter, ConvertsRgbpToBgr32WithOpaqueAlpha) {
  run_planar_to_packed_case<std::uint8_t>(VideoInfo::CS_RGBP, false, true, VideoInfo::CS_BGR32);
}

TEST(ConvertRgbFilter, ConvertsRgbapToBgr24WithoutAlpha) {
  run_planar_to_packed_case<std::uint8_t>(VideoInfo::CS_RGBAP, true, false, VideoInfo::CS_BGR24);
}

TEST(ConvertRgbFilter, ConvertsRgbap16ToBgr48WithoutAlpha) {
  run_planar_to_packed_case<std::uint16_t>(VideoInfo::CS_RGBAP16, true, false, VideoInfo::CS_BGR48);
}

}  // namespace
