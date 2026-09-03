#pragma once

#include "convert/convert_matrix.h"
#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#endif
#include "convert/intel/convert_planar_avx2.h"
#include "convert/intel/convert_planar_sse.h"
#include "support/compat_375.h"

#include "support/comparators.h"
#include "support/cpu_features.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace avsut::test {

enum class PackedYv24RgbVariant { Sse2, Ssse3, Avx2 };

struct PackedYv24RgbCase {
  int matrix{AVS_MATRIX_BT709};
  bool source_full{};
  bool destination_full{};
  int pixel_step{};
  bool has_alpha{};
  std::size_t width{};
  std::size_t height{};
  std::size_t y_pitch{};
  std::size_t uv_pitch{};
  std::size_t alpha_pitch{};
  std::size_t destination_pitch{};
  Variant<PackedYv24RgbVariant> variant;
  std::string expected_hash;
  std::string name;
};

inline void PrintTo(const PackedYv24RgbCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline std::string packed_yv24_rgb_variant_name(const std::string& name) {
  std::string result = "Variant";
  bool capitalize = true;
  for (const char character : name) {
    result.push_back(capitalize && character >= 'a' && character <= 'z'
                         ? static_cast<char>(character - ('a' - 'A'))
                         : character);
    capitalize = false;
  }
  return result;
}

inline const char* packed_matrix_name(int matrix) {
  switch (matrix) {
    case AVS_MATRIX_BT709:
      return "Bt709";
    case AVS_MATRIX_BT2020_NCL:
      return "Bt2020Ncl";
    case AVS_MATRIX_BT2020_CL:
      return "Bt2020Cl";
    case AVS_MATRIX_BT470_M:
      return "Bt470M";
    case AVS_MATRIX_ST240_M:
      return "St240M";
    default:
      return "Bt601Family";
  }
}

inline PackedYv24RgbCase make_packed_yv24_rgb_case(
    int matrix, bool source_full, bool destination_full, int pixel_step, bool has_alpha,
    std::size_t width, std::size_t height, std::size_t y_pitch, std::size_t uv_pitch,
    std::size_t alpha_pitch, std::size_t destination_pitch,
    Variant<PackedYv24RgbVariant> variant, std::string expected_hash) {
  PackedYv24RgbCase result{matrix,
                           source_full,
                           destination_full,
                           pixel_step,
                           has_alpha,
                           width,
                           height,
                           y_pitch,
                           uv_pitch,
                           alpha_pitch,
                           destination_pitch,
                           std::move(variant),
                           std::move(expected_hash),
                           {}};
  std::ostringstream stream;
  stream << packed_matrix_name(matrix) << "_Src" << (source_full ? "Full" : "Limited") << "_Dst"
         << (destination_full ? "Full" : "Limited") << '_'
         << (pixel_step == 3 ? "Bgr24" : "Bgr32")
         << (has_alpha ? "_SourceAlpha" : "_OpaqueAlpha") << "_Width" << width << "_Height"
         << height << "_YPitch" << y_pitch << "_UvPitch" << uv_pitch << "_AlphaPitch" << alpha_pitch
         << "_DstPitch" << destination_pitch << "_PatternChannelAnchors_"
         << packed_yv24_rgb_variant_name(result.variant.name);
  result.name = stream.str();
  return result;
}

struct PackedYuvToRgbCoefficients {
  int y_b{};
  int u_b{};
  int v_b{};
  int y_g{};
  int u_g{};
  int v_g{};
  int y_r{};
  int u_r{};
  int v_r{};
  int offset_y{};
  int offset_rgb{};
};

inline int round_symmetric(double value) {
  return static_cast<int>(value >= 0.0 ? value + 0.5 : value - 0.5);
}

inline void packed_kr_kb(int matrix, double& kr, double& kb) {
  switch (matrix) {
    case AVS_MATRIX_BT709:
      kr = 0.2126;
      kb = 0.0722;
      return;
    case AVS_MATRIX_BT2020_NCL:
    case AVS_MATRIX_BT2020_CL:
      kr = 0.2627;
      kb = 0.0593;
      return;
    case AVS_MATRIX_BT470_M:
      kr = 0.3;
      kb = 0.11;
      return;
    case AVS_MATRIX_ST240_M:
      kr = 0.212;
      kb = 0.087;
      return;
    default:
      kr = 0.299;
      kb = 0.114;
      return;
  }
}

inline PackedYuvToRgbCoefficients make_packed_yuv_to_rgb_coefficients(int matrix, bool source_full,
                                                                       bool destination_full) {
  double kr{};
  double kb{};
  packed_kr_kb(matrix, kr, kb);
  const double kg = 1.0 - kr - kb;
  const double y_span = source_full ? 255.0 : 219.0;
  const double uv_span = source_full ? 127.5 : 112.0;
  const double rgb_span = destination_full ? 255.0 : 219.0;
  constexpr double scale = 8192.0;

  return {round_symmetric(scale * rgb_span / y_span),
          round_symmetric(scale * rgb_span * (1.0 - kb) / uv_span),
          0,
          round_symmetric(scale * rgb_span / y_span),
          round_symmetric(scale * rgb_span * (kb - 1.0) * kb / (kg * uv_span)),
          round_symmetric(scale * rgb_span * (kr - 1.0) * kr / (kg * uv_span)),
          round_symmetric(scale * rgb_span / y_span),
          0,
          round_symmetric(scale * rgb_span * (1.0 - kr) / uv_span),
          source_full ? 0 : -16,
          destination_full ? 0 : 16};
}

inline std::uint8_t clip_byte(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

inline std::uint8_t packed_yuv_to_rgb_reference_component(
    int y, int u, int v, int y_coefficient, int u_coefficient, int v_coefficient,
    const PackedYuvToRgbCoefficients& coefficients) {
  const int total = y_coefficient * (y + coefficients.offset_y) + u_coefficient * (u - 128) +
                    v_coefficient * (v - 128) + 4096 + (coefficients.offset_rgb << 13);
  return clip_byte(total >> 13);
}

inline void fill_yv24_channel_anchors(PlaneView<std::uint8_t> y_plane,
                                      PlaneView<std::uint8_t> u_plane,
                                      PlaneView<std::uint8_t> v_plane,
                                      PlaneView<std::uint8_t> alpha_plane, bool source_full) {
  const auto y_low = static_cast<std::uint8_t>(source_full ? 0 : 16);
  const auto y_high = static_cast<std::uint8_t>(source_full ? 255 : 235);
  const auto uv_low = static_cast<std::uint8_t>(source_full ? 0 : 16);
  const auto uv_high = static_cast<std::uint8_t>(source_full ? 255 : 240);
  constexpr std::uint8_t y_mid = 128;
  constexpr std::uint8_t uv_mid = 128;
  constexpr std::array<std::uint8_t, 8> alpha_anchors{0, 1, 31, 64, 127, 128, 192, 255};
  for (std::size_t row = 0; row < y_plane.height(); ++row) {
    for (std::size_t column = 0; column < y_plane.width(); ++column) {
      const auto anchor = (column + row * 3) % 12;
      switch (anchor) {
        case 0:
          y_plane.row(row)[column] = y_mid;
          u_plane.row(row)[column] = uv_mid;
          v_plane.row(row)[column] = uv_mid;
          break;
        case 1:
          y_plane.row(row)[column] = y_low;
          u_plane.row(row)[column] = uv_mid;
          v_plane.row(row)[column] = uv_mid;
          break;
        case 2:
          y_plane.row(row)[column] = y_high;
          u_plane.row(row)[column] = uv_mid;
          v_plane.row(row)[column] = uv_mid;
          break;
        case 3:
          y_plane.row(row)[column] = y_mid;
          u_plane.row(row)[column] = uv_low;
          v_plane.row(row)[column] = uv_mid;
          break;
        case 4:
          y_plane.row(row)[column] = y_mid;
          u_plane.row(row)[column] = uv_high;
          v_plane.row(row)[column] = uv_mid;
          break;
        case 5:
          y_plane.row(row)[column] = y_mid;
          u_plane.row(row)[column] = uv_mid;
          v_plane.row(row)[column] = uv_low;
          break;
        case 6:
          y_plane.row(row)[column] = y_mid;
          u_plane.row(row)[column] = uv_mid;
          v_plane.row(row)[column] = uv_high;
          break;
        case 7:
          y_plane.row(row)[column] = y_high;
          u_plane.row(row)[column] = uv_high;
          v_plane.row(row)[column] = uv_high;
          break;
        case 8:
          y_plane.row(row)[column] = y_low;
          u_plane.row(row)[column] = uv_low;
          v_plane.row(row)[column] = uv_low;
          break;
        case 9:
          y_plane.row(row)[column] = y_mid;
          u_plane.row(row)[column] = uv_low;
          v_plane.row(row)[column] = uv_high;
          break;
        case 10:
          y_plane.row(row)[column] = y_mid;
          u_plane.row(row)[column] = uv_high;
          v_plane.row(row)[column] = uv_low;
          break;
        default:
          y_plane.row(row)[column] = 64;
          u_plane.row(row)[column] = 96;
          v_plane.row(row)[column] = 192;
          break;
      }
      alpha_plane.row(row)[column] = alpha_anchors[(column + row * 5) % alpha_anchors.size()];
    }
  }
}

inline void make_packed_yv24_rgb_reference(PlaneView<const std::uint8_t> y_plane,
                                           PlaneView<const std::uint8_t> u_plane,
                                           PlaneView<const std::uint8_t> v_plane,
                                           PlaneView<const std::uint8_t> alpha_plane,
                                           PlaneView<std::uint8_t> destination, int pixel_step,
                                           bool has_alpha, int matrix, bool source_full,
                                           bool destination_full) {
  const auto coefficients =
      make_packed_yuv_to_rgb_coefficients(matrix, source_full, destination_full);
  for (std::size_t source_row = 0; source_row < y_plane.height(); ++source_row) {
    auto* destination_row = destination.row(destination.height() - 1 - source_row);
    for (std::size_t column = 0; column < y_plane.width(); ++column) {
      destination_row[column * pixel_step + 0] = packed_yuv_to_rgb_reference_component(
          y_plane.row(source_row)[column], u_plane.row(source_row)[column],
          v_plane.row(source_row)[column], coefficients.y_b, coefficients.u_b, coefficients.v_b,
          coefficients);
      destination_row[column * pixel_step + 1] = packed_yuv_to_rgb_reference_component(
          y_plane.row(source_row)[column], u_plane.row(source_row)[column],
          v_plane.row(source_row)[column], coefficients.y_g, coefficients.u_g, coefficients.v_g,
          coefficients);
      destination_row[column * pixel_step + 2] = packed_yuv_to_rgb_reference_component(
          y_plane.row(source_row)[column], u_plane.row(source_row)[column],
          v_plane.row(source_row)[column], coefficients.y_r, coefficients.u_r, coefficients.v_r,
          coefficients);
      if (pixel_step == 4) {
        destination_row[column * pixel_step + 3] =
            has_alpha ? alpha_plane.row(source_row)[column] : 255;
      }
    }
  }
}

inline void call_packed_yv24_rgb_kernel(const PackedYv24RgbCase& test_case, BYTE* destination,
                                        const BYTE* y_plane, const BYTE* u_plane,
                                        const BYTE* v_plane, const BYTE* alpha_plane,
                                        const ConversionMatrix& matrix) {
  const auto call = [&](auto function) {
    function(destination, y_plane, u_plane, v_plane, alpha_plane, test_case.destination_pitch,
             test_case.y_pitch, test_case.uv_pitch, test_case.alpha_pitch, test_case.width,
             test_case.height, matrix);
  };

  if (test_case.pixel_step == 3) {
    if (test_case.variant.function == PackedYv24RgbVariant::Sse2) {
      if (test_case.has_alpha) {
        call(convert_yv24_to_rgb_sse2<3, true>);
      } else {
        call(convert_yv24_to_rgb_sse2<3, false>);
      }
    } else if (test_case.variant.function == PackedYv24RgbVariant::Ssse3) {
      if (test_case.has_alpha) {
        call(convert_yv24_to_rgb_ssse3<3, true>);
      } else {
        call(convert_yv24_to_rgb_ssse3<3, false>);
      }
    } else if (test_case.has_alpha) {
      call(convert_yv24_to_rgb_avx2<3, true>);
    } else {
      call(convert_yv24_to_rgb_avx2<3, false>);
    }
    return;
  }

  if (test_case.variant.function == PackedYv24RgbVariant::Sse2) {
    if (test_case.has_alpha) {
      call(convert_yv24_to_rgb_sse2<4, true>);
    } else {
      call(convert_yv24_to_rgb_sse2<4, false>);
    }
  } else if (test_case.variant.function == PackedYv24RgbVariant::Ssse3) {
    if (test_case.has_alpha) {
      call(convert_yv24_to_rgb_ssse3<4, true>);
    } else {
      call(convert_yv24_to_rgb_ssse3<4, false>);
    }
  } else if (test_case.has_alpha) {
    call(convert_yv24_to_rgb_avx2<4, true>);
  } else {
    call(convert_yv24_to_rgb_avx2<4, false>);
  }
}

inline void run_packed_yv24_rgb_case(const PackedYv24RgbCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> y_plane(test_case.width, test_case.height, test_case.y_pitch,
                                           64);
  GuardedVideoBuffer<std::uint8_t> u_plane(test_case.width, test_case.height, test_case.uv_pitch,
                                           64);
  GuardedVideoBuffer<std::uint8_t> v_plane(test_case.width, test_case.height, test_case.uv_pitch,
                                           64);
  GuardedVideoBuffer<std::uint8_t> alpha_plane(test_case.width, test_case.height,
                                               test_case.alpha_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected(
      test_case.width * static_cast<std::size_t>(test_case.pixel_step), test_case.height,
      test_case.destination_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual(
      test_case.width * static_cast<std::size_t>(test_case.pixel_step), test_case.height,
      test_case.destination_pitch, 64);
  fill_yv24_channel_anchors(y_plane.view(), u_plane.view(), v_plane.view(), alpha_plane.view(),
                            test_case.source_full);
  const auto y_snapshot = y_plane.snapshot_active();
  const auto u_snapshot = u_plane.snapshot_active();
  const auto v_snapshot = v_plane.snapshot_active();
  const auto alpha_snapshot = alpha_plane.snapshot_active();
  make_packed_yv24_rgb_reference(y_plane.view().as_const(), u_plane.view().as_const(),
                                 v_plane.view().as_const(), alpha_plane.view().as_const(),
                                 expected.view(), test_case.pixel_step, test_case.has_alpha,
                                 test_case.matrix, test_case.source_full,
                                 test_case.destination_full);

  ConversionMatrix matrix{};
  const int source_range = test_case.source_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED;
  const int destination_range =
      test_case.destination_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED;
  ASSERT_TRUE(do_BuildMatrix_Yuv2Rgb(test_case.matrix, source_range, destination_range, 13, 8,
                                     matrix));
  call_packed_yv24_rgb_kernel(
      test_case, reinterpret_cast<BYTE*>(actual.view().data()),
      reinterpret_cast<const BYTE*>(y_plane.view().data()),
      reinterpret_cast<const BYTE*>(u_plane.view().data()),
      reinterpret_cast<const BYTE*>(v_plane.view().data()),
      test_case.has_alpha ? reinterpret_cast<const BYTE*>(alpha_plane.view().data()) : nullptr,
      matrix);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name;
  EXPECT_EQ(format_hash(hash_active(actual.view().as_const())), test_case.expected_hash)
      << test_case.name;
  EXPECT_TRUE(y_plane.active_matches(y_snapshot)) << test_case.name;
  EXPECT_TRUE(u_plane.active_matches(u_snapshot)) << test_case.name;
  EXPECT_TRUE(v_plane.active_matches(v_snapshot)) << test_case.name;
  EXPECT_TRUE(alpha_plane.active_matches(alpha_snapshot)) << test_case.name;
  EXPECT_TRUE(y_plane.memory_intact()) << test_case.name;
  EXPECT_TRUE(u_plane.memory_intact()) << test_case.name;
  EXPECT_TRUE(v_plane.memory_intact()) << test_case.name;
  EXPECT_TRUE(alpha_plane.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.memory_intact()) << test_case.name;
}

}  // namespace avsut::test
