#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_HISTOGRAM_UNDEF_AVS_UNUSED
#endif
#include "filters/histogram.h"
#ifdef AVSUT_HISTOGRAM_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_HISTOGRAM_UNDEF_AVS_UNUSED
#endif

#include "support/audio_sequence_clip.h"
#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using avsut::test::AudioRequest;
using avsut::test::AviSynthEnvironment;
using avsut::test::CacheHintRequest;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSnapshot;
using avsut::test::make_audio_bytes;
using avsut::test::make_video_info;
using avsut::test::StaticFrameClip;
using avsut::test::VideoInfoSpec;
using avsut::test::write_frame_plane;

using InputRow = std::array<std::uint8_t, 8>;

void write_rows(PVideoFrame& frame, const std::array<InputRow, 2>& rows) {
  const int pitch = frame->GetPitch(PLANAR_Y);
  for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
    auto* row = frame->GetWritePtr(PLANAR_Y) + y * pitch;
    std::copy(rows[static_cast<std::size_t>(y)].begin(), rows[static_cast<std::size_t>(y)].end(),
              row);
  }
}

std::array<std::uint8_t, 256> expected_classic_row(const InputRow& input) {
  std::array<int, 256> histogram{};
  for (const std::uint8_t value : input) {
    ++histogram[value];
  }

  std::array<std::uint8_t, 256> exptab{};
  constexpr int tv_range_low = 16;
  constexpr int tv_range_high = 235;
  constexpr int range_luma = tv_range_high - tv_range_low;
  const double k = std::log(0.5 / range_luma) / 255.0;
  exptab[0] = static_cast<std::uint8_t>(tv_range_low);
  for (int i = 1; i < 255; ++i) {
    exptab[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(tv_range_low + 0.5 + range_luma * (1.0 - std::exp(i * k))));
  }
  exptab[255] = static_cast<std::uint8_t>(tv_range_high);

  std::array<std::uint8_t, 256> expected{};
  for (int x = 0; x < 256; ++x) {
    expected[static_cast<std::size_t>(x)] =
        exptab[static_cast<std::size_t>(std::min(255, histogram[static_cast<std::size_t>(x)]))];
  }
  return expected;
}

struct histogram_color2_params {
  enum { GRATICULE_OFF = 0, GRATICULE_ON = 1 };
  int graticule_type{GRATICULE_OFF};
  bool dummy1{}, dummy2{}, dummy3{}, dummy4{}, dummy5{};
};

class HistogramFilter : public Histogram {
 public:
  HistogramFilter(PClip _child, Mode _mode, AVSValue _option, int _show_bits, bool _keepsource,
                  bool _markers, const char*, const histogram_color2_params&,
                  IScriptEnvironment* env)
      : Histogram(_child, _mode, _option, _show_bits, _keepsource, _markers, env) {}
};
#define Histogram HistogramFilter

histogram_color2_params classic_params() {
  histogram_color2_params params{};
  params.graticule_type = histogram_color2_params::GRATICULE_OFF;
  return params;
}

TEST(Histogram, DrawsClassicPerRowHistogramWithStableMapping) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xc1, PLANAR_Y);
  const std::array<InputRow, 2> input{{
      InputRow{0, 16, 16, 128, 128, 128, 235, 255},
      InputRow{16, 16, 32, 32, 32, 32, 32, 240},
  }};
  write_rows(source, input);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Histogram filter(clip, Histogram::ModeClassic, AVSValue(), 8, false, false, nullptr,
                   classic_params(), environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetRowSize(PLANAR_Y), 256);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), vi.height);
  for (int y = 0; y < vi.height; ++y) {
    const auto* output_row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto expected = expected_classic_row(input[static_cast<std::size_t>(y)]);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), output_row)) << "row=" << y;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  const std::vector<int> first_requests{0};
  EXPECT_EQ(source_clip->frame_requests(), first_requests);
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);

  const PVideoFrame repeat = filter.GetFrame(0, environment.get());
  for (int y = 0; y < vi.height; ++y) {
    EXPECT_TRUE(std::equal(output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y),
                           output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y) + 256,
                           repeat->GetReadPtr(PLANAR_Y) + y * repeat->GetPitch(PLANAR_Y)))
        << "row=" << y;
  }
  const std::vector<int> repeated_requests{0, 0};
  EXPECT_EQ(source_clip->frame_requests(), repeated_requests);
}

TEST(Histogram, KeepsSourceToTheLeftOfClassicPanel) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xd2, PLANAR_Y);
  const std::array<InputRow, 2> input{{
      InputRow{0, 16, 16, 128, 128, 128, 235, 255},
      InputRow{16, 16, 32, 32, 32, 32, 32, 240},
  }};
  write_rows(source, input);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Histogram filter(clip, Histogram::ModeClassic, AVSValue(), 8, true, false, nullptr,
                   classic_params(), environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetRowSize(PLANAR_Y), vi.width + 256);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), vi.height);
  for (int y = 0; y < vi.height; ++y) {
    const auto* output_row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* source_row = source->GetReadPtr(PLANAR_Y) + y * source->GetPitch(PLANAR_Y);
    EXPECT_TRUE(std::equal(source_row, source_row + vi.width, output_row)) << "row=" << y;
    const auto expected = expected_classic_row(input[static_cast<std::size_t>(y)]);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), output_row + vi.width)) << "row=" << y;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  const std::vector<int> expected_requests{0};
  EXPECT_EQ(source_clip->frame_requests(), expected_requests);
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

void write_yv24_levels_source(PVideoFrame& frame) {
  constexpr std::array<std::array<std::uint8_t, 4>, 2> y_values{{
      {{0, 1, 1, 7}},
      {{8, 9, 10, 11}},
  }};
  constexpr std::array<std::array<std::uint8_t, 4>, 2> u_values{{
      {{2, 2, 3, 4}},
      {{5, 6, 7, 8}},
  }};
  constexpr std::array<std::array<std::uint8_t, 4>, 2> v_values{{
      {{12, 13, 14, 14}},
      {{15, 16, 17, 18}},
  }};
  const auto write_values = [](PVideoFrame& target, int plane, const auto& values) {
    const int pitch = target->GetPitch(plane);
    for (int y = 0; y < static_cast<int>(values.size()); ++y) {
      auto* row = target->GetWritePtr(plane) + y * pitch;
      for (int x = 0; x < static_cast<int>(values[static_cast<std::size_t>(y)].size()); ++x) {
        row[x] = values[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
      }
    }
  };
  fill_plane_full_pitch(frame, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(frame, 0xb2, PLANAR_U);
  fill_plane_full_pitch(frame, 0xc3, PLANAR_V);
  write_values(frame, PLANAR_Y, y_values);
  write_values(frame, PLANAR_U, u_values);
  write_values(frame, PLANAR_V, v_values);
}

histogram_color2_params no_color_overlays() {
  histogram_color2_params params{};
  params.graticule_type = histogram_color2_params::GRATICULE_OFF;
  return params;
}

TEST(Histogram, DrawsLevelsForEachYuvPlaneBesideKeptSource) {
  AviSynthEnvironment environment;
  constexpr int source_width = 4;
  constexpr int source_height = 2;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  write_yv24_levels_source(source);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Histogram filter(clip, Histogram::ModeLevels, AVSValue(100.0), 8, true, false, nullptr,
                   no_color_overlays(), environment.get());
  const auto& output_vi = filter.GetVideoInfo();
  EXPECT_EQ(output_vi.width, source_width + 256);
  EXPECT_EQ(output_vi.height, 256);
  EXPECT_EQ(output_vi.pixel_type, VideoInfo::CS_YV24);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    EXPECT_EQ(output->GetRowSize(plane), output_vi.width);
    EXPECT_EQ(output->GetHeight(plane), output_vi.height);
    for (int y = 0; y < source_height; ++y) {
      const auto* source_row = source->GetReadPtr(plane) + y * source->GetPitch(plane);
      const auto* output_row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      EXPECT_TRUE(std::equal(source_row, source_row + source_width, output_row))
          << "plane=" << plane << " row=" << y;
    }
  }

  const auto* output_y = output->GetReadPtr(PLANAR_Y);
  const int output_pitch = output->GetPitch(PLANAR_Y);
  const auto expect_bar = [&](int baseline, int value, int last_row) {
    EXPECT_EQ(output_y[last_row * output_pitch + source_width + value], 235)
        << "baseline=" << baseline << " value=" << value;
    EXPECT_EQ(output_y[(last_row - 1) * output_pitch + source_width + value], 16)
        << "baseline=" << baseline << " value=" << value;
  };
  expect_bar(64, 0, 34);   // One Y sample, maximum population is two.
  expect_bar(64, 1, 2);    // Two Y samples reach the full bar height.
  expect_bar(144, 2, 82);  // Two U samples reach the full bar height.
  expect_bar(144, 3, 114);
  expect_bar(224, 12, 194);  // One V sample at the V baseline.
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Histogram, PlotsSubsampledChromaInColorMode) {
  AviSynthEnvironment environment;
  constexpr int width = 4;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x51, PLANAR_Y);
  fill_plane_full_pitch(source, 0x62, PLANAR_U);
  fill_plane_full_pitch(source, 0x73, PLANAR_V);
  const std::array<std::array<std::uint8_t, 2>, 2> u_values{{
      {{20, 40}},
      {{60, 80}},
  }};
  const std::array<std::array<std::uint8_t, 2>, 2> v_values{{
      {{30, 50}},
      {{70, 90}},
  }};
  for (int y = 0; y < 2; ++y) {
    auto* u_row = source->GetWritePtr(PLANAR_U) + y * source->GetPitch(PLANAR_U);
    auto* v_row = source->GetWritePtr(PLANAR_V) + y * source->GetPitch(PLANAR_V);
    for (int x = 0; x < 2; ++x) {
      u_row[x] = u_values[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
      v_row[x] = v_values[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
    }
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Histogram filter(clip, Histogram::ModeColor, AVSValue(), 8, false, false, nullptr,
                   no_color_overlays(), environment.get());
  const auto& output_vi = filter.GetVideoInfo();
  EXPECT_EQ(output_vi.width, 256);
  EXPECT_EQ(output_vi.height, 256);
  EXPECT_EQ(output_vi.pixel_type, VideoInfo::CS_YV12);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  const int y_pitch = output->GetPitch(PLANAR_Y);
  const auto* y_panel = output->GetReadPtr(PLANAR_Y);
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      const int u = u_values[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
      const int v = v_values[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
      EXPECT_EQ(y_panel[v * y_pitch + u], 17) << "u=" << u << " v=" << v;
    }
  }
  EXPECT_EQ(y_panel[1 * y_pitch + 1], 0); // Outside the nominal chroma range.
  EXPECT_EQ(y_panel[128 * y_pitch + 128], 16); // Empty bin within the nominal range.
  EXPECT_EQ(output->GetReadPtr(PLANAR_U)[0], 0);
  EXPECT_EQ(output->GetReadPtr(PLANAR_U)[127], 254);
  EXPECT_EQ(output->GetReadPtr(PLANAR_V)[0], 0);
  EXPECT_EQ(output->GetReadPtr(PLANAR_V)[127 * output->GetPitch(PLANAR_V)], 254);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Histogram, PlotsSamplesInColor2VectorscopeWithGraticule) {
  AviSynthEnvironment environment;
  constexpr int width = 4;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x41, PLANAR_Y);
  fill_plane_full_pitch(source, 0x52, PLANAR_U);
  fill_plane_full_pitch(source, 0x63, PLANAR_V);
  const std::array<std::array<std::uint8_t, 4>, 4> y_values{{
      {{11, 12, 21, 22}},
      {{13, 14, 23, 24}},
      {{31, 32, 41, 42}},
      {{33, 34, 43, 44}},
  }};
  for (int y = 0; y < height; ++y) {
    auto* row = source->GetWritePtr(PLANAR_Y) + y * source->GetPitch(PLANAR_Y);
    std::copy(y_values[static_cast<std::size_t>(y)].begin(),
              y_values[static_cast<std::size_t>(y)].end(), row);
  }
  const std::array<std::array<std::uint8_t, 2>, 2> u_values{{
      {{20, 40}},
      {{60, 80}},
  }};
  const std::array<std::array<std::uint8_t, 2>, 2> v_values{{
      {{30, 50}},
      {{70, 90}},
  }};
  for (int y = 0; y < 2; ++y) {
    auto* u_row = source->GetWritePtr(PLANAR_U) + y * source->GetPitch(PLANAR_U);
    auto* v_row = source->GetWritePtr(PLANAR_V) + y * source->GetPitch(PLANAR_V);
    for (int x = 0; x < 2; ++x) {
      u_row[x] = u_values[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
      v_row[x] = v_values[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
    }
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  auto params = no_color_overlays();
  params.graticule_type = histogram_color2_params::GRATICULE_ON;

  Histogram filter(clip, Histogram::ModeColor2, AVSValue(), 8, false, false, nullptr, params,
                   environment.get());
  const auto& output_vi = filter.GetVideoInfo();
  EXPECT_EQ(output_vi.width, 256);
  EXPECT_EQ(output_vi.height, 256);
  EXPECT_EQ(output_vi.pixel_type, VideoInfo::CS_YV12);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const int y_pitch = output->GetPitch(PLANAR_Y);
  const auto* output_y = output->GetReadPtr(PLANAR_Y);
  const std::array<std::array<std::uint8_t, 2>, 2> expected_luma{{
      {{11, 21}},
      {{31, 41}},
  }};
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      const int u = u_values[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
      const int v = v_values[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
      EXPECT_EQ(output_y[v * y_pitch + u],
                expected_luma[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)])
          << "u=" << u << " v=" << v;
      EXPECT_EQ(output->GetReadPtr(PLANAR_U)[(v >> 1) * output->GetPitch(PLANAR_U) + (u >> 1)],
                static_cast<std::uint8_t>(u))
          << "U u=" << u << " v=" << v;
      EXPECT_EQ(output->GetReadPtr(PLANAR_V)[(v >> 1) * output->GetPitch(PLANAR_V) + (u >> 1)],
                static_cast<std::uint8_t>(v))
          << "V u=" << u << " v=" << v;
    }
  }
  EXPECT_EQ(output_y[1 * y_pitch + 1], 16);
  EXPECT_EQ(output_y[16 * y_pitch + 16], 128);
  EXPECT_EQ(output->GetReadPtr(PLANAR_U)[0], 128);
  EXPECT_EQ(output->GetReadPtr(PLANAR_V)[0], 128);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

std::uint8_t amplified_luma(std::uint8_t value) {
  const int pixel = static_cast<int>(value) << 4;
  return static_cast<std::uint8_t>((pixel & 256) ? (255 - (pixel & 0xff)) : (pixel & 0xff));
}

TEST(Histogram, AmplifiesLumaAndRestoresNeutralChromaAndAlpha) {
  AviSynthEnvironment environment;
  constexpr int width = 4;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x80 + plane * 13), plane);
  }
  write_frame_plane<std::uint8_t>(source, PLANAR_Y,
                                  [](int x, int y) { return 3 + x * 11 + y * 17; });
  write_frame_plane<std::uint8_t>(source, PLANAR_U,
                                  [](int x, int y) { return 21 + x * 7 + y * 5; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V,
                                  [](int x, int y) { return 201 - x * 9 - y * 3; });
  write_frame_plane<std::uint8_t>(source, PLANAR_A,
                                  [](int x, int y) { return 31 + x * 13 + y * 19; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Histogram filter(clip, Histogram::ModeLuma, AVSValue(), 8, false, false, nullptr,
                   no_color_overlays(), environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YUVA420);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < height; ++y) {
    const auto* output_row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < width; ++x) {
      const auto input = static_cast<std::uint8_t>(3 + x * 11 + y * 17);
      EXPECT_EQ(output_row[x], amplified_luma(input)) << "x=" << x << " y=" << y;
    }
  }
  for (const int plane : {PLANAR_U, PLANAR_V}) {
    for (int y = 0; y < output->GetHeight(plane); ++y) {
      const auto* row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int x = 0; x < output->GetRowSize(plane); ++x) {
        EXPECT_EQ(row[x], 128) << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  for (int y = 0; y < height; ++y) {
    const auto* alpha_row = output->GetReadPtr(PLANAR_A) + y * output->GetPitch(PLANAR_A);
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(alpha_row[x], 255) << "alpha x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Histogram, RejectsInvalidDisplayBitsBeforeFrameRequest) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(source, 0xb2, PLANAR_U);
  fill_plane_full_pitch(source, 0xc3, PLANAR_V);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  EXPECT_THROW(
      {
        Histogram filter(clip, Histogram::ModeColor2, AVSValue(), 7, false, false, nullptr,
                         no_color_overlays(), environment.get());
      },
      AvisynthError);
  EXPECT_THROW(
      {
        Histogram filter(clip, Histogram::ModeColor2, AVSValue(), 13, false, false, nullptr,
                         no_color_overlays(), environment.get());
      },
      AvisynthError);
  EXPECT_TRUE(source_clip->frame_requests().empty());
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Histogram, RejectsColor2ForGreyscaleWithoutRequestingFrame) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xd4, PLANAR_Y);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  EXPECT_THROW(
      {
        Histogram filter(clip, Histogram::ModeColor2, AVSValue(), 8, false, false, nullptr,
                         no_color_overlays(), environment.get());
      },
      AvisynthError);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

void attach_audio(VideoInfo& video_info, int sample_rate, int sample_type, int channels,
                  std::int64_t sample_count) {
  video_info.audio_samples_per_second = sample_rate;
  video_info.sample_type = sample_type;
  video_info.nchannels = channels;
  video_info.num_audio_samples = sample_count;
  video_info.SetChannelMask(false, 0);
}

void mix_luma_reference(std::uint8_t& pixel, int value, int alpha) {
  pixel = static_cast<std::uint8_t>(pixel + (((value - static_cast<int>(pixel)) * alpha) >> 8));
}

struct AudioLevelMetrics {
  int max_square{};
  std::int64_t rms_square_sum{};
  double peak_db{96.0};
  double rms_db{96.0};
  int y_pos{};
  int y_mid{};
  bool clipped{};
};

AudioLevelMetrics audio_level_metrics(const std::vector<std::int16_t>& frame_samples, int channel,
                                      int channels, int sample_count, int bar_height) {
  AudioLevelMetrics metrics{};
  for (int index = channel; index < sample_count * channels; index += channels) {
    const int sample = frame_samples[static_cast<std::size_t>(index)];
    const int square = sample * sample;
    metrics.rms_square_sum += square;
    metrics.max_square = std::max(metrics.max_square, square);
  }
  metrics.clipped = metrics.max_square >= 32767 * 32767;
  if (metrics.max_square > 0) {
    metrics.peak_db = -8.685889638 / 2.0 *
                      std::log(static_cast<double>(metrics.max_square) / (32768.0 * 32768.0));
  }
  const std::int64_t mean_square = metrics.rms_square_sum / sample_count;
  if (mean_square > 0) {
    metrics.rms_db =
        -8.685889638 / 2.0 * std::log(static_cast<double>(mean_square) / (32768.0 * 32768.0));
  }
  metrics.y_pos = static_cast<int>((static_cast<double>(bar_height) * metrics.peak_db) / 96.0);
  metrics.y_mid = static_cast<int>((static_cast<double>(bar_height) * metrics.rms_db) / 96.0);
  return metrics;
}

int audio_levels_bar_width(int frame_width, int channels) {
  int bar_w = 60;
  int total_width = (1 + channels * 2) * bar_w;
  if (total_width > frame_width) {
    bar_w = ((frame_width / (1 + channels * 2)) / 4) * 4;
  }
  return bar_w;
}

// Avoid the AudioLevels dotted-line x pattern: upstream paints when (x & 12) == 0.
int audio_levels_probe_x(int x_pos, int x_end) {
  for (int x = x_pos; x < x_end; ++x) {
    if ((x & 12) != 0) {
      return x;
    }
  }
  throw std::logic_error("no safe AudioLevels probe x inside bar");
}

TEST(Histogram, DrawsAudioLevelsBarsFromPerFrameInt16PeaksAndRms) {
  AviSynthEnvironment environment;
  // Wide enough for two 60-pixel meter columns without shrinking bar_w.
  auto vi = make_video_info(VideoInfoSpec{320, 120, VideoInfo::CS_YV12, 1, 25, 1});
  constexpr int sample_rate = 48000;
  constexpr int channels = 2;
  attach_audio(vi, sample_rate, SAMPLE_INT16, channels, 0);
  const int samples_per_frame = static_cast<int>(vi.AudioSamplesFromFrames(1));
  ASSERT_EQ(samples_per_frame, 1920);
  vi.num_audio_samples = samples_per_frame;

  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x40, PLANAR_Y);
  fill_plane_full_pitch(source, 0x80, PLANAR_U);
  fill_plane_full_pitch(source, 0x80, PLANAR_V);
  const auto source_before = FrameSnapshot::capture(source, vi);

  std::vector<std::int16_t> samples(static_cast<std::size_t>(samples_per_frame) * channels, 0);
  samples[0] = 32767;
  samples[1] = 16384;
  auto* source_clip = new StaticFrameClip(vi, source, make_audio_bytes(samples));
  const PClip clip(source_clip);

  Histogram filter(clip, Histogram::ModeAudioLevels, AVSValue(), 8, true, true, nullptr,
                   classic_params(), environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  ASSERT_FALSE(source_clip->cache_hint_requests().empty());
  const CacheHintRequest expected_audio_cache{CACHE_AUDIO, 4096 * 1024};
  EXPECT_EQ(source_clip->cache_hint_requests().front(), expected_audio_cache);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), vi.width);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), vi.height);

  const int bar_w = audio_levels_bar_width(vi.width, channels);
  ASSERT_EQ(bar_w, 60);
  const auto left = audio_level_metrics(samples, 0, channels, samples_per_frame, vi.height);
  const auto right = audio_level_metrics(samples, 1, channels, samples_per_frame, vi.height);
  ASSERT_TRUE(left.clipped);
  ASSERT_FALSE(right.clipped);
  ASSERT_EQ(left.y_pos, 0);
  ASSERT_GT(right.y_pos, 0);
  ASSERT_GT(left.y_mid, left.y_pos + 2);
  ASSERT_GT(right.y_mid, right.y_pos + 2);

  const int y_pitch = output->GetPitch(PLANAR_Y);
  const int uv_pitch = output->GetPitch(PLANAR_U);
  const auto* y_base = output->GetReadPtr(PLANAR_Y);
  const auto* u_base = output->GetReadPtr(PLANAR_U);
  const auto* v_base = output->GetReadPtr(PLANAR_V);

  auto expect_luma_bar = [&](int channel, const AudioLevelMetrics& metrics, int x, int y) {
    std::uint8_t expected = 0x40;
    if (y >= metrics.y_pos && y < metrics.y_mid) {
      mix_luma_reference(expected, metrics.clipped ? 78 : 90, metrics.clipped ? 96 : 128);
    } else if (y >= metrics.y_mid && y < vi.height) {
      mix_luma_reference(expected, metrics.clipped ? 216 : 137, metrics.clipped ? 160 : 128);
    }
    EXPECT_EQ(y_base[x + y * y_pitch], expected)
        << "channel=" << channel << " x=" << x << " y=" << y << " y_pos=" << metrics.y_pos
        << " y_mid=" << metrics.y_mid;
  };

  auto expect_chroma_bar = [&](int channel, const AudioLevelMetrics& metrics, int uv_x, int uv_y) {
    const int uv_y_pos = metrics.y_pos >> 1;
    const int uv_y_mid = metrics.y_mid >> 1;
    std::uint8_t expected_u = 0x80;
    std::uint8_t expected_v = 0x80;
    if (uv_y >= uv_y_pos && uv_y < uv_y_mid) {
      expected_u = static_cast<std::uint8_t>(metrics.clipped ? 92 : 212);
      expected_v = static_cast<std::uint8_t>(metrics.clipped ? 233 : 114);
    } else if (uv_y >= uv_y_mid) {
      expected_u = static_cast<std::uint8_t>(metrics.clipped ? 44 : 58);
      expected_v = static_cast<std::uint8_t>(metrics.clipped ? 142 : 40);
    }
    const int index = uv_x + uv_y * uv_pitch;
    EXPECT_EQ(u_base[index], expected_u)
        << "channel=" << channel << " uv_x=" << uv_x << " uv_y=" << uv_y;
    EXPECT_EQ(v_base[index], expected_v)
        << "channel=" << channel << " uv_x=" << uv_x << " uv_y=" << uv_y;
  };

  for (const auto& entry : {std::make_pair(0, left), std::make_pair(1, right)}) {
    const int channel = entry.first;
    const AudioLevelMetrics& metrics = entry.second;
    const int x_pos = ((channel * 2) + 1) * bar_w + 8;
    const int x_end = x_pos + bar_w - 8;
    const int probe_x = audio_levels_probe_x(x_pos, x_end);
    // Leave the bottom DrawString band and the top dB labels alone.
    const int usable_top = 24;
    const int usable_bottom = vi.height - 40;
    const int rms_y = std::max(metrics.y_mid + 1, (metrics.y_mid + usable_bottom) / 2);
    ASSERT_GE(rms_y, metrics.y_mid);
    ASSERT_LT(rms_y, usable_bottom);
    expect_luma_bar(channel, metrics, probe_x, rms_y);

    const int peak_span = std::max(1, metrics.y_mid - metrics.y_pos);
    const int peak_y = metrics.y_pos + peak_span / 3;
    if (peak_y >= usable_top && peak_y < metrics.y_mid) {
      expect_luma_bar(channel, metrics, probe_x, peak_y);
    }

    const int uv_x_pos = x_pos >> 1;
    const int uv_x_end = x_end >> 1;
    const int uv_probe_x = uv_x_pos + std::max(1, (uv_x_end - uv_x_pos) / 2);
    const int uv_rms_y =
        (metrics.y_mid >> 1) + std::max(1, ((usable_bottom >> 1) - (metrics.y_mid >> 1)) / 2);
    if (uv_rms_y < (usable_bottom >> 1)) {
      expect_chroma_bar(channel, metrics, uv_probe_x, uv_rms_y);
    }
    const int uv_peak_y =
        (metrics.y_pos >> 1) + std::max(0, ((metrics.y_mid >> 1) - (metrics.y_pos >> 1)) / 3);
    if (uv_peak_y >= (usable_top >> 1) && uv_peak_y < (metrics.y_mid >> 1)) {
      expect_chroma_bar(channel, metrics, uv_probe_x, uv_peak_y);
    }
  }

  // Far-right column is outside the meter and the dB text columns.
  EXPECT_EQ(y_base[(vi.width - 1) + (vi.height / 2) * y_pitch], 0x40);
  EXPECT_NE(output->CheckMemory(), 1);
  // Only rendering requests a video frame.
  EXPECT_EQ(source_clip->frame_requests(), (std::vector<int>{0}));
  EXPECT_EQ(source_clip->audio_requests(), (std::vector<AudioRequest>{{0, samples_per_frame}}));
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Histogram, DrawsStereoY8LissajousFromInterleavedInt16Pairs) {
  AviSynthEnvironment environment;
  // 50 Hz audio with 25 fps gives exactly two samples per frame, so the Stereo
  // plot executes a single supersampled segment.
  auto vi = make_video_info(VideoInfoSpec{8, 8, VideoInfo::CS_Y8, 1, 25, 1});
  constexpr int sample_rate = 50;
  constexpr int channels = 2;
  attach_audio(vi, sample_rate, SAMPLE_INT16, channels, 0);
  const int samples_per_frame = static_cast<int>(vi.AudioSamplesFromFrames(1));
  ASSERT_EQ(samples_per_frame, 2);
  vi.num_audio_samples = samples_per_frame;

  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xaa, PLANAR_Y);
  const auto source_before = FrameSnapshot::capture(source, vi);

  // Off-axis L/R values that avoid the 16-pixel crosshair lattice.
  constexpr std::int16_t left = 3000;
  constexpr std::int16_t right = -1000;
  std::vector<std::int16_t> samples{left, right, left, right};
  auto* source_clip = new StaticFrameClip(vi, source, make_audio_bytes(samples));
  const PClip clip(source_clip);

  Histogram filter(clip, Histogram::ModeStereoY8, AVSValue(), 8, false, false, nullptr,
                   classic_params(), environment.get());
  EXPECT_EQ(filter.GetVideoInfo().width, 512);
  EXPECT_EQ(filter.GetVideoInfo().height, 512);
  EXPECT_TRUE(filter.GetVideoInfo().IsY8());
  EXPECT_EQ(filter.GetVideoInfo().AudioChannels(), 2);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  ASSERT_FALSE(source_clip->cache_hint_requests().empty());
  const CacheHintRequest expected_audio_cache{CACHE_AUDIO, 4096 * 1024};
  EXPECT_EQ(source_clip->cache_hint_requests().front(), expected_audio_cache);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), 512);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), 512);
  const int pitch = output->GetPitch(PLANAR_Y);
  ASSERT_EQ(pitch, 512);
  const auto* plane = output->GetReadPtr(PLANAR_Y);
  auto pixel_at = [&](int x, int y) { return plane[x + y * pitch]; };

  // Independent coordinate for the equal-endpoint supersampled segment.
  const int expected_x = 256 + (((static_cast<int>(left) - static_cast<int>(right)) * 8) >> 11);
  const int expected_y = 256 + (((static_cast<int>(left) + static_cast<int>(right)) * 8) >> 11);
  ASSERT_NE(expected_x % 16, 0);
  ASSERT_NE(expected_y % 16, 0);

  // The plot brightens the Lissajous coordinate and leaves neighbors black.
  EXPECT_GT(pixel_at(expected_x, expected_y), 16);
  EXPECT_LE(pixel_at(expected_x, expected_y), 235);
  EXPECT_EQ(pixel_at(0, 0), 16);
  EXPECT_EQ(pixel_at(0, 256), 235);
  EXPECT_EQ(pixel_at(256, 0), 235);
  EXPECT_EQ(pixel_at(expected_x + 1, expected_y), 16);
  EXPECT_EQ(pixel_at(expected_x, expected_y + 1), 16);

  EXPECT_NE(output->CheckMemory(), 1);
  // Audio-only rendering does not request a video frame.
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>());
  EXPECT_EQ(source_clip->audio_requests(), (std::vector<AudioRequest>{{0, samples_per_frame}}));
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Histogram, DrawsStereoOverlayLissajousOnSourcePanel) {
  AviSynthEnvironment environment;
  // keepsource expands a small planar source to at least 512x512 and plots the
  // stereo Lissajous over a half-brightness copy of the source panel.
  auto vi = make_video_info(VideoInfoSpec{8, 8, VideoInfo::CS_YV12, 1, 25, 1});
  constexpr int sample_rate = 50;
  constexpr int channels = 2;
  attach_audio(vi, sample_rate, SAMPLE_INT16, channels, 0);
  const int samples_per_frame = static_cast<int>(vi.AudioSamplesFromFrames(1));
  ASSERT_EQ(samples_per_frame, 2);
  vi.num_audio_samples = samples_per_frame;

  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x80, PLANAR_Y);
  fill_plane_full_pitch(source, 0x90, PLANAR_U);
  fill_plane_full_pitch(source, 0xa0, PLANAR_V);
  const auto source_before = FrameSnapshot::capture(source, vi);

  // Off-axis L/R values that avoid the 16-pixel crosshair lattice.
  constexpr std::int16_t left = 3000;
  constexpr std::int16_t right = -1000;
  std::vector<std::int16_t> samples{left, right, left, right};
  auto* source_clip = new StaticFrameClip(vi, source, make_audio_bytes(samples));
  const PClip clip(source_clip);

  Histogram filter(clip, Histogram::ModeOverlay, AVSValue(), 8, true, true, nullptr,
                   classic_params(), environment.get());
  EXPECT_EQ(filter.GetVideoInfo().width, 512);
  EXPECT_EQ(filter.GetVideoInfo().height, 512);
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV12);
  EXPECT_EQ(filter.GetVideoInfo().AudioChannels(), 2);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  ASSERT_FALSE(source_clip->cache_hint_requests().empty());
  const CacheHintRequest expected_audio_cache{CACHE_AUDIO, 4096 * 1024};
  EXPECT_EQ(source_clip->cache_hint_requests().front(), expected_audio_cache);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), 512);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), 512);
  const int y_pitch = output->GetPitch(PLANAR_Y);
  const int uv_pitch = output->GetPitch(PLANAR_U);
  ASSERT_GE(y_pitch, 512);
  const auto* y_plane = output->GetReadPtr(PLANAR_Y);
  const auto* u_plane = output->GetReadPtr(PLANAR_U);
  const auto* v_plane = output->GetReadPtr(PLANAR_V);
  auto y_at = [&](int x, int y) { return y_plane[x + y * y_pitch]; };
  auto u_at = [&](int x, int y) { return u_plane[x + y * uv_pitch]; };
  auto v_at = [&](int x, int y) { return v_plane[x + y * uv_pitch]; };

  // Independent coordinate for the equal-endpoint supersampled segment.
  const int expected_x = 256 + (((static_cast<int>(left) - static_cast<int>(right)) * 8) >> 11);
  const int expected_y = 256 + (((static_cast<int>(left) + static_cast<int>(right)) * 8) >> 11);
  ASSERT_NE(expected_x % 16, 0);
  ASSERT_NE(expected_y % 16, 0);
  ASSERT_GE(expected_x, 8);
  ASSERT_GE(expected_y, 8);

  // Outside the source panel the cleared TV-black field is halved to 8. Equal
  // endpoints supersample the same Lissajous coordinate eight times. Each hit
  // does BYTE(value+48) then min(value, 235), so the wrapped accumulation ends
  // at 123 rather than staying clamped at 235.
  EXPECT_EQ(y_at(0, 0), 0x40);
  EXPECT_EQ(y_at(7, 7), 0x40);
  EXPECT_EQ(y_at(8, 0), 8);
  EXPECT_EQ(y_at(0, 8), 8);
  EXPECT_EQ(y_at(expected_x, expected_y), 123);
  EXPECT_EQ(y_at(expected_x + 1, expected_y), 8);
  EXPECT_EQ(y_at(expected_x, expected_y + 1), 8);
  EXPECT_EQ(y_at(0, 256), 235);
  EXPECT_EQ(y_at(256, 0), 235);

  // Chroma is copied from the source panel and left neutral outside it.
  EXPECT_EQ(u_at(0, 0), 0x90);
  EXPECT_EQ(v_at(0, 0), 0xa0);
  EXPECT_EQ(u_at(3, 3), 0x90);
  EXPECT_EQ(v_at(3, 3), 0xa0);
  EXPECT_EQ(u_at(4, 0), 128);
  EXPECT_EQ(v_at(0, 4), 128);

  EXPECT_NE(output->CheckMemory(), 1);
  // Rendering the overlay requests the source video frame once.
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(source_clip->audio_requests(), (std::vector<AudioRequest>{{0, samples_per_frame}}));
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Histogram, RejectsStereoWithoutTwoAudioChannels) {
  AviSynthEnvironment environment;
  auto mono = make_video_info(VideoInfoSpec{8, 8, VideoInfo::CS_Y8, 1, 25, 1});
  attach_audio(mono, 48000, SAMPLE_INT16, 1, 1920);
  PVideoFrame mono_frame = environment.get()->NewVideoFrame(mono);
  fill_plane_full_pitch(mono_frame, 0x11, PLANAR_Y);
  const auto mono_before = FrameSnapshot::capture(mono_frame, mono);
  std::vector<std::int16_t> mono_samples(1920, 1000);
  auto* mono_clip = new StaticFrameClip(mono, mono_frame, make_audio_bytes(mono_samples));
  const PClip mono_source(mono_clip);
  EXPECT_THROW(
      {
        Histogram filter(mono_source, Histogram::ModeStereo, AVSValue(), 8, false, false, nullptr,
                         classic_params(), environment.get());
      },
      AvisynthError);
  // Invalid audio is rejected without requesting a video frame.
  EXPECT_EQ(mono_clip->frame_requests(), std::vector<int>());
  EXPECT_EQ(FrameSnapshot::capture(mono_frame, mono), mono_before);

  auto silent = make_video_info(VideoInfoSpec{8, 8, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame silent_frame = environment.get()->NewVideoFrame(silent);
  fill_plane_full_pitch(silent_frame, 0x22, PLANAR_Y);
  const auto silent_before = FrameSnapshot::capture(silent_frame, silent);
  auto* silent_clip = new StaticFrameClip(silent, silent_frame);
  const PClip silent_source(silent_clip);
  EXPECT_THROW(
      {
        Histogram filter(silent_source, Histogram::ModeStereoY8, AVSValue(), 8, false, false,
                         nullptr, classic_params(), environment.get());
      },
      AvisynthError);
  EXPECT_EQ(silent_clip->frame_requests(), std::vector<int>());
  EXPECT_EQ(FrameSnapshot::capture(silent_frame, silent), silent_before);
}

}  // namespace
