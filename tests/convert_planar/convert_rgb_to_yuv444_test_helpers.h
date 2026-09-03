#pragma once

#include "convert/convert_matrix.h"

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVSUT_DEFINED_AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#endif
#include "convert/convert_planar.h"
#ifdef AVSUT_DEFINED_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_DEFINED_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace avsut::test {

struct PublicRgbToYuv444Case {
  int source_pixel_type{};
  int source_bit_depth{};
  int target_bit_depth{};
  int matrix{};
  std::string matrix_spec;
  bool source_full{};
  bool destination_full{};
  bool quality{};
  bool source_has_alpha{};
  std::size_t width{};
  std::size_t height{};
  std::string name;
};

inline void PrintTo(const PublicRgbToYuv444Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline const char* public_rgb_to_yuv444_matrix_name(int matrix) {
  switch (matrix) {
    case AVS_MATRIX_BT709:
      return "Bt709";
    case AVS_MATRIX_BT2020_NCL:
      return "Bt2020Ncl";
    default:
      return "Bt601Family";
  }
}

inline PublicRgbToYuv444Case make_public_rgb_to_yuv444_case(
    int source_pixel_type, int source_bit_depth, int target_bit_depth, int matrix,
    std::string matrix_spec, bool source_full, bool destination_full, bool quality,
    bool source_has_alpha, std::size_t width, std::size_t height) {
  PublicRgbToYuv444Case result{source_pixel_type,
                               source_bit_depth,
                               target_bit_depth,
                               matrix,
                               std::move(matrix_spec),
                               source_full,
                               destination_full,
                               quality,
                               source_has_alpha,
                               width,
                               height,
                               {}};
  std::ostringstream name;
  name << public_rgb_to_yuv444_matrix_name(matrix) << "_Src"
       << (source_full ? "Full" : "Limited") << "_Dst"
       << (destination_full ? "Full" : "Limited") << "_SrcBits" << source_bit_depth
       << "_DstBits" << target_bit_depth << (source_has_alpha ? "_RgbAP" : "_RgbP")
       << "_" << (quality ? "QualityFloat" : "NativeOrConvert") << "_Width" << width
       << "_Height" << height << "_PatternColorAnchors_PublicPlanarYuv444";
  result.name = name.str();
  return result;
}

struct PublicRgbRange {
  double offset{};
  double span{};
};

struct PublicYuvOutputRange {
  double y_offset{};
  double y_span{};
  double chroma_center{};
  double chroma_half{};
  double maximum{};
};

inline PublicRgbRange make_public_rgb_range(bool full, int bit_depth) {
  if (bit_depth == 32) {
    return full ? PublicRgbRange{0.0, 1.0}
                : PublicRgbRange{16.0 / 255.0, 219.0 / 255.0};
  }
  const double scale = static_cast<double>(std::uint32_t{1} << (bit_depth - 8));
  const double maximum = static_cast<double>((std::uint32_t{1} << bit_depth) - 1U);
  return full ? PublicRgbRange{0.0, maximum}
              : PublicRgbRange{16.0 * scale, 219.0 * scale};
}

inline PublicYuvOutputRange make_public_yuv_output_range(bool full, int bit_depth) {
  if (bit_depth == 32) {
    return full ? PublicYuvOutputRange{0.0, 1.0, 0.0, 0.5, 1.0}
                : PublicYuvOutputRange{16.0 / 255.0, 219.0 / 255.0, 0.0,
                                       112.0 / 255.0, 1.0};
  }
  const double scale = static_cast<double>(std::uint32_t{1} << (bit_depth - 8));
  const double maximum = static_cast<double>((std::uint32_t{1} << bit_depth) - 1U);
  return full ? PublicYuvOutputRange{0.0,
                                     maximum,
                                     static_cast<double>(std::uint32_t{1} << (bit_depth - 1)),
                                     maximum / 2.0,
                                     maximum}
              : PublicYuvOutputRange{16.0 * scale,
                                     219.0 * scale,
                                     static_cast<double>(std::uint32_t{1} << (bit_depth - 1)),
                                     112.0 * scale,
                                     maximum};
}

inline void public_rgb_to_yuv444_kr_kb(int matrix, double& kr, double& kb) {
  switch (matrix) {
    case AVS_MATRIX_BT709:
      kr = 0.2126;
      kb = 0.0722;
      return;
    case AVS_MATRIX_BT2020_NCL:
      kr = 0.2627;
      kb = 0.0593;
      return;
    default:
      kr = 0.299;
      kb = 0.114;
      return;
  }
}

inline double public_rgb_to_yuv444_sample_from_normalized(
    double normalized, const PublicRgbToYuv444Case& test_case) {
  const auto range = make_public_rgb_range(test_case.source_full, test_case.source_bit_depth);
  return range.offset + range.span * normalized;
}

struct PublicRgbToYuv444Anchor {
  double green{};
  double blue{};
  double red{};
};

inline PublicRgbToYuv444Anchor public_rgb_to_yuv444_color_anchor(std::size_t anchor) {
  switch (anchor % 8) {
    case 0:
      return {0.0, 0.0, 0.0};
    case 1:
      return {1.0, 1.0, 1.0};
    case 2:
      return {0.0, 0.0, 1.0};
    case 3:
      return {1.0, 0.0, 0.0};
    case 4:
      return {0.0, 1.0, 0.0};
    case 5:
      return {0.25, 0.5, 0.75};
    case 6:
      return {0.75, 0.25, 0.5};
    default:
      return {0.625, 0.875, 0.125};
  }
}

template <typename T>
inline void fill_public_rgb_to_yuv444_color_input(
    PVideoFrame& frame, const PublicRgbToYuv444Case& test_case) {
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(frame, 0x9a + plane, plane);
  }
  write_frame_plane<T>(frame, PLANAR_G, [&test_case](int x, int y) {
    return public_rgb_to_yuv444_sample_from_normalized(
        public_rgb_to_yuv444_color_anchor(static_cast<std::size_t>(x + y * 3)).green,
        test_case);
  });
  write_frame_plane<T>(frame, PLANAR_B, [&test_case](int x, int y) {
    return public_rgb_to_yuv444_sample_from_normalized(
        public_rgb_to_yuv444_color_anchor(static_cast<std::size_t>(x + y * 3)).blue,
        test_case);
  });
  write_frame_plane<T>(frame, PLANAR_R, [&test_case](int x, int y) {
    return public_rgb_to_yuv444_sample_from_normalized(
        public_rgb_to_yuv444_color_anchor(static_cast<std::size_t>(x + y * 3)).red,
        test_case);
  });
  if (test_case.source_has_alpha) {
    const auto alpha_max = make_public_rgb_range(true, test_case.source_bit_depth).span;
    fill_plane_full_pitch(frame, 0xc7, PLANAR_A);
    write_frame_plane<T>(frame, PLANAR_A, [alpha_max](int x, int y) {
      return alpha_max * static_cast<double>((x + y * 2) % 5) / 4.0;
    });
  }
}

template <typename T>
inline void fill_public_rgb_to_yuv444_grayscale_input(
    PVideoFrame& frame, const PublicRgbToYuv444Case& test_case) {
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(frame, 0xa5 + plane, plane);
  }
  const double denominator = static_cast<double>(std::max<std::size_t>(1, test_case.width - 1));
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    write_frame_plane<T>(frame, plane, [&test_case, denominator](int x, int) {
      return public_rgb_to_yuv444_sample_from_normalized(
          static_cast<double>(x) / denominator, test_case);
    });
  }
}

struct PublicRgbToYuv444Reference {
  double y{};
  double u{};
  double v{};
};

template <typename T>
inline PublicRgbToYuv444Reference public_rgb_to_yuv444_reference(
    const PublicRgbToYuv444Case& test_case, T green_sample, T blue_sample, T red_sample) {
  const auto source_range = make_public_rgb_range(test_case.source_full, test_case.source_bit_depth);
  const auto destination_range =
      make_public_yuv_output_range(test_case.destination_full, test_case.target_bit_depth);
  double kr{};
  double kb{};
  public_rgb_to_yuv444_kr_kb(test_case.matrix, kr, kb);
  const double kg = 1.0 - kr - kb;
  const double green = (static_cast<double>(green_sample) - source_range.offset) /
                       source_range.span;
  const double blue = (static_cast<double>(blue_sample) - source_range.offset) /
                      source_range.span;
  const double red = (static_cast<double>(red_sample) - source_range.offset) /
                     source_range.span;
  const double y = kr * red + kg * green + kb * blue;
  const double u = blue - (kr * red + kg * green) / (1.0 - kb);
  const double v = red - (kg * green + kb * blue) / (1.0 - kr);
  return {destination_range.y_offset + destination_range.y_span * y,
          destination_range.chroma_center + destination_range.chroma_half * u,
          destination_range.chroma_center + destination_range.chroma_half * v};
}

inline std::uint32_t public_rgb_to_yuv444_round_and_clip(double value, int bit_depth) {
  const auto maximum = (std::uint32_t{1} << bit_depth) - 1U;
  const auto rounded = static_cast<std::int64_t>(std::floor(value + 0.5));
  return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
      rounded, 0, static_cast<std::int64_t>(maximum)));
}

inline double public_rgb_to_yuv444_alpha_reference(
    const PublicRgbToYuv444Case& test_case, double source_alpha) {
  if (test_case.source_bit_depth == test_case.target_bit_depth) {
    return source_alpha;
  }
  if (test_case.target_bit_depth == 32) {
    if (test_case.source_bit_depth == 32) {
      return source_alpha;
    }
    const double source_maximum = static_cast<double>(
        (std::uint32_t{1} << test_case.source_bit_depth) - 1U);
    return source_alpha / source_maximum;
  }

  const double target_maximum = static_cast<double>(
      (std::uint32_t{1} << test_case.target_bit_depth) - 1U);
  const double normalized = test_case.source_bit_depth == 32
                                ? source_alpha
                                : source_alpha / static_cast<double>(
                                      (std::uint32_t{1} << test_case.source_bit_depth) - 1U);
  return static_cast<double>(public_rgb_to_yuv444_round_and_clip(
      normalized * target_maximum, test_case.target_bit_depth));
}

template <typename SourceT, typename DestinationT>
inline void expect_public_rgb_to_yuv444_color_output(
    const PVideoFrame& source, const PVideoFrame& output,
    const PublicRgbToYuv444Case& test_case) {
  const int source_pitch = source->GetPitch(PLANAR_G) / static_cast<int>(sizeof(SourceT));
  const int destination_pitch = output->GetPitch(PLANAR_Y) / static_cast<int>(sizeof(DestinationT));
  const auto* green = reinterpret_cast<const SourceT*>(source->GetReadPtr(PLANAR_G));
  const auto* blue = reinterpret_cast<const SourceT*>(source->GetReadPtr(PLANAR_B));
  const auto* red = reinterpret_cast<const SourceT*>(source->GetReadPtr(PLANAR_R));
  const auto* actual_y = reinterpret_cast<const DestinationT*>(output->GetReadPtr(PLANAR_Y));
  const auto* actual_u = reinterpret_cast<const DestinationT*>(output->GetReadPtr(PLANAR_U));
  const auto* actual_v = reinterpret_cast<const DestinationT*>(output->GetReadPtr(PLANAR_V));
  const int width = output->GetRowSize(PLANAR_Y) / static_cast<int>(sizeof(DestinationT));
  ASSERT_EQ(width, static_cast<int>(test_case.width)) << test_case.name;
  ASSERT_EQ(output->GetHeight(PLANAR_Y), static_cast<int>(test_case.height)) << test_case.name;

  for (int y = 0; y < static_cast<int>(test_case.height); ++y) {
    for (int x = 0; x < width; ++x) {
      const auto expected = public_rgb_to_yuv444_reference(
          test_case, green[y * source_pitch + x], blue[y * source_pitch + x],
          red[y * source_pitch + x]);
      const std::array<double, 3> expected_values{expected.y, expected.u, expected.v};
      const std::array<double, 3> actual_values{
          static_cast<double>(actual_y[y * destination_pitch + x]),
          static_cast<double>(actual_u[y * destination_pitch + x]),
          static_cast<double>(actual_v[y * destination_pitch + x])};
      for (std::size_t plane = 0; plane < expected_values.size(); ++plane) {
        const double tolerance = test_case.target_bit_depth == 32 ? 1.0e-5 : 1.0;
        EXPECT_LE(std::abs(actual_values[plane] - expected_values[plane]), tolerance)
            << test_case.name << " plane=" << plane << " row=" << y << " column=" << x
            << " expected=" << expected_values[plane]
            << " actual=" << actual_values[plane];
      }
    }
  }
}

template <typename SourceT, typename DestinationT>
inline void run_public_rgb_to_yuv444_color_case_typed(
    const PublicRgbToYuv444Case& test_case) {
  AviSynthEnvironment environment;
  const auto source_vi = make_video_info(VideoInfoSpec{
      static_cast<int>(test_case.width), static_cast<int>(test_case.height),
      test_case.source_pixel_type, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_public_rgb_to_yuv444_color_input<SourceT>(source_frame, test_case);
  if (!test_case.source_full) {
    AVSMap* properties = environment.get()->getFramePropsRW(source_frame);
    ASSERT_NE(properties, nullptr) << test_case.name;
    ASSERT_EQ(environment.get()->propSetInt(properties, "_ColorRange", AVS_COLORRANGE_LIMITED,
                                             AVSPropAppendMode::PROPAPPENDMODE_REPLACE),
              0)
        << test_case.name;
  }
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  bool bitdepth_converted = false;
  ConvertRGBToYUV444 filter(source, test_case.matrix_spec.c_str(), false,
                            test_case.target_bit_depth, test_case.quality, bitdepth_converted,
                            false, environment.get());
  ASSERT_TRUE(bitdepth_converted) << test_case.name;
  ASSERT_TRUE(filter.GetVideoInfo().IsYUV() || filter.GetVideoInfo().IsYUVA())
      << test_case.name;
  ASSERT_TRUE(filter.GetVideoInfo().Is444()) << test_case.name;
  ASSERT_EQ(filter.GetVideoInfo().BitsPerComponent(), test_case.target_bit_depth)
      << test_case.name;
  ASSERT_EQ(filter.GetVideoInfo().IsYUVA(), test_case.source_has_alpha) << test_case.name;
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER) << test_case.name;

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  expect_public_rgb_to_yuv444_color_output<SourceT, DestinationT>(source_frame, output,
                                                                   test_case);
  if (test_case.source_has_alpha) {
    const int source_pitch = source_frame->GetPitch(PLANAR_A) / static_cast<int>(sizeof(SourceT));
    const int output_pitch = output->GetPitch(PLANAR_A) / static_cast<int>(sizeof(DestinationT));
    const auto* source_alpha = reinterpret_cast<const SourceT*>(source_frame->GetReadPtr(PLANAR_A));
    const auto* output_alpha = reinterpret_cast<const DestinationT*>(output->GetReadPtr(PLANAR_A));
    for (int y = 0; y < static_cast<int>(test_case.height); ++y) {
      for (int x = 0; x < static_cast<int>(test_case.width); ++x) {
        const auto expected_alpha = public_rgb_to_yuv444_alpha_reference(
            test_case, static_cast<double>(source_alpha[y * source_pitch + x]));
        EXPECT_EQ(output_alpha[y * output_pitch + x], static_cast<DestinationT>(expected_alpha))
            << test_case.name << " alpha row=" << y << " column=" << x;
      }
    }
  }

  const AVSMap* output_props = environment.get()->getFramePropsRO(output);
  ASSERT_NE(output_props, nullptr) << test_case.name;
  int error = 0;
  EXPECT_EQ(environment.get()->propGetInt(output_props, "_ColorRange", 0, &error),
            test_case.destination_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED)
      << test_case.name;
  EXPECT_EQ(error, 0) << test_case.name;
  error = 0;
  EXPECT_EQ(environment.get()->propGetInt(output_props, "_Matrix", 0, &error), test_case.matrix)
      << test_case.name;
  EXPECT_EQ(error, 0) << test_case.name;

  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>({0, 0})) << test_case.name;
  EXPECT_NE(source_frame->CheckMemory(), 1) << test_case.name;
  EXPECT_NE(output->CheckMemory(), 1) << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before) << test_case.name;
}

inline void run_public_rgb_to_yuv444_color_case(const PublicRgbToYuv444Case& test_case) {
  if (test_case.source_bit_depth == 8) {
    if (test_case.target_bit_depth == 8) {
      run_public_rgb_to_yuv444_color_case_typed<std::uint8_t, std::uint8_t>(test_case);
    } else {
      run_public_rgb_to_yuv444_color_case_typed<std::uint8_t, std::uint16_t>(test_case);
    }
  } else if (test_case.source_bit_depth == 16) {
    run_public_rgb_to_yuv444_color_case_typed<std::uint16_t, std::uint16_t>(test_case);
  } else {
    if (test_case.target_bit_depth == 32) {
      run_public_rgb_to_yuv444_color_case_typed<float, float>(test_case);
    } else if (test_case.target_bit_depth == 8) {
      run_public_rgb_to_yuv444_color_case_typed<float, std::uint8_t>(test_case);
    } else {
      run_public_rgb_to_yuv444_color_case_typed<float, std::uint16_t>(test_case);
    }
  }
}

template <typename SourceT, typename DestinationT>
inline void run_public_rgb_to_yuv444_grayscale_case_typed(
    const PublicRgbToYuv444Case& test_case) {
  AviSynthEnvironment environment;
  const auto source_vi = make_video_info(VideoInfoSpec{
      static_cast<int>(test_case.width), static_cast<int>(test_case.height),
      test_case.source_pixel_type, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_public_rgb_to_yuv444_grayscale_input<SourceT>(source_frame, test_case);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  bool bitdepth_converted = false;
  ConvertRGBToYUV444 filter(source, test_case.matrix_spec.c_str(), false,
                            test_case.target_bit_depth, test_case.quality, bitdepth_converted,
                            false, environment.get());
  ASSERT_TRUE(bitdepth_converted) << test_case.name;
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  const int source_pitch = source_frame->GetPitch(PLANAR_G) / static_cast<int>(sizeof(SourceT));
  const int output_pitch = output->GetPitch(PLANAR_Y) / static_cast<int>(sizeof(DestinationT));
  const auto* source_row = reinterpret_cast<const SourceT*>(source_frame->GetReadPtr(PLANAR_G));
  const auto* output_y = reinterpret_cast<const DestinationT*>(output->GetReadPtr(PLANAR_Y));
  const auto* output_u = reinterpret_cast<const DestinationT*>(output->GetReadPtr(PLANAR_U));
  const auto* output_v = reinterpret_cast<const DestinationT*>(output->GetReadPtr(PLANAR_V));
  const auto destination_range =
      make_public_yuv_output_range(test_case.destination_full, test_case.target_bit_depth);
  const auto expected_center = static_cast<DestinationT>(destination_range.chroma_center);
  for (int y = 0; y < static_cast<int>(test_case.height); ++y) {
    int previous_y = -1;
    for (int x = 0; x < static_cast<int>(test_case.width); ++x) {
      const auto expected = public_rgb_to_yuv444_reference(
          test_case, source_row[y * source_pitch + x], source_row[y * source_pitch + x],
          source_row[y * source_pitch + x]);
      const auto expected_y = public_rgb_to_yuv444_round_and_clip(
          expected.y, test_case.target_bit_depth);
      EXPECT_EQ(output_y[y * output_pitch + x], static_cast<DestinationT>(expected_y))
          << test_case.name << " endpoint/gray row=" << y << " column=" << x;
      EXPECT_EQ(output_u[y * output_pitch + x], expected_center)
          << test_case.name << " neutral U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[y * output_pitch + x], expected_center)
          << test_case.name << " neutral V row=" << y << " column=" << x;
      ASSERT_GE(static_cast<int>(output_y[y * output_pitch + x]), previous_y)
          << test_case.name << " luma is not monotonic at row=" << y << " column=" << x;
      previous_y = static_cast<int>(output_y[y * output_pitch + x]);
    }
  }
  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>({0, 0})) << test_case.name;
  EXPECT_NE(source_frame->CheckMemory(), 1) << test_case.name;
  EXPECT_NE(output->CheckMemory(), 1) << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before) << test_case.name;
}

inline void run_public_rgb_to_yuv444_grayscale_case(
    const PublicRgbToYuv444Case& test_case) {
  run_public_rgb_to_yuv444_grayscale_case_typed<std::uint8_t, std::uint16_t>(test_case);
}

inline void fill_public_rgb_to_yuv444_roundtrip_input(PVideoFrame& frame) {
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(frame, 0xd1 + plane, plane);
  }
  write_frame_plane<std::uint8_t>(frame, PLANAR_G, [](int x, int y) {
    return (x * 17 + y * 23 + 11) & 0xff;
  });
  write_frame_plane<std::uint8_t>(frame, PLANAR_B, [](int x, int y) {
    return (x * 29 + y * 13 + 37) & 0xff;
  });
  write_frame_plane<std::uint8_t>(frame, PLANAR_R, [](int x, int y) {
    return (x * 31 + y * 19 + 73) & 0xff;
  });
}

inline void run_public_rgb_to_yuv444_roundtrip_case() {
  constexpr int width = 11;
  constexpr int height = 3;
  AviSynthEnvironment environment;
  const auto rgb_vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBP, 1, 25, 1});
  PVideoFrame rgb_source = environment.get()->NewVideoFrame(rgb_vi);
  fill_public_rgb_to_yuv444_roundtrip_input(rgb_source);
  const auto rgb_before = FrameSnapshot::capture(rgb_source, rgb_vi);
  auto* rgb_clip_impl = new StaticFrameClip(rgb_vi, rgb_source);
  const PClip rgb_clip(rgb_clip_impl);

  bool bitdepth_converted = false;
  ConvertRGBToYUV444 to_yuv(rgb_clip, "709:limited", false, -1, false, bitdepth_converted,
                            false, environment.get());
  ASSERT_FALSE(bitdepth_converted);
  const PVideoFrame yuv_frame = to_yuv.GetFrame(0, environment.get());
  const auto yuv_vi = to_yuv.GetVideoInfo();
  const auto yuv_before = FrameSnapshot::capture(yuv_frame, yuv_vi);
  auto* yuv_clip_impl = new StaticFrameClip(yuv_vi, yuv_frame);
  const PClip yuv_clip(yuv_clip_impl);

  bitdepth_converted = false;
  ConvertYUV444ToRGB to_rgb(yuv_clip, "709:limited", -1, -1, false, bitdepth_converted,
                            environment.get());
  ASSERT_FALSE(bitdepth_converted);
  ASSERT_TRUE(to_rgb.GetVideoInfo().IsPlanarRGB());
  const PVideoFrame roundtrip = to_rgb.GetFrame(0, environment.get());
  const int source_pitch = rgb_source->GetPitch(PLANAR_G);
  const int roundtrip_pitch = roundtrip->GetPitch(PLANAR_G);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    const auto* source = rgb_source->GetReadPtr(plane);
    const auto* actual = roundtrip->GetReadPtr(plane);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const int error = std::abs(static_cast<int>(actual[y * roundtrip_pitch + x]) -
                                   static_cast<int>(source[y * source_pitch + x]));
        EXPECT_LE(error, 2) << "plane=" << plane << " row=" << y << " column=" << x
                            << " source=" << static_cast<int>(source[y * source_pitch + x])
                            << " actual=" << static_cast<int>(actual[y * roundtrip_pitch + x]);
      }
    }
  }
  const AVSMap* output_props = environment.get()->getFramePropsRO(roundtrip);
  ASSERT_NE(output_props, nullptr);
  int error = 0;
  EXPECT_EQ(environment.get()->propGetInt(output_props, "_ColorRange", 0, &error),
            AVS_COLORRANGE_FULL);
  EXPECT_EQ(error, 0);
  EXPECT_EQ(rgb_clip_impl->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(yuv_clip_impl->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_NE(rgb_source->CheckMemory(), 1);
  EXPECT_NE(yuv_frame->CheckMemory(), 1);
  EXPECT_NE(roundtrip->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(rgb_source, rgb_vi), rgb_before);
  EXPECT_EQ(FrameSnapshot::capture(yuv_frame, yuv_vi), yuv_before);
}

}  // namespace avsut::test
