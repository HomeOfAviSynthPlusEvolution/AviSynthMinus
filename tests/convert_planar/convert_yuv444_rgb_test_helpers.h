#pragma once

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
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace avsut::test {

struct PublicYuvToRgbCase {
  int matrix{};
  std::string matrix_spec;
  int source_pixel_type{};
  int source_bit_depth{};
  int target_bit_depth{};
  int constructor_target_bit_depth{};
  int pixel_step{};
  bool quality{};
  bool source_full{};
  bool destination_full{};
  bool source_has_alpha{};
  std::size_t width{};
  std::size_t height{};
  std::string name;
};

inline void PrintTo(const PublicYuvToRgbCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline std::string public_yuv_to_rgb_matrix_name(int matrix) {
  switch (matrix) {
    case AVS_MATRIX_BT709:
      return "Bt709";
    case AVS_MATRIX_BT2020_NCL:
      return "Bt2020Ncl";
    default:
      return "Bt601Family";
  }
}

inline PublicYuvToRgbCase make_public_yuv_to_rgb_case(
    int matrix, std::string matrix_spec, int source_pixel_type, int source_bit_depth,
    int target_bit_depth, int constructor_target_bit_depth, int pixel_step, bool quality,
    bool source_full, bool destination_full, bool source_has_alpha, std::size_t width,
    std::size_t height) {
  PublicYuvToRgbCase result{matrix,
                           std::move(matrix_spec),
                           source_pixel_type,
                           source_bit_depth,
                           target_bit_depth,
                           constructor_target_bit_depth,
                           pixel_step,
                           quality,
                           source_full,
                           destination_full,
                           source_has_alpha,
                           width,
                           height,
                           {}};
  std::ostringstream name;
  name << public_yuv_to_rgb_matrix_name(matrix) << "_Src" << (source_full ? "Full" : "Limited")
       << "_Dst" << (destination_full ? "Full" : "Limited") << "_SrcBits" << source_bit_depth
       << "_DstBits" << target_bit_depth << "_" << (quality ? "QualityFloat" : "NativeOrConvert")
       << (source_has_alpha ? "_SourceAlpha" : "_OpaqueAlpha") << "_Width" << width << "_Height"
       << height << "_PatternMatrixAnchors_PublicPlanarRgb";
  result.name = name.str();
  return result;
}

struct PublicYuvRange {
  double y_low{};
  double y_high{};
  double y_mid{};
  double uv_low{};
  double uv_high{};
  double uv_mid{};
  double uv_center{};
  double maximum{};
  double chroma_half{};
};

inline PublicYuvRange make_public_yuv_range(bool full, int bit_depth) {
  if (bit_depth == 32) {
    return full ? PublicYuvRange{0.0, 1.0, 0.5, -0.5, 0.5, 0.0, 0.0, 1.0, 0.5}
                : PublicYuvRange{16.0 / 255.0,
                                 235.0 / 255.0,
                                 128.0 / 255.0,
                                 -112.0 / 255.0,
                                 112.0 / 255.0,
                                 0.0,
                                 0.0,
                                 1.0,
                                 112.0 / 255.0};
  }

  const double scale = static_cast<double>(std::uint32_t{1} << (bit_depth - 8));
  const double maximum = static_cast<double>((std::uint32_t{1} << bit_depth) - 1U);
  const double uv_center = static_cast<double>(std::uint32_t{1} << (bit_depth - 1));
  return full ? PublicYuvRange{0.0,
                               maximum,
                               (maximum + 1.0) / 2.0,
                               -maximum / 2.0,
                               maximum / 2.0,
                               0.0,
                               uv_center,
                               maximum,
                               maximum / 2.0}
              : PublicYuvRange{16.0 * scale,
                               235.0 * scale,
                               128.0 * scale,
                               -112.0 * scale,
                               112.0 * scale,
                               0.0,
                               uv_center,
                               maximum,
                               112.0 * scale};
}

template <typename T>
inline void fill_public_yuv444_input(PVideoFrame& frame, const PublicYuvToRgbCase& test_case) {
  const auto range = make_public_yuv_range(test_case.source_full, test_case.source_bit_depth);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0x80 + plane), plane);
  }
  if (test_case.source_has_alpha) {
    fill_plane_full_pitch(frame, 0xd4, PLANAR_A);
  }

  auto anchor = [](int x, int y) { return (x + y * 3) % 12; };
  write_frame_plane<T>(frame, PLANAR_Y, [&range, &anchor](int x, int y) {
    switch (anchor(x, y)) {
      case 0:
        return range.y_mid;
      case 1:
        return range.y_low;
      case 2:
        return range.y_high;
      case 7:
        return range.y_high;
      case 8:
        return range.y_low;
      default:
        return range.y_mid;
    }
  });
  write_frame_plane<T>(frame, PLANAR_U, [&range, &anchor](int x, int y) {
    switch (anchor(x, y)) {
      case 3:
        return range.uv_low + range.uv_center;
      case 4:
        return range.uv_high + range.uv_center;
      case 7:
        return range.uv_high + range.uv_center;
      case 8:
        return range.uv_low + range.uv_center;
      case 9:
        return range.uv_low + range.uv_center;
      case 10:
        return range.uv_high + range.uv_center;
      default:
        return range.uv_center;
    }
  });
  write_frame_plane<T>(frame, PLANAR_V, [&range, &anchor](int x, int y) {
    switch (anchor(x, y)) {
      case 5:
        return range.uv_low + range.uv_center;
      case 6:
        return range.uv_high + range.uv_center;
      case 7:
        return range.uv_high + range.uv_center;
      case 8:
        return range.uv_low + range.uv_center;
      case 9:
        return range.uv_high + range.uv_center;
      case 10:
        return range.uv_low + range.uv_center;
      default:
        return range.uv_center;
    }
  });
  if (test_case.source_has_alpha) {
    write_frame_plane<T>(frame, PLANAR_A, [&range, &anchor](int x, int y) {
      switch (anchor(x, y) % 4) {
        case 0:
          return 0.0;
        case 1:
          return range.maximum * 0.25;
        case 2:
          return range.maximum * 0.75;
        default:
          return range.maximum;
      }
    });
  }
}

struct PublicYuvToRgbFloatCoefficients {
  double y_b{};
  double u_b{};
  double v_b{};
  double y_g{};
  double u_g{};
  double v_g{};
  double y_r{};
  double u_r{};
  double v_r{};
  double input_offset{};
  double output_offset{};
  double output_span{};
};

inline void public_yuv_to_rgb_kr_kb(int matrix, double& kr, double& kb) {
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

inline PublicYuvToRgbFloatCoefficients make_public_yuv_to_rgb_float_coefficients(
    const PublicYuvToRgbCase& test_case) {
  double kr{};
  double kb{};
  public_yuv_to_rgb_kr_kb(test_case.matrix, kr, kb);
  const auto source_range = make_public_yuv_range(test_case.source_full, test_case.source_bit_depth);
  const auto destination_range =
      make_public_yuv_range(test_case.destination_full, test_case.source_bit_depth);
  const double kg = 1.0 - kr - kb;
  const double source_y_span = source_range.y_high - source_range.y_low;
  const double destination_rgb_span =
      destination_range.y_high - destination_range.y_low;
  return {destination_rgb_span / source_y_span,
          destination_rgb_span * (1.0 - kb) / source_range.chroma_half,
          0.0,
          destination_rgb_span / source_y_span,
          destination_rgb_span * (kb - 1.0) * kb /
              (kg * source_range.chroma_half),
          destination_rgb_span * (kr - 1.0) * kr /
              (kg * source_range.chroma_half),
          destination_rgb_span / source_y_span,
          0.0,
          destination_rgb_span * (1.0 - kr) / source_range.chroma_half,
          test_case.source_full ? 0.0 : -source_range.y_low,
          test_case.destination_full ? 0.0 : destination_range.y_low,
          destination_rgb_span};
}

struct PublicYuvToRgbFixedCoefficients {
  int y_b{};
  int u_b{};
  int v_b{};
  int y_g{};
  int u_g{};
  int v_g{};
  int y_r{};
  int u_r{};
  int v_r{};
  int input_offset{};
  int output_offset{};
};

inline int public_round_symmetric(double value) {
  return static_cast<int>(value >= 0.0 ? value + 0.5 : value - 0.5);
}

inline PublicYuvToRgbFixedCoefficients make_public_yuv_to_rgb_fixed_coefficients(
    const PublicYuvToRgbCase& test_case) {
  double kr{};
  double kb{};
  public_yuv_to_rgb_kr_kb(test_case.matrix, kr, kb);
  const auto source_range = make_public_yuv_range(test_case.source_full, test_case.source_bit_depth);
  const auto destination_range =
      make_public_yuv_range(test_case.destination_full, test_case.source_bit_depth);
  const double kg = 1.0 - kr - kb;
  const double source_y_span = source_range.y_high - source_range.y_low;
  const double destination_rgb_span =
      destination_range.y_high - destination_range.y_low;
  constexpr double scale = 8192.0;
  return {public_round_symmetric(scale * destination_rgb_span / source_y_span),
          public_round_symmetric(scale * destination_rgb_span * (1.0 - kb) /
                                 source_range.chroma_half),
          0,
          public_round_symmetric(scale * destination_rgb_span / source_y_span),
          public_round_symmetric(scale * destination_rgb_span * (kb - 1.0) * kb /
                                 (kg * source_range.chroma_half)),
          public_round_symmetric(scale * destination_rgb_span * (kr - 1.0) * kr /
                                 (kg * source_range.chroma_half)),
          public_round_symmetric(scale * destination_rgb_span / source_y_span),
          0,
          public_round_symmetric(scale * destination_rgb_span * (1.0 - kr) /
                                 source_range.chroma_half),
          test_case.source_full ? 0 : -static_cast<int>(source_range.y_low),
          test_case.destination_full ? 0 : static_cast<int>(destination_range.y_low)};
}

inline std::uint16_t public_clip_integer(double value, int bit_depth) {
  const auto maximum = (std::int64_t{1} << bit_depth) - 1;
  return static_cast<std::uint16_t>(std::clamp<std::int64_t>(
      static_cast<std::int64_t>(value), 0, maximum));
}

inline double public_yuv_to_rgb_alpha_reference(
    const PublicYuvToRgbCase& test_case, double source_alpha) {
  if (test_case.source_bit_depth == test_case.target_bit_depth) {
    return source_alpha;
  }
  if (test_case.target_bit_depth == 32) {
    if (test_case.source_bit_depth == 32) {
      return source_alpha;
    }
    return source_alpha / static_cast<double>(
                              (std::uint32_t{1} << test_case.source_bit_depth) - 1U);
  }

  const double source_maximum = test_case.source_bit_depth == 32
                                    ? 1.0
                                    : static_cast<double>(
                                          (std::uint32_t{1} << test_case.source_bit_depth) - 1U);
  const double target_maximum = static_cast<double>(
      (std::uint32_t{1} << test_case.target_bit_depth) - 1U);
  const double normalized = source_alpha / source_maximum;
  return static_cast<double>(public_clip_integer(
      std::floor(normalized * target_maximum + 0.5), test_case.target_bit_depth));
}

inline std::int64_t public_shifted_component(int coefficient_a, int coefficient_b,
                                             int coefficient_c, std::int64_t a,
                                             std::int64_t b, std::int64_t c, int offset) {
  const std::int64_t total = static_cast<std::int64_t>(coefficient_a) * a +
                             static_cast<std::int64_t>(coefficient_b) * b +
                             static_cast<std::int64_t>(coefficient_c) * c + 4096 +
                             (static_cast<std::int64_t>(offset) << 13);
  return total >> 13;
}

inline double public_target_float_offset(const PublicYuvToRgbCase& test_case) {
  return test_case.destination_full ? 0.0 : 16.0 / 255.0;
}

inline double public_target_float_span(const PublicYuvToRgbCase& test_case) {
  return test_case.destination_full ? 1.0 : 219.0 / 255.0;
}

inline double public_target_integer_offset(const PublicYuvToRgbCase& test_case) {
  const double scale = static_cast<double>(std::uint32_t{1} << (test_case.target_bit_depth - 8));
  return test_case.destination_full ? 0.0 : 16.0 * scale;
}

inline double public_target_integer_span(const PublicYuvToRgbCase& test_case) {
  const double maximum = static_cast<double>(
      (std::uint32_t{1} << test_case.target_bit_depth) - 1U);
  const double scale = static_cast<double>(std::uint32_t{1} << (test_case.target_bit_depth - 8));
  return test_case.destination_full ? maximum : 219.0 * scale;
}

template <typename SourceT, typename DestinationT>
inline DestinationT public_yuv_to_rgb_expected_sample(const PublicYuvToRgbCase& test_case,
                                                      SourceT y_sample, SourceT u_sample,
                                                      SourceT v_sample, int channel) {
  const bool native_integer = !std::is_floating_point_v<SourceT> &&
                              !std::is_floating_point_v<DestinationT> &&
                              test_case.source_bit_depth == test_case.target_bit_depth &&
                              !test_case.quality;
  double component{};
  if (native_integer) {
    const auto coefficients = make_public_yuv_to_rgb_fixed_coefficients(test_case);
    const auto y = static_cast<std::int64_t>(y_sample) + coefficients.input_offset;
    const auto u = static_cast<std::int64_t>(u_sample) -
                   (std::int64_t{1} << (test_case.source_bit_depth - 1));
    const auto v = static_cast<std::int64_t>(v_sample) -
                   (std::int64_t{1} << (test_case.source_bit_depth - 1));
    switch (channel) {
      case 0:
        component = static_cast<double>(public_shifted_component(
            coefficients.y_g, coefficients.u_g, coefficients.v_g, y, u, v,
            coefficients.output_offset));
        break;
      case 1:
        component = static_cast<double>(public_shifted_component(
            coefficients.y_b, coefficients.u_b, coefficients.v_b, y, u, v,
            coefficients.output_offset));
        break;
      default:
        component = static_cast<double>(public_shifted_component(
            coefficients.y_r, coefficients.u_r, coefficients.v_r, y, u, v,
            coefficients.output_offset));
        break;
    }
    if constexpr (std::is_floating_point_v<DestinationT>) {
      return static_cast<DestinationT>(component);
    }
    return static_cast<DestinationT>(public_clip_integer(component, test_case.target_bit_depth));
  }

  const auto coefficients = make_public_yuv_to_rgb_float_coefficients(test_case);
  const auto source_range = make_public_yuv_range(test_case.source_full, test_case.source_bit_depth);
  double y{};
  double u{};
  double v{};
  if constexpr (std::is_floating_point_v<SourceT>) {
    y = static_cast<double>(y_sample) + coefficients.input_offset;
    u = static_cast<double>(u_sample);
    v = static_cast<double>(v_sample);
  } else {
    y = static_cast<double>(y_sample) + coefficients.input_offset;
    u = static_cast<double>(u_sample) - source_range.uv_center;
    v = static_cast<double>(v_sample) - source_range.uv_center;
  }

  switch (channel) {
    case 0:
      component = coefficients.y_g * y + coefficients.u_g * u + coefficients.v_g * v +
                  coefficients.output_offset;
      break;
    case 1:
      component = coefficients.y_b * y + coefficients.u_b * u + coefficients.v_b * v +
                  coefficients.output_offset;
      break;
    default:
      component = coefficients.y_r * y + coefficients.u_r * u + coefficients.v_r * v +
                  coefficients.output_offset;
      break;
  }

  const double fraction = (component - coefficients.output_offset) / coefficients.output_span;
  if constexpr (std::is_floating_point_v<DestinationT>) {
    return static_cast<DestinationT>(public_target_float_offset(test_case) +
                                     fraction * public_target_float_span(test_case));
  }
  const double target = public_target_integer_offset(test_case) +
                        fraction * public_target_integer_span(test_case);
  return static_cast<DestinationT>(public_clip_integer(std::floor(target + 0.5),
                                                       test_case.target_bit_depth));
}

template <typename SourceT, typename DestinationT>
inline void run_public_yuv_to_rgb_case_typed(const PublicYuvToRgbCase& test_case) {
  AviSynthEnvironment environment;
  const auto source_vi = make_video_info(VideoInfoSpec{
      static_cast<int>(test_case.width), static_cast<int>(test_case.height),
      test_case.source_pixel_type, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_public_yuv444_input<SourceT>(source_frame, test_case);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);

  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);
  bool bitdepth_converted = false;
  ConvertYUV444ToRGB filter(source, test_case.matrix_spec.c_str(), test_case.pixel_step,
                            test_case.constructor_target_bit_depth, test_case.quality,
                            bitdepth_converted, environment.get());

  ASSERT_EQ(bitdepth_converted, test_case.constructor_target_bit_depth != -1) << test_case.name;
  ASSERT_EQ(filter.GetVideoInfo().BitsPerComponent(), test_case.target_bit_depth)
      << test_case.name;
  if (test_case.source_has_alpha) {
    ASSERT_TRUE(filter.GetVideoInfo().IsPlanarRGBA()) << test_case.name;
  } else {
    ASSERT_TRUE(filter.GetVideoInfo().IsPlanarRGB()) << test_case.name;
  }

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  const auto output_vi = filter.GetVideoInfo();
  ASSERT_EQ(output_vi.width, static_cast<int>(test_case.width)) << test_case.name;
  ASSERT_EQ(output->GetHeight(PLANAR_G), static_cast<int>(test_case.height)) << test_case.name;
  const std::array<int, 3> output_planes{PLANAR_G, PLANAR_B, PLANAR_R};
  const int source_pitch_y = source_frame->GetPitch(PLANAR_Y) / static_cast<int>(sizeof(SourceT));
  const int source_pitch_u = source_frame->GetPitch(PLANAR_U) / static_cast<int>(sizeof(SourceT));
  const int source_pitch_v = source_frame->GetPitch(PLANAR_V) / static_cast<int>(sizeof(SourceT));
  const auto* source_y = reinterpret_cast<const SourceT*>(source_frame->GetReadPtr(PLANAR_Y));
  const auto* source_u = reinterpret_cast<const SourceT*>(source_frame->GetReadPtr(PLANAR_U));
  const auto* source_v = reinterpret_cast<const SourceT*>(source_frame->GetReadPtr(PLANAR_V));
  for (std::size_t plane_index = 0; plane_index < output_planes.size(); ++plane_index) {
    const int output_plane = output_planes[plane_index];
    const int output_width = output->GetRowSize(output_plane) / static_cast<int>(sizeof(DestinationT));
    const int output_pitch = output->GetPitch(output_plane) / static_cast<int>(sizeof(DestinationT));
    const auto* output_rows = reinterpret_cast<const DestinationT*>(output->GetReadPtr(output_plane));
    ASSERT_EQ(output_width, static_cast<int>(test_case.width)) << test_case.name;
    for (int y = 0; y < static_cast<int>(test_case.height); ++y) {
      for (int x = 0; x < output_width; ++x) {
        const auto expected = public_yuv_to_rgb_expected_sample<SourceT, DestinationT>(
            test_case, source_y[y * source_pitch_y + x], source_u[y * source_pitch_u + x],
            source_v[y * source_pitch_v + x],
            static_cast<int>(plane_index));
        const auto actual = output_rows[y * output_pitch + x];
        if constexpr (std::is_floating_point_v<DestinationT>) {
          EXPECT_NEAR(actual, expected, 3.0e-5F)
              << test_case.name << " plane=" << output_plane << " row=" << y << " column=" << x;
        } else {
          EXPECT_EQ(actual, expected)
              << test_case.name << " plane=" << output_plane << " row=" << y << " column=" << x;
        }
      }
    }
  }

  if (test_case.source_has_alpha) {
    const int source_pitch = source_frame->GetPitch(PLANAR_A) / static_cast<int>(sizeof(SourceT));
    const int output_pitch = output->GetPitch(PLANAR_A) / static_cast<int>(sizeof(DestinationT));
    const int width = output->GetRowSize(PLANAR_A) / static_cast<int>(sizeof(DestinationT));
    const auto* source_alpha = reinterpret_cast<const SourceT*>(source_frame->GetReadPtr(PLANAR_A));
    const auto* output_alpha = reinterpret_cast<const DestinationT*>(output->GetReadPtr(PLANAR_A));
    for (int y = 0; y < static_cast<int>(test_case.height); ++y) {
      for (int x = 0; x < width; ++x) {
        const auto expected_alpha = public_yuv_to_rgb_alpha_reference(
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
  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>({0, 0})) << test_case.name;
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER) << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before) << test_case.name;
  EXPECT_NE(output->CheckMemory(), 1) << test_case.name;
  for (const int plane : video_frame_planes(output_vi)) {
    EXPECT_GE(output->GetPitch(plane), output->GetRowSize(plane)) << test_case.name;
  }
}

inline void run_public_yuv_to_rgb_case(const PublicYuvToRgbCase& test_case) {
  if (test_case.source_bit_depth == 8) {
    if (test_case.target_bit_depth == 32) {
      run_public_yuv_to_rgb_case_typed<std::uint8_t, float>(test_case);
    } else {
      run_public_yuv_to_rgb_case_typed<std::uint8_t, std::uint16_t>(test_case);
    }
  } else if (test_case.source_bit_depth == 16) {
    if (test_case.target_bit_depth == 8) {
      run_public_yuv_to_rgb_case_typed<std::uint16_t, std::uint8_t>(test_case);
    } else if (test_case.target_bit_depth == 32) {
      run_public_yuv_to_rgb_case_typed<std::uint16_t, float>(test_case);
    } else {
      run_public_yuv_to_rgb_case_typed<std::uint16_t, std::uint16_t>(test_case);
    }
  } else {
    if (test_case.target_bit_depth == 32) {
      run_public_yuv_to_rgb_case_typed<float, float>(test_case);
    } else if (test_case.target_bit_depth == 8) {
      run_public_yuv_to_rgb_case_typed<float, std::uint8_t>(test_case);
    } else {
      run_public_yuv_to_rgb_case_typed<float, std::uint16_t>(test_case);
    }
  }
}

}  // namespace avsut::test
