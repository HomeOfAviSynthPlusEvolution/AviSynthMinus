#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_RGBADJUST_UNDEF_AVS_UNUSED
#endif
#include "filters/levels.h"
#ifdef AVSUT_RGBADJUST_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_RGBADJUST_UNDEF_AVS_UNUSED
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

std::uint8_t adjust_channel(std::uint8_t value, double scale, double bias) {
  const double normalized =
      std::clamp((bias + static_cast<double>(value) * scale) / 255.0, 0.0, 1.0);
  return static_cast<std::uint8_t>(std::pow(normalized, 1.0) * 255.0 + 0.5);
}

template <typename Pixel>
Pixel adjust_sample(Pixel value, double scale, double bias, double gamma,
                    double maximum_value) {
  const double normalized =
      std::clamp((bias + static_cast<double>(value) * scale) / maximum_value, 0.0, 1.0);
  return static_cast<Pixel>(std::pow(normalized, 1.0 / gamma) * maximum_value + 0.5);
}

float adjust_float(float value, double scale, double bias, double gamma) {
  return static_cast<float>(std::pow(std::clamp(bias + static_cast<double>(value) * scale, 0.0,
                                                 1.0),
                                     1.0 / gamma));
}

TEST(RGBAdjustFilter, AppliesPackedChannelScaleBiasAndAlphaMapping) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 2;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR32, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xd1, DEFAULT_PLANE);
  const std::array<std::uint8_t, 8> blue{0, 5, 20, 64, 128, 200, 250, 255};
  const std::array<std::uint8_t, 8> green{3, 17, 31, 63, 127, 191, 239, 255};
  const std::array<std::uint8_t, 8> red{1, 16, 32, 80, 128, 180, 240, 255};
  const std::array<std::uint8_t, 8> alpha{0, 15, 64, 100, 128, 192, 240, 255};
  const int pitch = source->GetPitch();
  for (int y = 0; y < height; ++y) {
    auto* row = source->GetWritePtr() + y * pitch;
    for (int x = 0; x < width; ++x) {
      row[4 * x + 0] = blue[static_cast<std::size_t>(x)];
      row[4 * x + 1] = green[static_cast<std::size_t>(x)];
      row[4 * x + 2] = red[static_cast<std::size_t>(x)];
      row[4 * x + 3] = alpha[static_cast<std::size_t>(x)];
    }
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  RGBAdjust filter(clip, 0.5, 1.0, 1.0, 1.0, 8.0, 0.0, -6.0, 5.0, 1.0, 1.0, 1.0, 1.0, false, false,
                   false, "", environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* source_row = source->GetReadPtr() + y * source->GetPitch();
    const auto* output_row = output->GetReadPtr() + y * output->GetPitch();
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(output_row[4 * x + 0], adjust_channel(blue[static_cast<std::size_t>(x)], 1.0, -6.0))
          << "blue x=" << x << " y=" << y;
      EXPECT_EQ(output_row[4 * x + 1], adjust_channel(green[static_cast<std::size_t>(x)], 1.0, 0.0))
          << "green x=" << x << " y=" << y;
      EXPECT_EQ(output_row[4 * x + 2], adjust_channel(red[static_cast<std::size_t>(x)], 0.5, 8.0))
          << "red x=" << x << " y=" << y;
      EXPECT_EQ(output_row[4 * x + 3], adjust_channel(alpha[static_cast<std::size_t>(x)], 1.0, 5.0))
          << "alpha x=" << x << " y=" << y;
      EXPECT_EQ(source_row[4 * x + 0], blue[static_cast<std::size_t>(x)]);
      EXPECT_EQ(source_row[4 * x + 1], green[static_cast<std::size_t>(x)]);
      EXPECT_EQ(source_row[4 * x + 2], red[static_cast<std::size_t>(x)]);
      EXPECT_EQ(source_row[4 * x + 3], alpha[static_cast<std::size_t>(x)]);
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(RGBAdjustFilter, AppliesScaleBiasToPackedBgr24TailPixels) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xc1, DEFAULT_PLANE);
  constexpr std::array<std::array<std::uint8_t, 3>, 5> pixels{{
      {{0, 3, 1}}, {{5, 17, 16}}, {{20, 31, 32}}, {{200, 191, 180}}, {{255, 255, 255}}}};
  write_frame_plane<std::uint8_t>(source, DEFAULT_PLANE, [&](int byte, int y) {
    const auto& pixel = pixels[static_cast<std::size_t>(byte / 3)];
    return static_cast<std::uint8_t>(pixel[static_cast<std::size_t>(byte % 3)] + y * 2);
  });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  RGBAdjust filter(clip, 1.25, 0.75, 0.5, 1.0, 8.0, -5.0, 20.0, 0.0, 1.0, 1.0,
                   1.0, 1.0, false, false, false, "", environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* row = output->GetReadPtr() + y * output->GetPitch();
    for (int x = 0; x < width; ++x) {
      const auto& pixel = pixels[static_cast<std::size_t>(x)];
      const auto blue = static_cast<std::uint8_t>(pixel[0] + y * 2);
      const auto green = static_cast<std::uint8_t>(pixel[1] + y * 2);
      const auto red = static_cast<std::uint8_t>(pixel[2] + y * 2);
      EXPECT_EQ(row[x * 3 + 0], adjust_channel(blue, 0.5, 20.0))
          << "blue x=" << x << " y=" << y;
      EXPECT_EQ(row[x * 3 + 1], adjust_channel(green, 0.75, -5.0))
          << "green x=" << x << " y=" << y;
      EXPECT_EQ(row[x * 3 + 2], adjust_channel(red, 1.25, 8.0))
          << "red x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(RGBAdjustFilter, AppliesSixteenBitScaleBiasToPackedBgr64IncludingAlpha) {
  AviSynthEnvironment environment;
  constexpr int width = 3;
  constexpr int height = 2;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR64, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xd2, DEFAULT_PLANE);
  constexpr std::array<std::array<std::uint16_t, 4>, 3> pixels{{
      {{0, 3, 1, 0}}, {{1024, 16384, 32768, 49152}}, {{65535, 60000, 50000, 65535}}}};
  write_frame_plane<std::uint16_t>(source, DEFAULT_PLANE, [&](int component, int y) {
    const auto& pixel = pixels[static_cast<std::size_t>(component / 4)];
    return static_cast<std::uint16_t>(pixel[static_cast<std::size_t>(component % 4)] + y * 17);
  });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  RGBAdjust filter(clip, 0.8, 1.1, 0.6, 0.5, 2048.0, -1024.0, 4096.0, 8192.0, 1.0,
                   1.0, 1.0, 1.0, false, false, false, "", environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* row = reinterpret_cast<const std::uint16_t*>(output->GetReadPtr() +
                                                              y * output->GetPitch());
    for (int x = 0; x < width; ++x) {
      const auto& pixel = pixels[static_cast<std::size_t>(x)];
      const std::array<std::uint16_t, 4> expected{
          adjust_sample(static_cast<std::uint16_t>(pixel[0] + y * 17), 0.6, 4096.0, 1.0,
                        65535.0),
          adjust_sample(static_cast<std::uint16_t>(pixel[1] + y * 17), 1.1, -1024.0, 1.0,
                        65535.0),
          adjust_sample(static_cast<std::uint16_t>(pixel[2] + y * 17), 0.8, 2048.0, 1.0,
                        65535.0),
          adjust_sample(static_cast<std::uint16_t>(pixel[3] + y * 17), 0.5, 8192.0, 1.0,
                        65535.0)};
      for (int component = 0; component < 4; ++component) {
        EXPECT_EQ(row[x * 4 + component], expected[static_cast<std::size_t>(component)])
            << "component=" << component << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(RGBAdjustFilter, AppliesSixteenBitScaleBiasToPlanarRgbPlanes) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBP16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x71 + plane * 0x17), plane);
  }
  write_frame_plane<std::uint16_t>(source, PLANAR_B,
                                   [](int x, int y) { return 2000 + x * 3001 + y * 701; });
  write_frame_plane<std::uint16_t>(source, PLANAR_G,
                                   [](int x, int y) { return 4000 + x * 2101 + y * 503; });
  write_frame_plane<std::uint16_t>(source, PLANAR_R,
                                   [](int x, int y) { return 6000 + x * 1701 + y * 307; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  RGBAdjust filter(clip, 0.9, 1.2, 0.7, 1.0, 2048.0, -2048.0, 4096.0, 0.0, 1.0,
                   1.0, 1.0, 1.0, false, false, false, "", environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const auto source_b = read_frame_plane_active<std::uint16_t>(source, PLANAR_B);
  const auto source_g = read_frame_plane_active<std::uint16_t>(source, PLANAR_G);
  const auto source_r = read_frame_plane_active<std::uint16_t>(source, PLANAR_R);
  const auto expected_b = [&]() {
    std::vector<std::uint16_t> values;
    values.reserve(source_b.size());
    for (const auto value : source_b) values.push_back(adjust_sample(value, 0.7, 4096.0, 1.0, 65535.0));
    return values;
  }();
  const auto expected_g = [&]() {
    std::vector<std::uint16_t> values;
    values.reserve(source_g.size());
    for (const auto value : source_g) values.push_back(adjust_sample(value, 1.2, -2048.0, 1.0, 65535.0));
    return values;
  }();
  const auto expected_r = [&]() {
    std::vector<std::uint16_t> values;
    values.reserve(source_r.size());
    for (const auto value : source_r) values.push_back(adjust_sample(value, 0.9, 2048.0, 1.0, 65535.0));
    return values;
  }();
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, PLANAR_B), expected_b);
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, PLANAR_G), expected_g);
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, PLANAR_R), expected_r);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(RGBAdjustFilter, AppliesFloatScaleBiasGammaToPlanarRgbAlpha) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBAPS, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x31 + plane * 0x19), plane);
  }
  write_frame_plane<float>(source, PLANAR_B,
                           [](int x, int y) { return 0.05F + x * 0.07F + y * 0.03F; });
  write_frame_plane<float>(source, PLANAR_G,
                           [](int x, int y) { return 0.15F + x * 0.05F + y * 0.04F; });
  write_frame_plane<float>(source, PLANAR_R,
                           [](int x, int y) { return 0.25F + x * 0.04F + y * 0.02F; });
  write_frame_plane<float>(source, PLANAR_A,
                           [](int x, int y) { return 0.35F + x * 0.03F + y * 0.05F; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  RGBAdjust filter(clip, 1.1, 0.8, 0.6, 0.7, -0.1, 0.15, 0.05, 0.2, 1.3, 0.9,
                   2.0, 1.1, false, false, false, "", environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const auto check_plane = [&](int plane, double scale, double bias, double gamma) {
    const auto source_values = read_frame_plane_active<float>(source, plane);
    const auto output_values = read_frame_plane_active<float>(output, plane);
    ASSERT_EQ(output_values.size(), source_values.size());
    for (std::size_t index = 0; index < source_values.size(); ++index) {
      EXPECT_NEAR(output_values[index], adjust_float(source_values[index], scale, bias, gamma),
                  1.0e-6F)
          << "plane=" << plane << " active_index=" << index;
    }
  };
  check_plane(PLANAR_B, 0.6, 0.05, 2.0);
  check_plane(PLANAR_G, 0.8, 0.15, 0.9);
  check_plane(PLANAR_R, 1.1, -0.1, 1.3);
  check_plane(PLANAR_A, 0.7, 0.2, 1.1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(RGBAdjustFilter, AppliesConditionalScaleBiasAndGammaToFloatPlanarRgbAlpha) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBAPS, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x46 + plane * 0x13), plane);
  }
  write_frame_plane<float>(source, PLANAR_B,
                           [](int x, int y) { return 0.08F + x * 0.06F + y * 0.025F; });
  write_frame_plane<float>(source, PLANAR_G,
                           [](int x, int y) { return 0.18F + x * 0.045F + y * 0.035F; });
  write_frame_plane<float>(source, PLANAR_R,
                           [](int x, int y) { return 0.28F + x * 0.035F + y * 0.02F; });
  write_frame_plane<float>(source, PLANAR_A,
                           [](int x, int y) { return 0.38F + x * 0.025F + y * 0.03F; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  ASSERT_TRUE(environment.get()->SetVar("rgbadjust_r_f23_float", AVSValue(0.55)));
  ASSERT_TRUE(environment.get()->SetVar("rgbadjust_gb_f23_float", AVSValue(0.08)));
  ASSERT_TRUE(environment.get()->SetVar("rgbadjust_bg_f23_float", AVSValue(1.8)));
  ASSERT_TRUE(environment.get()->SetVar("rgbadjust_a_f23_float", AVSValue(0.65)));
  ASSERT_TRUE(environment.get()->SetVar("rgbadjust_ab_f23_float", AVSValue(0.1)));
  ASSERT_TRUE(environment.get()->SetVar("rgbadjust_ag_f23_float", AVSValue(0.75)));

  RGBAdjust filter(clip, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0,
                   false, false, true, "_f23_float", environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const auto check_plane = [&](int plane, double scale, double bias, double gamma) {
    const auto source_values = read_frame_plane_active<float>(source, plane);
    const auto output_values = read_frame_plane_active<float>(output, plane);
    ASSERT_EQ(output_values.size(), source_values.size());
    for (std::size_t index = 0; index < source_values.size(); ++index) {
      EXPECT_NEAR(output_values[index], adjust_float(source_values[index], scale, bias, gamma),
                  1.0e-6F)
          << "plane=" << plane << " active_index=" << index;
    }
  };
  check_plane(PLANAR_B, 1.0, 0.0, 1.8);
  check_plane(PLANAR_G, 1.0, 0.08, 1.0);
  check_plane(PLANAR_R, 0.55, 0.0, 1.0);
  check_plane(PLANAR_A, 0.65, 0.1, 0.75);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(RGBAdjustFilter, IgnoresOrderedDitherForFloatPlanarRgbAlpha) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBAPS, 1, 25, 1});
  PVideoFrame plain_source = environment.get()->NewVideoFrame(vi);
  PVideoFrame dither_source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A}) {
    fill_plane_full_pitch(plain_source, static_cast<std::uint8_t>(0x57 + plane * 0x11), plane);
    fill_plane_full_pitch(dither_source, static_cast<std::uint8_t>(0x57 + plane * 0x11), plane);
  }
  const auto write_values = [](PVideoFrame& frame) {
    write_frame_plane<float>(frame, PLANAR_B,
                             [](int x, int y) { return 0.04F + x * 0.08F + y * 0.02F; });
    write_frame_plane<float>(frame, PLANAR_G,
                             [](int x, int y) { return 0.14F + x * 0.06F + y * 0.03F; });
    write_frame_plane<float>(frame, PLANAR_R,
                             [](int x, int y) { return 0.24F + x * 0.05F + y * 0.025F; });
    write_frame_plane<float>(frame, PLANAR_A,
                             [](int x, int y) { return 0.34F + x * 0.04F + y * 0.035F; });
  };
  write_values(plain_source);
  write_values(dither_source);
  const auto plain_before = FrameSnapshot::capture(plain_source, vi);
  const auto dither_before = FrameSnapshot::capture(dither_source, vi);
  auto* plain_clip_impl = new StaticFrameClip(vi, plain_source);
  auto* dither_clip_impl = new StaticFrameClip(vi, dither_source);
  const PClip plain_clip(plain_clip_impl);
  const PClip dither_clip(dither_clip_impl);

  RGBAdjust plain_filter(plain_clip, 1.1, 0.8, 0.7, 0.6, -0.05, 0.1, 0.03, 0.2, 1.3, 0.9,
                         1.7, 0.75, false, false, false, "", environment.get());
  RGBAdjust dither_filter(dither_clip, 1.1, 0.8, 0.7, 0.6, -0.05, 0.1, 0.03, 0.2, 1.3, 0.9,
                          1.7, 0.75, false, true, false, "", environment.get());
  const PVideoFrame plain_output = plain_filter.GetFrame(0, environment.get());
  const PVideoFrame dither_output = dither_filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A}) {
    EXPECT_EQ(read_frame_plane_active<float>(dither_output, plane),
              read_frame_plane_active<float>(plain_output, plane))
        << "plane=" << plane;
  }
  EXPECT_NE(plain_output->CheckMemory(), 1);
  EXPECT_NE(dither_output->CheckMemory(), 1);
  EXPECT_EQ(plain_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(dither_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(plain_source, vi), plain_before);
  EXPECT_EQ(FrameSnapshot::capture(dither_source, vi), dither_before);
}

TEST(RGBAdjustFilter, AppliesOrderedDitherToEightBitPackedChannels) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR24, 1, 25, 1});
  PVideoFrame plain_source = environment.get()->NewVideoFrame(vi);
  PVideoFrame dither_source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(plain_source, 0x81, DEFAULT_PLANE);
  fill_plane_full_pitch(dither_source, 0x81, DEFAULT_PLANE);
  const auto value_at = [](int byte, int y) {
    const int component = byte % 3;
    const int x = byte / 3;
    return static_cast<std::uint8_t>((x * 31 + y * 47 + component * 59 + 3) & 0xff);
  };
  write_frame_plane<std::uint8_t>(plain_source, DEFAULT_PLANE, value_at);
  write_frame_plane<std::uint8_t>(dither_source, DEFAULT_PLANE, value_at);
  const auto plain_before = FrameSnapshot::capture(plain_source, vi);
  const auto dither_before = FrameSnapshot::capture(dither_source, vi);
  auto* plain_clip_impl = new StaticFrameClip(vi, plain_source);
  auto* dither_clip_impl = new StaticFrameClip(vi, dither_source);
  const PClip plain_clip(plain_clip_impl);
  const PClip dither_clip(dither_clip_impl);

  RGBAdjust plain_filter(plain_clip, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0,
                         1.0, 1.0, false, false, false, "", environment.get());
  RGBAdjust dither_filter(dither_clip, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0,
                          1.0, 1.0, false, true, false, "", environment.get());
  const PVideoFrame plain_output = plain_filter.GetFrame(0, environment.get());
  const PVideoFrame dither_output = dither_filter.GetFrame(0, environment.get());

  int changed_components = 0;
  for (int y = 0; y < height; ++y) {
    const auto* plain_row = plain_output->GetReadPtr() + y * plain_output->GetPitch();
    const auto* dither_row = dither_output->GetReadPtr() + y * dither_output->GetPitch();
    const auto* source_row = plain_source->GetReadPtr() + y * plain_source->GetPitch();
    for (int byte = 0; byte < vi.width * 3; ++byte) {
      EXPECT_EQ(plain_row[byte], source_row[byte]) << "byte=" << byte << " y=" << y;
      EXPECT_LE(std::abs(static_cast<int>(dither_row[byte]) - static_cast<int>(plain_row[byte])), 1)
          << "byte=" << byte << " y=" << y;
      changed_components += dither_row[byte] != plain_row[byte] ? 1 : 0;
    }
  }
  EXPECT_GT(changed_components, 0);
  EXPECT_NE(plain_output->CheckMemory(), 1);
  EXPECT_NE(dither_output->CheckMemory(), 1);
  EXPECT_EQ(plain_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(dither_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(plain_source, vi), plain_before);
  EXPECT_EQ(FrameSnapshot::capture(dither_source, vi), dither_before);
}

TEST(RGBAdjustFilter, ReadsConditionalScaleBiasAndGammaVariables) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 2;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x92, DEFAULT_PLANE);
  write_frame_plane<std::uint8_t>(source, DEFAULT_PLANE, [](int byte, int y) {
    const int component = byte % 3;
    const int x = byte / 3;
    return static_cast<std::uint8_t>(17 + x * 29 + y * 13 + component * 7);
  });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  ASSERT_TRUE(environment.get()->SetVar("rgbadjust_r_f23", AVSValue(0.5)));
  ASSERT_TRUE(environment.get()->SetVar("rgbadjust_gb_f23", AVSValue(10.0)));
  ASSERT_TRUE(environment.get()->SetVar("rgbadjust_bg_f23", AVSValue(2.0)));

  RGBAdjust filter(clip, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0,
                   false, false, true, "_f23", environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* source_row = source->GetReadPtr() + y * source->GetPitch();
    const auto* output_row = output->GetReadPtr() + y * output->GetPitch();
    for (int x = 0; x < width; ++x) {
      const auto blue = source_row[x * 3 + 0];
      const auto green = source_row[x * 3 + 1];
      const auto red = source_row[x * 3 + 2];
      EXPECT_EQ(output_row[x * 3 + 0], adjust_sample(blue, 1.0, 0.0, 2.0, 255.0))
          << "blue x=" << x << " y=" << y;
      EXPECT_EQ(output_row[x * 3 + 1], adjust_channel(green, 1.0, 10.0))
          << "green x=" << x << " y=" << y;
      EXPECT_EQ(output_row[x * 3 + 2], adjust_channel(red, 0.5, 0.0))
          << "red x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(RGBAdjustFilter, LeavesConditionalVariablesUnusedWhenDisabled) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 2;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa7, DEFAULT_PLANE);
  write_frame_plane<std::uint8_t>(source, DEFAULT_PLANE, [](int byte, int y) {
    const int component = byte % 3;
    const int x = byte / 3;
    return static_cast<std::uint8_t>(20 + x * 31 + y * 11 + component * 9);
  });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  ASSERT_TRUE(environment.get()->SetVar("rgbadjust_r_f23_disabled", AVSValue(0.5)));

  RGBAdjust filter(clip, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0,
                   false, false, false, "_f23_disabled", environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* source_row = source->GetReadPtr() + y * source->GetPitch();
    const auto* output_row = output->GetReadPtr() + y * output->GetPitch();
    for (int byte = 0; byte < vi.width * 3; ++byte) {
      EXPECT_EQ(output_row[byte], source_row[byte]) << "byte=" << byte << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(RGBAdjustFilter, AnalyzeOverlaysStatisticsOnPackedOutput) {
  AviSynthEnvironment environment;
  constexpr int width = 320;
  constexpr int height = 120;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x63, DEFAULT_PLANE);
  write_frame_plane<std::uint8_t>(source, DEFAULT_PLANE, [](int byte, int y) {
    const int component = byte % 3;
    const int x = byte / 3;
    return static_cast<std::uint8_t>((x * 11 + y * 17 + component * 43) & 0xff);
  });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  RGBAdjust filter(clip, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0,
                   true, false, false, "", environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  std::size_t changed_components = 0;
  for (int y = 0; y < height; ++y) {
    const auto* source_row = source->GetReadPtr() + y * source->GetPitch();
    const auto* output_row = output->GetReadPtr() + y * output->GetPitch();
    for (int byte = 0; byte < vi.width * 3; ++byte) {
      changed_components += output_row[byte] != source_row[byte] ? 1U : 0U;
    }
  }
  EXPECT_GT(changed_components, 0U);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(RGBAdjustFilter, AnalyzeOverlaysStatisticsOnFloatPlanarOutput) {
  AviSynthEnvironment environment;
  constexpr int width = 320;
  constexpr int height = 120;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBPS, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x6e + plane * 0x0d), plane);
  }
  write_frame_plane<float>(source, PLANAR_B, [](int x, int y) {
    return static_cast<float>((x * 17 + y * 13 + 5) % 257) / 256.0F;
  });
  write_frame_plane<float>(source, PLANAR_G, [](int x, int y) {
    return static_cast<float>((x * 7 + y * 19 + 31) % 257) / 256.0F;
  });
  write_frame_plane<float>(source, PLANAR_R, [](int x, int y) {
    return static_cast<float>((x * 23 + y * 3 + 47) % 257) / 256.0F;
  });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  RGBAdjust filter(clip, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0,
                   true, false, false, "", environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  std::size_t changed_samples = 0;
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    const auto source_values = read_frame_plane_active<float>(source, plane);
    const auto output_values = read_frame_plane_active<float>(output, plane);
    ASSERT_EQ(output_values.size(), source_values.size());
    for (std::size_t index = 0; index < source_values.size(); ++index) {
      ASSERT_TRUE(std::isfinite(output_values[index]))
          << "plane=" << plane << " active_index=" << index;
      changed_samples += output_values[index] != source_values[index] ? 1U : 0U;
    }
  }
  EXPECT_GT(changed_samples, 0U);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

}  // namespace
