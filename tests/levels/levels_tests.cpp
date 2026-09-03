#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_LEVELS_UNDEF_AVS_UNUSED
#endif
#include "filters/levels.h"
#ifdef AVSUT_LEVELS_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_LEVELS_UNDEF_AVS_UNUSED
#endif

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
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

TEST(Levels, MapsEightBitLumaWithClamping) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa7, PLANAR_Y);
  write_values<std::uint8_t>(source, PLANAR_Y, vi.width, vi.height,
                             std::array<std::uint8_t, 8>{0, 16, 32, 64, 128, 200, 235, 255});
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Levels filter(clip, 16.0f, 1.0, 235.0f, 0.0f, 255.0f, false, false, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetRowSize(PLANAR_Y), vi.width);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), vi.height);
  for (int y = 0; y < vi.height; ++y) {
    const auto* output_row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* source_row = source->GetReadPtr(PLANAR_Y) + y * source->GetPitch(PLANAR_Y);
    for (int x = 0; x < vi.width; ++x) {
      const int input = source_row[x];
      const int expected = std::clamp(static_cast<int>((input - 16) * 255.0 / 219.0 + 0.5), 0, 255);
      EXPECT_EQ(output_row[x], expected) << "x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Levels, MapsLumaAndChromaIndependentlyForYuv444) {
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

  Levels filter(clip, 16.0f, 1.0, 235.0f, 0.0f, 255.0f, true, false, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int pitch = output->GetPitch(plane);
    const int source_pitch = source->GetPitch(plane);
    const auto* output_base = output->GetReadPtr(plane);
    const auto* source_base = source->GetReadPtr(plane);
    for (int y = 0; y < vi.height; ++y) {
      const auto* output_row = output_base + y * pitch;
      const auto* source_row = source_base + y * source_pitch;
      for (int x = 0; x < vi.width; ++x) {
        const int input = source_row[x];
        const int expected =
            plane == PLANAR_Y
                ? std::clamp(static_cast<int>((input - 16) * 255.0 / 219.0 + 0.5), 16, 235)
                : std::clamp(static_cast<int>((input - 128) * 255.0 / 219.0 + 128.5), 16, 240);
        EXPECT_EQ(output_row[x], expected) << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Levels, MapsSixteenBitYuv420WithIndependentLumaAndChromaRanges) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUV420P16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x70 + plane * 0x13), plane);
    write_frame_plane<std::uint16_t>(source, plane, [](int x, int y) {
      return 2048 + x * 8192 + y * 4096;
    });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Levels filter(clip, 8192.0f, 1.0, 57344.0f, 4096.0f, 61440.0f, false, false,
                environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const auto expected_luma = [](std::uint16_t value) {
    const double normalized = std::clamp((static_cast<double>(value) - 8192.0) / 49152.0,
                                         0.0, 1.0);
    const double mapped = normalized * 57344.0 + 4096.0;
    return static_cast<std::uint16_t>(static_cast<int>(mapped + 0.5));
  };
  const auto expected_chroma = [](std::uint16_t value) {
    const double mapped = (static_cast<double>(value) - 32768.0) * 57344.0 / 49152.0 + 32768.0;
    return static_cast<std::uint16_t>(std::clamp(static_cast<int>(mapped + 0.5), 0, 65535));
  };
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int plane_width = output->GetRowSize(plane) / static_cast<int>(sizeof(std::uint16_t));
    const int plane_height = output->GetHeight(plane);
    const int source_pitch = source->GetPitch(plane);
    const int output_pitch = output->GetPitch(plane);
    for (int y = 0; y < plane_height; ++y) {
      const auto* source_row = reinterpret_cast<const std::uint16_t*>(
          source->GetReadPtr(plane) + y * source_pitch);
      const auto* output_row = reinterpret_cast<const std::uint16_t*>(
          output->GetReadPtr(plane) + y * output_pitch);
      for (int x = 0; x < plane_width; ++x) {
        const auto expected = plane == PLANAR_Y ? expected_luma(source_row[x])
                                                : expected_chroma(source_row[x]);
        EXPECT_EQ(output_row[x], expected) << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_EQ(output->GetRowSize(PLANAR_U), width / 2 * static_cast<int>(sizeof(std::uint16_t)));
  EXPECT_EQ(output->GetHeight(PLANAR_U), height / 2);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Levels, AppliesGammaToFloatYuv444AndPreservesChromaCenter) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 2;
  const auto vi = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YUV444PS, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  const std::array<float, 5> luma{0.0F, 0.04F, 0.25F, 0.64F, 1.0F};
  const std::array<float, 5> chroma{-0.6F, -0.25F, 0.0F, 0.25F, 0.6F};
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x90 + plane * 0x11), plane);
    write_frame_plane<float>(source, plane, [plane, &luma, &chroma](int x, int) {
      return plane == PLANAR_Y ? luma[static_cast<std::size_t>(x)]
                                : chroma[static_cast<std::size_t>(x)];
    });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Levels filter(clip, 0.0f, 2.0, 1.0f, 0.0f, 1.0f, false, false, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int pitch = output->GetPitch(plane);
    const auto* output_row = reinterpret_cast<const float*>(output->GetReadPtr(plane));
    for (int x = 0; x < width; ++x) {
      const float input = plane == PLANAR_Y ? luma[static_cast<std::size_t>(x)]
                                            : chroma[static_cast<std::size_t>(x)];
      const float expected = plane == PLANAR_Y
                                 ? std::sqrt(std::clamp(input, 0.0F, 1.0F))
                                 : std::clamp(input, -0.5F, 0.5F);
      EXPECT_NEAR(output_row[x], expected, 1.0e-5F) << "plane=" << plane << " x=" << x;
    }
    EXPECT_EQ(pitch >= output->GetRowSize(plane), true);
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

bool mask_hs_selects(std::uint8_t u, std::uint8_t v, double start_hue, double end_hue,
                     double max_sat, double min_sat) {
  const double x = static_cast<double>(v) - 128.0;
  const double y = static_cast<double>(u) - 128.0;
  double hue = std::atan2(x, y) * 180.0 / 3.141592653589793;
  if (hue < 0.0) {
    hue += 360.0;
  }
  const bool in_hue = start_hue < end_hue
                          ? hue >= start_hue && hue <= end_hue
                          : !(hue < start_hue && hue > end_hue);
  if (!in_hue) {
    return false;
  }
  const double saturation_squared = x * x + y * y;
  const double scaled_min_sat = 1.19 * min_sat;
  const double scaled_max_sat = 1.19 * max_sat;
  return scaled_min_sat * scaled_min_sat <= saturation_squared &&
         saturation_squared <= scaled_max_sat * scaled_max_sat;
}

class MaskHsYuv444Test : public ::testing::TestWithParam<bool> {};

TEST_P(MaskHsYuv444Test, SelectsWrappedHueAndSaturationWithIndependentOutputRange) {
  constexpr int width = 7;
  constexpr int height = 2;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  AviSynthEnvironment environment;
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x91, PLANAR_Y);
  fill_plane_full_pitch(source, 0xa2, PLANAR_U);
  fill_plane_full_pitch(source, 0xb3, PLANAR_V);
  const std::array<std::uint8_t, 7> u_values{128, 188, 168, 98, 228, 160, 128};
  const std::array<std::uint8_t, 7> v_values{128, 128, 168, 180, 128, 73, 68};
  write_frame_plane<std::uint8_t>(source, PLANAR_U,
                                  [&u_values](int x, int) { return u_values[static_cast<std::size_t>(x)]; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V,
                                  [&v_values](int x, int) { return v_values[static_cast<std::size_t>(x)]; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  const bool realcalc = GetParam();

  MaskHS filter(clip, 300.0, 60.0, 80.0, 20.0, true, realcalc, environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_Y8);
  EXPECT_EQ(filter.GetVideoInfo().width, width);
  EXPECT_EQ(filter.GetVideoInfo().height, height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const auto expected_for = [&u_values, &v_values](int x) {
    return mask_hs_selects(u_values[static_cast<std::size_t>(x)],
                           v_values[static_cast<std::size_t>(x)], 300.0, 60.0, 80.0, 20.0)
               ? static_cast<std::uint8_t>(235)
               : static_cast<std::uint8_t>(16);
  };
  for (int y = 0; y < height; ++y) {
    const auto* row = output->GetReadPtr() + y * output->GetPitch();
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(row[x], expected_for(x))
          << "realcalc=" << realcalc << " x=" << x << " y=" << y;
    }
  }
  const auto output_before = FrameSnapshot::capture(output, filter.GetVideoInfo());
  const PVideoFrame repeat = filter.GetFrame(0, environment.get());
  EXPECT_EQ(FrameSnapshot::capture(repeat, filter.GetVideoInfo()), output_before);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_NE(repeat->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(
    Implementation, MaskHsYuv444Test, ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<bool>& info) {
      return info.param ? "VariantRealcalc" : "VariantLut";
    });

TEST(MaskHsFilter, ProducesSubsampledMaskGeometryForYuv420) {
  constexpr int width = 8;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV12, 1, 25, 1});
  AviSynthEnvironment environment;
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xc4, PLANAR_Y);
  fill_plane_full_pitch(source, 0xd5, PLANAR_U);
  fill_plane_full_pitch(source, 0xe6, PLANAR_V);
  const std::array<std::uint8_t, 4> u_values{188, 98, 228, 160};
  const std::array<std::uint8_t, 4> v_values{128, 180, 128, 73};
  write_frame_plane<std::uint8_t>(source, PLANAR_U,
                                  [&u_values](int x, int) { return u_values[static_cast<std::size_t>(x)]; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V,
                                  [&v_values](int x, int) { return v_values[static_cast<std::size_t>(x)]; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  MaskHS filter(clip, 0.0, 180.0, 80.0, 20.0, false, true, environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_Y8);
  EXPECT_EQ(filter.GetVideoInfo().width, width / 2);
  EXPECT_EQ(filter.GetVideoInfo().height, height / 2);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  std::vector<std::uint8_t> expected;
  expected.reserve(u_values.size() * 2);
  for (std::size_t x = 0; x < u_values.size(); ++x) {
    expected.push_back(mask_hs_selects(u_values[x], v_values[x], 0.0, 180.0, 80.0, 20.0)
                           ? static_cast<std::uint8_t>(255)
                           : static_cast<std::uint8_t>(0));
  }
  expected.insert(expected.end(), expected.begin(), expected.end());
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, DEFAULT_PLANE), expected);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

struct MaskHsInvalidCase {
  const char* name;
  double start_hue;
  double end_hue;
  double max_sat;
  double min_sat;
};

class MaskHsInvalidTest : public ::testing::TestWithParam<MaskHsInvalidCase> {};

TEST_P(MaskHsInvalidTest, RejectsOutOfRangeConstructionArgumentsBeforeFrameRequest) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x71, PLANAR_Y);
  fill_plane_full_pitch(source, 0x82, PLANAR_U);
  fill_plane_full_pitch(source, 0x93, PLANAR_V);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  EXPECT_THROW(
      {
        MaskHS filter(clip, test_case.start_hue, test_case.end_hue, test_case.max_sat,
                      test_case.min_sat, false, true, environment.get());
      },
      AvisynthError)
      << "case=" << test_case.name;
  EXPECT_TRUE(source_clip->frame_requests().empty()) << "case=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before) << "case=" << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(
    ArgumentBounds, MaskHsInvalidTest,
    ::testing::Values(
        MaskHsInvalidCase{"StartBelowZero", -0.1, 60.0, 80.0, 20.0},
        MaskHsInvalidCase{"StartAt360", 360.0, 60.0, 80.0, 20.0},
        MaskHsInvalidCase{"EndAtZero", 0.0, 0.0, 80.0, 20.0},
        MaskHsInvalidCase{"EndAbove360", 0.0, 360.1, 80.0, 20.0},
        MaskHsInvalidCase{"MinNotBelowMax", 0.0, 60.0, 20.0, 80.0},
        MaskHsInvalidCase{"MinBelowZero", 0.0, 60.0, 80.0, -0.1},
        MaskHsInvalidCase{"MinAt150", 0.0, 60.0, 80.0, 150.0},
        MaskHsInvalidCase{"MaxAbove150", 0.0, 60.0, 150.1, 20.0}),
    [](const ::testing::TestParamInfo<MaskHsInvalidCase>& info) {
      return info.param.name;
    });

}  // namespace
