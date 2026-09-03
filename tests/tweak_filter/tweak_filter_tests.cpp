#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_TWEAK_UNDEF_AVS_UNUSED
#endif
#include "filters/levels.h"
#ifdef AVSUT_TWEAK_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_TWEAK_UNDEF_AVS_UNUSED
#endif

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSnapshot;
using avsut::test::make_video_info;
using avsut::test::read_frame_plane_active;
using avsut::test::StaticFrameClip;
using avsut::test::VideoInfoSpec;
using avsut::test::write_frame_plane;

template <std::size_t N>
void write_values(PVideoFrame& frame, int plane, const std::array<std::uint8_t, N>& values) {
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

std::array<std::uint8_t, 2> expected_conditional_chroma(std::uint8_t source_u,
                                                          std::uint8_t source_v) {
  constexpr double pi = 3.14159265358979323846;
  constexpr double start_hue = 329.0;
  constexpr double end_hue = 31.0;
  constexpr double max_sat = 1.19 * 100.0;
  constexpr double min_sat = 1.19 * 40.0;
  constexpr int middle_chroma = 128;
  constexpr int sat = static_cast<int>(1.5 * 512.0);
  const int u = static_cast<int>(source_u) - middle_chroma;
  const int v = static_cast<int>(source_v) - middle_chroma;

  double hue = std::atan2(static_cast<double>(v), static_cast<double>(u)) * 180.0 / pi;
  if (hue < 0.0) {
    hue += 360.0;
  }
  const bool hue_selected = start_hue < end_hue
                                ? hue >= start_hue && hue <= end_hue
                                : !(hue < start_hue && hue > end_hue);
  const double squared_sat = static_cast<double>(u * u + v * v);
  const bool sat_selected = min_sat * min_sat <= squared_sat && squared_sat <= max_sat * max_sat;
  if (!hue_selected || !sat_selected) {
    return {source_u, source_v};
  }

  const double hue_radians = 90.0 * pi / 180.0;
  const int mapped_u = static_cast<int>((u * std::cos(hue_radians) + v * std::sin(hue_radians)) * sat) >> 9;
  const int mapped_v = static_cast<int>((v * std::cos(hue_radians) - u * std::sin(hue_radians)) * sat) >> 9;
  return {static_cast<std::uint8_t>(std::clamp(mapped_u + middle_chroma, 0, 255)),
          static_cast<std::uint8_t>(std::clamp(mapped_v + middle_chroma, 0, 255))};
}

float expected_float_luma_dither(float source, int x, int y, bool coring) {
  constexpr std::array<std::uint8_t, 10> dither_map{
      0x00, 0xb0, 0x60, 0xd0, 0x0b, 0xc0, 0x70, 0x90, 0x20, 0xcb};
  constexpr float dither_scale = 1.0F / 65536.0F;
  const float ordered_dither =
      (static_cast<float>(dither_map[static_cast<std::size_t>(y * 5 + x)]) - 127.5F) / 256.0F *
      dither_scale;
  const float min_y = coring ? 16.0F / 256.0F : 0.0F;
  const float max_y = coring ? 235.0F / 256.0F : 1.0F;
  return std::clamp(min_y + (source - min_y + ordered_dither), min_y, max_y);
}

float expected_float_chroma_dither(float source, int x, int y, bool coring) {
  constexpr std::array<std::uint8_t, 16> dither_map4{
      0x0, 0xb, 0x6, 0xd, 0xc, 0x7, 0x9, 0x2, 0x3, 0x8, 0x5, 0xe, 0xf, 0x4, 0xa, 0x1};
  constexpr float dither_scale = 1.0F / 65536.0F;
  const auto index = static_cast<std::size_t>(((y << 2) & 0xc) | (x & 0x3));
  const float ordered_dither =
      (static_cast<float>(dither_map4[index]) - 7.5F) / 16.0F * dither_scale;
  const float min_uv = coring ? (16.0F - 128.0F) / 255.0F : -0.5F;
  const float max_uv = coring ? (240.0F - 128.0F) / 255.0F : 0.5F;
  return std::clamp(source + ordered_dither, min_uv, max_uv);
}

std::array<float, 2> expected_float_conditional_chroma(float source_u, float source_v) {
  constexpr double pi = 3.14159265358979323846;
  constexpr double start_hue = 329.0;
  constexpr double end_hue = 31.0;
  constexpr double max_sat = 1.19 * 100.0;
  constexpr double min_sat = 1.19 * 40.0;
  constexpr double saturation = 1.5;
  const double u = static_cast<double>(source_u) * 255.0;
  const double v = static_cast<double>(source_v) * 255.0;
  double hue = std::atan2(v, u) * 180.0 / pi;
  if (hue < 0.0) {
    hue += 360.0;
  }
  const bool hue_selected =
      !(hue < start_hue && hue > end_hue);
  const double squared_sat = u * u + v * v;
  const bool sat_selected = min_sat * min_sat <= squared_sat &&
                            squared_sat <= max_sat * max_sat;
  if (!hue_selected || !sat_selected) {
    return {std::clamp(source_u, -0.5F, 0.5F), std::clamp(source_v, -0.5F, 0.5F)};
  }

  constexpr double hue_radians = 90.0 * pi / 180.0;
  const float mapped_u = static_cast<float>(
      (source_u * std::cos(hue_radians) + source_v * std::sin(hue_radians)) * saturation);
  const float mapped_v = static_cast<float>(
      (source_v * std::cos(hue_radians) - source_u * std::sin(hue_radians)) * saturation);
  return {std::clamp(mapped_u, -0.5F, 0.5F), std::clamp(mapped_v, -0.5F, 0.5F)};
}

std::uint16_t expected_ten_bit_luma(std::uint16_t source, bool realcalc) {
  constexpr int min_y = 64;
  constexpr int max_y = 940;
  if (realcalc) {
    const float mapped = static_cast<float>(min_y) +
                         (static_cast<float>(source) - static_cast<float>(min_y)) * 1.25F +
                         12.0F;
    return static_cast<std::uint16_t>(std::clamp(static_cast<int>(mapped), min_y, max_y));
  }
  const int mapped = static_cast<int>((static_cast<double>(source) - min_y) * 1.25 + 12.0 +
                                      min_y + 0.5);
  return static_cast<std::uint16_t>(std::clamp(mapped, min_y, max_y));
}

std::uint16_t expected_ten_bit_chroma(std::uint16_t source, bool realcalc) {
  constexpr int middle = 512;
  constexpr int min_uv = 64;
  constexpr int max_uv = 960;
  if (realcalc) {
    const float centered = (static_cast<float>(source) - static_cast<float>(middle)) / 1024.0F;
    const float mapped = (centered * 1.25F + 0.5F) * 1024.0F;
    return static_cast<std::uint16_t>(
        std::clamp(static_cast<int>(mapped), min_uv, max_uv));
  }
  const int centered = static_cast<int>(source) - middle;
  const int delta = static_cast<int>(static_cast<double>(centered) * 640.0) >> 9;
  return static_cast<std::uint16_t>(std::clamp(delta + middle, min_uv, max_uv));
}

TEST(TweakFilter, AppliesEightBitLumaBrightnessWithoutTouchingSource) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa7, PLANAR_Y);
  const std::array<std::uint8_t, 8> values{0, 1, 16, 64, 128, 200, 243, 255};
  write_values(source, PLANAR_Y, values);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Tweak filter(clip, 0.0, 1.0, 12.0, 1.0, false, 0.0, 360.0, 150.0, 0.0, 0.0, false, true, 1.0,
               environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < vi.height; ++y) {
    const auto* source_row = source->GetReadPtr(PLANAR_Y) + y * source->GetPitch(PLANAR_Y);
    const auto* output_row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < vi.width; ++x) {
      const int expected = std::clamp(static_cast<int>(source_row[x]) + 12, 0, 255);
      EXPECT_EQ(output_row[x], expected) << "x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

class NeutralTweakTest : public ::testing::TestWithParam<bool> {};

TEST_P(NeutralTweakTest, PreservesAllYuv444ActivePlanes) {
  const bool realcalc = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 2;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x61, PLANAR_Y);
  fill_plane_full_pitch(source, 0x72, PLANAR_U);
  fill_plane_full_pitch(source, 0x83, PLANAR_V);
  const std::array<std::uint8_t, 8> y_values{0, 1, 16, 64, 128, 200, 235, 255};
  const std::array<std::uint8_t, 8> u_values{0, 17, 64, 96, 128, 160, 224, 255};
  const std::array<std::uint8_t, 8> v_values{255, 211, 173, 128, 97, 53, 19, 0};
  write_values(source, PLANAR_Y, y_values);
  write_values(source, PLANAR_U, u_values);
  write_values(source, PLANAR_V, v_values);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Tweak filter(clip, 0.0, 1.0, 0.0, 1.0, false, 0.0, 360.0, 150.0, 0.0, 0.0, false,
               realcalc, 1.0, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, plane),
              read_frame_plane_active<std::uint8_t>(source, plane))
        << "realcalc=" << realcalc << " plane=" << plane;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(
    Implementation, NeutralTweakTest, ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<bool>& info) {
      return info.param ? "VariantRealcalc" : "VariantLut";
    });

TEST(TweakFilter, AppliesRealCalcToSixteenBitYuv420LumaAndChroma) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUV420P16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x31 + plane * 0x19), plane);
    write_frame_plane<std::uint16_t>(source, plane, [plane](int x, int y) {
      return plane == PLANAR_Y ? 5000 + x * 7000 + y * 1000 : 28000 + x * 1400 + y * 800;
    });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Tweak filter(clip, 0.0, 1.0, 2.0, 1.25, false, 0.0, 360.0, 150.0, 0.0, 0.0, false,
               true, 1.0, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const int y_width = output->GetRowSize(PLANAR_Y) / static_cast<int>(sizeof(std::uint16_t));
  const int y_height = output->GetHeight(PLANAR_Y);
  for (int y = 0; y < y_height; ++y) {
    const auto* source_row = reinterpret_cast<const std::uint16_t*>(
        source->GetReadPtr(PLANAR_Y) + y * source->GetPitch(PLANAR_Y));
    const auto* output_row = reinterpret_cast<const std::uint16_t*>(
        output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y));
    for (int x = 0; x < y_width; ++x) {
      const auto expected = static_cast<std::uint16_t>(std::clamp(
          static_cast<int>(source_row[x] * 1.25 + 512.0), 0, 65535));
      EXPECT_EQ(output_row[x], expected) << "plane=Y x=" << x << " y=" << y;
    }
  }
  for (const int plane : {PLANAR_U, PLANAR_V}) {
    const int plane_width = output->GetRowSize(plane) / static_cast<int>(sizeof(std::uint16_t));
    const int plane_height = output->GetHeight(plane);
    for (int y = 0; y < plane_height; ++y) {
      const auto* source_row = reinterpret_cast<const std::uint16_t*>(
          source->GetReadPtr(plane) + y * source->GetPitch(plane));
      const auto* output_row = reinterpret_cast<const std::uint16_t*>(
          output->GetReadPtr(plane) + y * output->GetPitch(plane));
      for (int x = 0; x < plane_width; ++x) {
        EXPECT_EQ(output_row[x], source_row[x]) << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

class TenBitTweakVariantTest : public ::testing::TestWithParam<bool> {};

TEST_P(TenBitTweakVariantTest, MapsCoringLumaAndChromaAcrossImplementationVariants) {
  const bool realcalc = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 2;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUV444P10, 1, 25, 1});
  constexpr std::array<std::uint16_t, width> luma_values{0, 64, 256, 512, 768, 940, 1023};
  constexpr std::array<std::uint16_t, width> u_values{0, 64, 256, 512, 768, 960, 1023};
  constexpr std::array<std::uint16_t, width> v_values{1023, 960, 768, 512, 256, 64, 0};
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x3a, PLANAR_Y);
  fill_plane_full_pitch(source, 0x4b, PLANAR_U);
  fill_plane_full_pitch(source, 0x5c, PLANAR_V);
  write_frame_plane<std::uint16_t>(source, PLANAR_Y,
                                   [&luma_values](int x, int) {
                                     return luma_values[static_cast<std::size_t>(x)];
                                   });
  write_frame_plane<std::uint16_t>(source, PLANAR_U,
                                   [&u_values](int x, int) {
                                     return u_values[static_cast<std::size_t>(x)];
                                   });
  write_frame_plane<std::uint16_t>(source, PLANAR_V,
                                   [&v_values](int x, int) {
                                     return v_values[static_cast<std::size_t>(x)];
                                   });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Tweak filter(clip, 0.0, 1.25, 3.0, 1.25, true, 0.0, 360.0, 150.0, 0.0, 0.0, false,
               realcalc, 1.0, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* output_y = reinterpret_cast<const std::uint16_t*>(
        output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y));
    const auto* output_u = reinterpret_cast<const std::uint16_t*>(
        output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U));
    const auto* output_v = reinterpret_cast<const std::uint16_t*>(
        output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V));
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(x);
      EXPECT_EQ(output_y[x], expected_ten_bit_luma(luma_values[index], realcalc))
          << "realcalc=" << realcalc << " plane=Y x=" << x << " y=" << y;
      EXPECT_EQ(output_u[x], expected_ten_bit_chroma(u_values[index], realcalc))
          << "realcalc=" << realcalc << " plane=U x=" << x << " y=" << y;
      EXPECT_EQ(output_v[x], expected_ten_bit_chroma(v_values[index], realcalc))
          << "realcalc=" << realcalc << " plane=V x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(
    Implementation, TenBitTweakVariantTest, ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<bool>& info) {
      return info.param ? "VariantRealcalc" : "VariantLut";
    });

TEST(TweakFilter, KeepsSixteenBitDitherWithinOneEightBitStep) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUV420P16, 1, 25, 1});
  PVideoFrame plain_source = environment.get()->NewVideoFrame(vi);
  PVideoFrame dither_source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(plain_source, static_cast<std::uint8_t>(0x71 + plane * 0x13), plane);
    fill_plane_full_pitch(dither_source, static_cast<std::uint8_t>(0x71 + plane * 0x13), plane);
    const auto values = [plane](int x, int y) {
      return plane == PLANAR_Y ? 10000 + x * 997 + y * 503 : 30000 + x * 401 + y * 211;
    };
    write_frame_plane<std::uint16_t>(plain_source, plane, values);
    write_frame_plane<std::uint16_t>(dither_source, plane, values);
  }
  const auto plain_before = FrameSnapshot::capture(plain_source, vi);
  const auto dither_before = FrameSnapshot::capture(dither_source, vi);
  auto* plain_clip_impl = new StaticFrameClip(vi, plain_source);
  auto* dither_clip_impl = new StaticFrameClip(vi, dither_source);
  const PClip plain_clip(plain_clip_impl);
  const PClip dither_clip(dither_clip_impl);

  Tweak plain_filter(plain_clip, 0.0, 1.0, 0.0, 1.0, false, 0.0, 360.0, 150.0, 0.0, 0.0,
                     false, true, 256.0, environment.get());
  Tweak dither_filter(dither_clip, 0.0, 1.0, 0.0, 1.0, false, 0.0, 360.0, 150.0, 0.0, 0.0,
                      true, true, 256.0, environment.get());
  const PVideoFrame plain_output = plain_filter.GetFrame(0, environment.get());
  const PVideoFrame dither_output = dither_filter.GetFrame(0, environment.get());

  int changed_luma_samples = 0;
  const int plane_width = vi.width;
  const int plane_height = vi.height;
  for (int y = 0; y < plane_height; ++y) {
    const auto* plain_row = reinterpret_cast<const std::uint16_t*>(
        plain_output->GetReadPtr(PLANAR_Y) + y * plain_output->GetPitch(PLANAR_Y));
    const auto* dither_row = reinterpret_cast<const std::uint16_t*>(
        dither_output->GetReadPtr(PLANAR_Y) + y * dither_output->GetPitch(PLANAR_Y));
    for (int x = 0; x < plane_width; ++x) {
      const int delta = std::abs(static_cast<int>(dither_row[x]) - static_cast<int>(plain_row[x]));
      EXPECT_LE(delta, 128) << "x=" << x << " y=" << y;
      changed_luma_samples += dither_row[x] != plain_row[x] ? 1 : 0;
    }
  }
  EXPECT_GT(changed_luma_samples, 0);
  EXPECT_NE(plain_output->CheckMemory(), 1);
  EXPECT_NE(dither_output->CheckMemory(), 1);
  EXPECT_EQ(plain_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(dither_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(plain_source, vi), plain_before);
  EXPECT_EQ(FrameSnapshot::capture(dither_source, vi), dither_before);
}

class FloatDitherTweakTest : public ::testing::TestWithParam<bool> {};

TEST_P(FloatDitherTweakTest, AppliesOrderedDitherWithRangeClipping) {
  const bool coring = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 2;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUV444PS, 1, 25, 1});
  const std::array<float, width> luma{-0.1F, 0.0F, 0.25F, 0.75F, 1.1F};
  const std::array<float, width> u{-0.6F, -0.25F, 0.0F, 0.25F, 0.6F};
  const std::array<float, width> v{0.6F, 0.25F, 0.0F, -0.25F, -0.6F};
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x4d, PLANAR_Y);
  fill_plane_full_pitch(source, 0x5e, PLANAR_U);
  fill_plane_full_pitch(source, 0x6f, PLANAR_V);
  write_frame_plane<float>(source, PLANAR_Y,
                           [&luma](int x, int) { return luma[static_cast<std::size_t>(x)]; });
  write_frame_plane<float>(source, PLANAR_U,
                           [&u](int x, int) { return u[static_cast<std::size_t>(x)]; });
  write_frame_plane<float>(source, PLANAR_V,
                           [&v](int x, int) { return v[static_cast<std::size_t>(x)]; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Tweak filter(clip, 0.0, 1.0, 0.0, 1.0, coring, 0.0, 360.0, 150.0, 0.0, 0.0, true,
               false, 1.0, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* output_y = reinterpret_cast<const float*>(
        output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y));
    const auto* output_u = reinterpret_cast<const float*>(
        output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U));
    const auto* output_v = reinterpret_cast<const float*>(
        output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V));
    for (int x = 0; x < width; ++x) {
      EXPECT_NEAR(output_y[x], expected_float_luma_dither(luma[static_cast<std::size_t>(x)], x,
                                                           y, coring),
                  2.0e-6F)
          << "coring=" << coring << " plane=Y x=" << x << " y=" << y;
      EXPECT_NEAR(output_u[x], expected_float_chroma_dither(u[static_cast<std::size_t>(x)], x,
                                                             y, coring),
                  2.0e-6F)
          << "coring=" << coring << " plane=U x=" << x << " y=" << y;
      EXPECT_NEAR(output_v[x], expected_float_chroma_dither(v[static_cast<std::size_t>(x)], x,
                                                             y, coring),
                  2.0e-6F)
          << "coring=" << coring << " plane=V x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(
    Range, FloatDitherTweakTest, ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<bool>& info) {
      return info.param ? "Coring" : "FullRange";
    });

TEST(TweakFilter, SelectsConditionalFloatHueAndSaturationWithWrapping) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 2;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUV444PS, 1, 25, 1});
  const std::array<float, width> luma{0.0F, 0.1F, 0.3F, 0.5F, 0.7F, 0.9F, 1.0F};
  constexpr std::array<std::uint8_t, width> u_values{192, 183, 183, 173, 173, 152, 254};
  constexpr std::array<std::uint8_t, width> v_values{128, 160, 96, 173, 83, 128, 128};
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x81, PLANAR_Y);
  fill_plane_full_pitch(source, 0x92, PLANAR_U);
  fill_plane_full_pitch(source, 0xa3, PLANAR_V);
  write_frame_plane<float>(source, PLANAR_Y, [&luma](int x, int) {
    return luma[static_cast<std::size_t>(x)];
  });
  write_frame_plane<float>(source, PLANAR_U, [&u_values](int x, int) {
    return (static_cast<float>(u_values[static_cast<std::size_t>(x)]) - 128.0F) / 255.0F;
  });
  write_frame_plane<float>(source, PLANAR_V, [&v_values](int x, int) {
    return (static_cast<float>(v_values[static_cast<std::size_t>(x)]) - 128.0F) / 255.0F;
  });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Tweak filter(clip, 90.0, 1.5, 0.0, 1.0, false, 329.0, 31.0, 100.0, 40.0, 0.0, false,
               false, 1.0, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* output_y = reinterpret_cast<const float*>(
        output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y));
    const auto* output_u = reinterpret_cast<const float*>(
        output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U));
    const auto* output_v = reinterpret_cast<const float*>(
        output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V));
    for (int x = 0; x < width; ++x) {
      const auto expected = expected_float_conditional_chroma(
          (static_cast<float>(u_values[static_cast<std::size_t>(x)]) - 128.0F) / 255.0F,
          (static_cast<float>(v_values[static_cast<std::size_t>(x)]) - 128.0F) / 255.0F);
      EXPECT_NEAR(output_u[x], expected[0], 1.0e-6F) << "plane=U x=" << x << " y=" << y;
      EXPECT_NEAR(output_v[x], expected[1], 1.0e-6F) << "plane=V x=" << x << " y=" << y;
      EXPECT_NEAR(output_y[x], luma[static_cast<std::size_t>(x)], 1.0e-6F)
          << "plane=Y x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(TweakFilter, AppliesFloatHueSaturationAndBrightnessWithRealCalc) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 2;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUV444PS, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  const std::array<float, 5> luma{0.0F, 0.1F, 0.5F, 0.9F, 1.0F};
  const std::array<float, 5> u{-0.4F, -0.2F, 0.0F, 0.2F, 0.4F};
  const std::array<float, 5> v{0.3F, 0.1F, -0.1F, -0.3F, 0.45F};
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x57 + plane * 0x13), plane);
    write_frame_plane<float>(source, plane, [plane, &luma, &u, &v](int x, int) {
      const auto index = static_cast<std::size_t>(x);
      return plane == PLANAR_Y ? luma[index] : plane == PLANAR_U ? u[index] : v[index];
    });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  constexpr double hue_radians = 30.0 * 3.14159265358979323846 / 180.0;
  constexpr double saturation = 1.25;
  Tweak filter(clip, 30.0, saturation, 25.6, 1.0, false, 0.0, 360.0, 150.0, 0.0, 0.0,
               false, true, 1.0, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int x = 0; x < width; ++x) {
    const float expected_y = std::clamp(luma[static_cast<std::size_t>(x)] + 0.1F, 0.0F, 1.0F);
    const double expected_u = std::clamp(
        (u[static_cast<std::size_t>(x)] * std::cos(hue_radians) +
         v[static_cast<std::size_t>(x)] * std::sin(hue_radians)) * saturation,
        -0.5, 0.5);
    const double expected_v = std::clamp(
        (v[static_cast<std::size_t>(x)] * std::cos(hue_radians) -
         u[static_cast<std::size_t>(x)] * std::sin(hue_radians)) * saturation,
        -0.5, 0.5);
    const auto* output_y = reinterpret_cast<const float*>(output->GetReadPtr(PLANAR_Y));
    const auto* output_u = reinterpret_cast<const float*>(output->GetReadPtr(PLANAR_U));
    const auto* output_v = reinterpret_cast<const float*>(output->GetReadPtr(PLANAR_V));
    EXPECT_NEAR(output_y[x], expected_y, 1.0e-5F) << "x=" << x;
    EXPECT_NEAR(output_u[x], expected_u, 1.0e-5F) << "U x=" << x;
    EXPECT_NEAR(output_v[x], expected_v, 1.0e-5F) << "V x=" << x;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(TweakFilter, SelectsConditionalHueAndSaturationRangesWithWrapping) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x91, PLANAR_Y);
  fill_plane_full_pitch(source, 0xa2, PLANAR_U);
  fill_plane_full_pitch(source, 0xb3, PLANAR_V);
  constexpr std::array<std::uint8_t, width> u_values{192, 183, 183, 173, 173, 152, 254};
  constexpr std::array<std::uint8_t, width> v_values{128, 160, 96, 173, 83, 128, 128};
  constexpr std::array<std::uint8_t, width> y_values{16, 48, 96, 128, 192, 224, 235};
  write_values(source, PLANAR_Y, y_values);
  write_values(source, PLANAR_U, u_values);
  write_values(source, PLANAR_V, v_values);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Tweak filter(clip, 90.0, 1.5, 0.0, 1.0, false, 329.0, 31.0, 100.0, 40.0, 0.0, false,
               false, 1.0, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < width; ++x) {
      const auto expected = expected_conditional_chroma(u_values[static_cast<std::size_t>(x)],
                                                         v_values[static_cast<std::size_t>(x)]);
      EXPECT_EQ(output_u[x], expected[0]) << "U x=" << x << " y=" << y;
      EXPECT_EQ(output_v[x], expected[1]) << "V x=" << x << " y=" << y;
      EXPECT_EQ(output_y[x], y_values[static_cast<std::size_t>(x)])
          << "Y x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

}  // namespace
