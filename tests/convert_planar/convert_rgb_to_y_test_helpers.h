#pragma once

#include "convert/convert_matrix.h"

// This header uses AVS_UNUSED after avisynth.h undefines it.
#ifndef AVS_UNUSED
#define AVSUT_DEFINED_AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#endif
#include "convert/convert_planar.h"
#ifdef AVSUT_DEFINED_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_DEFINED_AVS_UNUSED
#endif

#include "convert/intel/convert_planar_avx2.h"
#include "convert/intel/convert_planar_sse.h"

#include "support/avisynth_environment.h"
#include "support/comparators.h"
#include "support/guarded_video_buffer.h"
#include "support/variant_registry.h"
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

enum class RgbToYVariant { C, Sse2, Avx2 };

struct RgbToYCase {
  int matrix{};
  bool source_full{};
  bool destination_full{};
  int source_bit_depth{};
  int target_bit_depth{};
  bool force_float{};
  std::size_t width{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<RgbToYVariant> variant;
  std::string name;
};

inline void PrintTo(const RgbToYCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline const char* rgb_to_y_matrix_name(int matrix) {
  if (matrix == AVS_MATRIX_BT709) {
    return "Bt709";
  }
  if (matrix == AVS_MATRIX_BT2020_NCL) {
    return "Bt2020Ncl";
  }
  return "Bt601";
}

inline std::string rgb_to_y_variant_name(const std::string& name) {
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

inline RgbToYCase make_rgb_to_y_case(int matrix, bool source_full, bool destination_full,
                                     int source_bit_depth, int target_bit_depth, bool force_float,
                                     std::size_t width, std::size_t height,
                                     std::size_t source_pitch, std::size_t destination_pitch,
                                     Variant<RgbToYVariant> variant) {
  RgbToYCase result{matrix,
                    source_full,
                    destination_full,
                    source_bit_depth,
                    target_bit_depth,
                    force_float,
                    width,
                    height,
                    source_pitch,
                    destination_pitch,
                    std::move(variant),
                    {}};
  std::ostringstream name;
  name << rgb_to_y_matrix_name(matrix) << "_Src" << (source_full ? "Full" : "Limited")
       << "_Dst" << (destination_full ? "Full" : "Limited") << "_SrcBits" << source_bit_depth
       << "_DstBits" << target_bit_depth << "_" << (force_float ? "ForceFloat" : "NativeOrConvert")
       << "_Width" << width << "_Height" << height << "_PatternRgbAndGrayAnchors_"
       << rgb_to_y_variant_name(result.variant.name);
  result.name = name.str();
  return result;
}

struct RgbToYRange {
  double offset{};
  double span{};
};

inline RgbToYRange make_rgb_to_y_range(bool full, int bit_depth) {
  if (bit_depth == 32) {
    return full ? RgbToYRange{0.0, 1.0}
                : RgbToYRange{16.0 / 255.0, 219.0 / 255.0};
  }
  const int scale = 1 << (bit_depth - 8);
  const int maximum = (1 << bit_depth) - 1;
  return full ? RgbToYRange{0.0, static_cast<double>(maximum)}
              : RgbToYRange{static_cast<double>(16 * scale), static_cast<double>(219 * scale)};
}

inline int rgb_to_y_round_symmetric(double value) {
  return static_cast<int>(value >= 0.0 ? value + 0.5 : value - 0.5);
}

inline void rgb_to_y_kr_kb(int matrix, double& kr, double& kb) {
  if (matrix == AVS_MATRIX_BT709) {
    kr = 0.2126;
    kb = 0.0722;
    return;
  }
  if (matrix == AVS_MATRIX_BT2020_NCL) {
    kr = 0.2627;
    kb = 0.0593;
    return;
  }
  kr = 0.299;
  kb = 0.114;
}

struct RgbToYLumaReference {
  double y_b{};
  double y_g{};
  double y_r{};
  int y_b_i{};
  int y_g_i{};
  int y_r_i{};
  int input_offset_i{};
  int output_offset_i{};
};

inline RgbToYLumaReference make_rgb_to_y_luma_reference(const RgbToYCase& test_case) {
  double kr{};
  double kb{};
  rgb_to_y_kr_kb(test_case.matrix, kr, kb);
  const double kg = 1.0 - kr - kb;
  const auto source_range = make_rgb_to_y_range(test_case.source_full, test_case.source_bit_depth);
  const auto destination_range =
      make_rgb_to_y_range(test_case.destination_full, test_case.target_bit_depth);
  const double scale = 32768.0;
  RgbToYLumaReference result{
      destination_range.span * kb / source_range.span,
      destination_range.span * kg / source_range.span,
      destination_range.span * kr / source_range.span,
      rgb_to_y_round_symmetric(scale * destination_range.span * kb / source_range.span),
      rgb_to_y_round_symmetric(scale * destination_range.span * kg / source_range.span),
      rgb_to_y_round_symmetric(scale * destination_range.span * kr / source_range.span),
      test_case.source_bit_depth == 32
          ? 0
          : -static_cast<int>(source_range.offset),
      test_case.source_bit_depth == 32 ? 0 : static_cast<int>(destination_range.offset)};
  if (test_case.source_bit_depth <= 16 && test_case.source_full && test_case.destination_full) {
    result.y_g_i += (1 << 15) - (result.y_b_i + result.y_g_i + result.y_r_i);
  }
  return result;
}

inline double rgb_to_y_anchor_value(const RgbToYCase& test_case, std::size_t anchor) {
  const auto range = make_rgb_to_y_range(test_case.source_full, test_case.source_bit_depth);
  const auto middle = range.offset + range.span * 0.5 +
                      (test_case.source_bit_depth == 16 ? 64.0 : 0.0);
  switch (anchor % 8) {
    case 0:
      return range.offset;
    case 1:
      return middle;
    case 2:
      return range.offset + range.span;
    case 3:
      return range.offset + range.span * 0.75;
    case 4:
      return range.offset + range.span * 0.25;
    case 5:
      return range.offset + range.span * 0.625;
    case 6:
      return range.offset + range.span * 0.375;
    default:
      return range.offset + range.span * 0.875;
  }
}

template <typename T>
inline void fill_rgb_to_y_inputs(PlaneView<T> green, PlaneView<T> blue, PlaneView<T> red,
                                 const RgbToYCase& test_case) {
  const auto range = make_rgb_to_y_range(test_case.source_full, test_case.source_bit_depth);
  const auto high = range.offset + range.span;
  const auto low = range.offset;
  const auto middle = range.offset + range.span * 0.5 +
                      (test_case.source_bit_depth == 16 ? 64.0 : 0.0);
  for (std::size_t y = 0; y < green.height(); ++y) {
    for (std::size_t x = 0; x < green.width(); ++x) {
      const std::size_t anchor = (x + y * 8) % 8;
      const auto channel = rgb_to_y_anchor_value(test_case, anchor);
      switch (anchor) {
        case 0:
          green.row(y)[x] = static_cast<T>(low);
          blue.row(y)[x] = static_cast<T>(low);
          red.row(y)[x] = static_cast<T>(low);
          break;
        case 1:
          green.row(y)[x] = static_cast<T>(middle);
          blue.row(y)[x] = static_cast<T>(middle);
          red.row(y)[x] = static_cast<T>(middle);
          break;
        case 2:
          green.row(y)[x] = static_cast<T>(high);
          blue.row(y)[x] = static_cast<T>(high);
          red.row(y)[x] = static_cast<T>(high);
          break;
        case 3:
          green.row(y)[x] = static_cast<T>(low);
          blue.row(y)[x] = static_cast<T>(low);
          red.row(y)[x] = static_cast<T>(high);
          break;
        case 4:
          green.row(y)[x] = static_cast<T>(high);
          blue.row(y)[x] = static_cast<T>(low);
          red.row(y)[x] = static_cast<T>(low);
          break;
        case 5:
          green.row(y)[x] = static_cast<T>(low);
          blue.row(y)[x] = static_cast<T>(high);
          red.row(y)[x] = static_cast<T>(low);
          break;
        case 6:
          green.row(y)[x] = static_cast<T>(middle);
          blue.row(y)[x] = static_cast<T>(low);
          red.row(y)[x] = static_cast<T>(high);
          break;
        default:
          green.row(y)[x] = static_cast<T>(channel);
          blue.row(y)[x] = static_cast<T>(high);
          red.row(y)[x] = static_cast<T>(low);
          break;
      }
    }
  }
}

inline double rgb_to_y_semantic_reference(const RgbToYCase& test_case, double green, double blue,
                                          double red) {
  double kr{};
  double kb{};
  rgb_to_y_kr_kb(test_case.matrix, kr, kb);
  const double kg = 1.0 - kr - kb;
  const auto source_range = make_rgb_to_y_range(test_case.source_full, test_case.source_bit_depth);
  const auto destination_range = make_rgb_to_y_range(test_case.destination_full,
                                                      test_case.target_bit_depth);
  const double normalized_green = (green - source_range.offset) / source_range.span;
  const double normalized_blue = (blue - source_range.offset) / source_range.span;
  const double normalized_red = (red - source_range.offset) / source_range.span;
  return (kg * normalized_green + kb * normalized_blue + kr * normalized_red) *
             destination_range.span +
         destination_range.offset;
}

inline double rgb_to_y_integer_reference(const RgbToYCase& test_case,
                                         const ConversionMatrix& matrix, std::int64_t green,
                                         std::int64_t blue, std::int64_t red) {
  const std::int64_t adjusted_green = green + matrix.offset_in;
  const std::int64_t adjusted_blue = blue + matrix.offset_in;
  const std::int64_t adjusted_red = red + matrix.offset_in;
  const std::int64_t total = static_cast<std::int64_t>(matrix.y_g) * adjusted_green +
                             static_cast<std::int64_t>(matrix.y_b) * adjusted_blue +
                             static_cast<std::int64_t>(matrix.y_r) * adjusted_red +
                             (std::int64_t{1} << 14) +
                             (static_cast<std::int64_t>(matrix.offset_out) << 15);
  const std::int64_t maximum = (std::int64_t{1} << test_case.target_bit_depth) - 1;
  return static_cast<double>(std::clamp(total >> 15, std::int64_t{0}, maximum));
}

inline double rgb_to_y_expected_sample(const RgbToYCase& test_case,
                                       const ConversionMatrix& matrix, double green, double blue,
                                       double red) {
  const bool native_integer = !test_case.force_float && test_case.source_bit_depth < 16 &&
                              test_case.source_bit_depth == test_case.target_bit_depth;
  if (native_integer) {
    return rgb_to_y_integer_reference(test_case, matrix, static_cast<std::int64_t>(green),
                                      static_cast<std::int64_t>(blue),
                                      static_cast<std::int64_t>(red));
  }
  return rgb_to_y_semantic_reference(test_case, green, blue, red);
}

template <typename T, bool LessThan16Bit>
inline void call_rgb_to_y_kernel(const RgbToYCase& test_case, BYTE* (&destination)[3],
                                 int (&destination_pitch)[3], const BYTE* (&source)[3],
                                 const int (&source_pitch)[3], const ConversionMatrix& matrix) {
  const int width = static_cast<int>(test_case.width);
  const int height = static_cast<int>(test_case.height);
  if (test_case.variant.function == RgbToYVariant::C) {
    convert_yuv_to_planarrgb_c<ConversionDirection::RGB_TO_Y, T, LessThan16Bit>(
        destination, destination_pitch, source, source_pitch, width, height, matrix,
        test_case.source_bit_depth, test_case.target_bit_depth, test_case.force_float);
  } else if (test_case.variant.function == RgbToYVariant::Sse2) {
    convert_yuv_to_planarrgb_sse2<ConversionDirection::RGB_TO_Y, T, LessThan16Bit>(
        destination, destination_pitch, source, source_pitch, width, height, matrix,
        test_case.source_bit_depth, test_case.target_bit_depth, test_case.force_float);
  } else {
    convert_yuv_to_planarrgb_avx2<ConversionDirection::RGB_TO_Y, T, LessThan16Bit>(
        destination, destination_pitch, source, source_pitch, width, height, matrix,
        test_case.source_bit_depth, test_case.target_bit_depth, test_case.force_float);
  }
}

template <typename T>
inline void call_rgb_to_y_kernel_for_source(const RgbToYCase& test_case,
                                            BYTE* (&destination)[3],
                                            int (&destination_pitch)[3],
                                            const BYTE* (&source)[3],
                                            const int (&source_pitch)[3],
                                            const ConversionMatrix& matrix) {
  if constexpr (std::is_same_v<T, std::uint8_t> || std::is_same_v<T, float>) {
    call_rgb_to_y_kernel<T, std::is_same_v<T, std::uint8_t>>(
        test_case, destination, destination_pitch, source, source_pitch, matrix);
  } else if (test_case.source_bit_depth < 16) {
    call_rgb_to_y_kernel<T, true>(test_case, destination, destination_pitch, source, source_pitch,
                                  matrix);
  } else {
    call_rgb_to_y_kernel<T, false>(test_case, destination, destination_pitch, source, source_pitch,
                                   matrix);
  }
}

template <typename T>
inline void run_rgb_to_y_case_typed(const RgbToYCase& test_case) {
  static_assert(std::is_arithmetic_v<T>);
  GuardedVideoBuffer<T> source_green(test_case.width, test_case.height, test_case.source_pitch);
  GuardedVideoBuffer<T> source_blue(test_case.width, test_case.height, test_case.source_pitch);
  GuardedVideoBuffer<T> source_red(test_case.width, test_case.height, test_case.source_pitch);

  if (test_case.target_bit_depth == 8) {
    GuardedVideoBuffer<std::uint8_t> expected(test_case.width, test_case.height,
                                              test_case.destination_pitch);
    GuardedVideoBuffer<std::uint8_t> actual(test_case.width, test_case.height,
                                            test_case.destination_pitch);
    fill_rgb_to_y_inputs(source_green.view(), source_blue.view(), source_red.view(), test_case);
    const auto green_before = source_green.snapshot_active();
    const auto blue_before = source_blue.snapshot_active();
    const auto red_before = source_red.snapshot_active();
    ConversionMatrix matrix{};
    const bool built = do_BuildMatrix_Rgb2Yuv(
        test_case.matrix, test_case.source_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED,
        test_case.destination_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED, 15,
        test_case.source_bit_depth, matrix);
    ASSERT_TRUE(built) << test_case.name;
    for (std::size_t y = 0; y < test_case.height; ++y) {
      for (std::size_t x = 0; x < test_case.width; ++x) {
        expected.view().row(y)[x] = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(rgb_to_y_expected_sample(
                                 test_case, matrix, source_green.view().row(y)[x],
                                 source_blue.view().row(y)[x], source_red.view().row(y)[x]) +
                             0.5),
            0, 255));
      }
    }
    BYTE* destination[3]{reinterpret_cast<BYTE*>(actual.view().data()), nullptr, nullptr};
    int destination_pitch[3]{static_cast<int>(test_case.destination_pitch), 0, 0};
    const BYTE* source[3]{reinterpret_cast<const BYTE*>(source_green.view().data()),
                          reinterpret_cast<const BYTE*>(source_blue.view().data()),
                          reinterpret_cast<const BYTE*>(source_red.view().data())};
    const int source_pitch[3]{static_cast<int>(test_case.source_pitch),
                              static_cast<int>(test_case.source_pitch),
                              static_cast<int>(test_case.source_pitch)};
    call_rgb_to_y_kernel_for_source<T>(test_case, destination, destination_pitch, source,
                                       source_pitch, matrix);
    EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
        << test_case.name;
    EXPECT_TRUE(source_green.active_matches(green_before)) << test_case.name;
    EXPECT_TRUE(source_blue.active_matches(blue_before)) << test_case.name;
    EXPECT_TRUE(source_red.active_matches(red_before)) << test_case.name;
    EXPECT_TRUE(source_green.memory_intact()) << test_case.name;
    EXPECT_TRUE(source_blue.memory_intact()) << test_case.name;
    EXPECT_TRUE(source_red.memory_intact()) << test_case.name;
    EXPECT_TRUE(expected.memory_intact()) << test_case.name;
    EXPECT_TRUE(actual.memory_intact()) << test_case.name;
    return;
  }

  if (test_case.target_bit_depth < 32) {
    GuardedVideoBuffer<std::uint16_t> expected(test_case.width, test_case.height,
                                               test_case.destination_pitch);
    GuardedVideoBuffer<std::uint16_t> actual(test_case.width, test_case.height,
                                             test_case.destination_pitch);
    fill_rgb_to_y_inputs(source_green.view(), source_blue.view(), source_red.view(), test_case);
    const auto green_before = source_green.snapshot_active();
    const auto blue_before = source_blue.snapshot_active();
    const auto red_before = source_red.snapshot_active();
    ConversionMatrix matrix{};
    const bool built = do_BuildMatrix_Rgb2Yuv(
        test_case.matrix, test_case.source_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED,
        test_case.destination_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED, 15,
        test_case.source_bit_depth, matrix);
    ASSERT_TRUE(built) << test_case.name;
    const auto maximum = (std::int64_t{1} << test_case.target_bit_depth) - 1;
    for (std::size_t y = 0; y < test_case.height; ++y) {
      for (std::size_t x = 0; x < test_case.width; ++x) {
        const auto value = static_cast<std::int64_t>(rgb_to_y_expected_sample(
            test_case, matrix, source_green.view().row(y)[x], source_blue.view().row(y)[x],
            source_red.view().row(y)[x]) +
                                                       0.5);
        expected.view().row(y)[x] = static_cast<std::uint16_t>(std::clamp<std::int64_t>(
            value, 0, maximum));
      }
    }
    BYTE* destination[3]{reinterpret_cast<BYTE*>(actual.view().data()), nullptr, nullptr};
    int destination_pitch[3]{static_cast<int>(test_case.destination_pitch), 0, 0};
    const BYTE* source[3]{reinterpret_cast<const BYTE*>(source_green.view().data()),
                          reinterpret_cast<const BYTE*>(source_blue.view().data()),
                          reinterpret_cast<const BYTE*>(source_red.view().data())};
    const int source_pitch[3]{static_cast<int>(test_case.source_pitch),
                              static_cast<int>(test_case.source_pitch),
                              static_cast<int>(test_case.source_pitch)};
    call_rgb_to_y_kernel_for_source<T>(test_case, destination, destination_pitch, source,
                                       source_pitch, matrix);
    EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
        << test_case.name;
    EXPECT_TRUE(source_green.active_matches(green_before)) << test_case.name;
    EXPECT_TRUE(source_blue.active_matches(blue_before)) << test_case.name;
    EXPECT_TRUE(source_red.active_matches(red_before)) << test_case.name;
    EXPECT_TRUE(source_green.memory_intact()) << test_case.name;
    EXPECT_TRUE(source_blue.memory_intact()) << test_case.name;
    EXPECT_TRUE(source_red.memory_intact()) << test_case.name;
    EXPECT_TRUE(expected.memory_intact()) << test_case.name;
    EXPECT_TRUE(actual.memory_intact()) << test_case.name;
    return;
  }

  GuardedVideoBuffer<float> expected(test_case.width, test_case.height, test_case.destination_pitch);
  GuardedVideoBuffer<float> actual(test_case.width, test_case.height, test_case.destination_pitch);
  fill_rgb_to_y_inputs(source_green.view(), source_blue.view(), source_red.view(), test_case);
  const auto green_before = source_green.snapshot_active();
  const auto blue_before = source_blue.snapshot_active();
  const auto red_before = source_red.snapshot_active();
  ConversionMatrix matrix{};
  const bool built = do_BuildMatrix_Rgb2Yuv(
      test_case.matrix, test_case.source_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED,
      test_case.destination_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED, 15,
      test_case.source_bit_depth, matrix);
  ASSERT_TRUE(built) << test_case.name;
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width; ++x) {
      expected.view().row(y)[x] = static_cast<float>(rgb_to_y_expected_sample(
          test_case, matrix, source_green.view().row(y)[x], source_blue.view().row(y)[x],
          source_red.view().row(y)[x]));
    }
  }
  BYTE* destination[3]{reinterpret_cast<BYTE*>(actual.view().data()), nullptr, nullptr};
  int destination_pitch[3]{static_cast<int>(test_case.destination_pitch), 0, 0};
  const BYTE* source[3]{reinterpret_cast<const BYTE*>(source_green.view().data()),
                        reinterpret_cast<const BYTE*>(source_blue.view().data()),
                        reinterpret_cast<const BYTE*>(source_red.view().data())};
  const int source_pitch[3]{static_cast<int>(test_case.source_pitch),
                            static_cast<int>(test_case.source_pitch),
                            static_cast<int>(test_case.source_pitch)};
  call_rgb_to_y_kernel_for_source<T>(test_case, destination, destination_pitch, source, source_pitch,
                                     matrix);
  EXPECT_TRUE(compare_float(expected.view().as_const(), actual.view().as_const(),
                            FloatTolerance{2.0e-5F, 2.0e-5F}))
      << test_case.name;
  EXPECT_TRUE(source_green.active_matches(green_before)) << test_case.name;
  EXPECT_TRUE(source_blue.active_matches(blue_before)) << test_case.name;
  EXPECT_TRUE(source_red.active_matches(red_before)) << test_case.name;
  EXPECT_TRUE(source_green.memory_intact()) << test_case.name;
  EXPECT_TRUE(source_blue.memory_intact()) << test_case.name;
  EXPECT_TRUE(source_red.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.memory_intact()) << test_case.name;
}

inline void run_rgb_to_y_case(const RgbToYCase& test_case) {
  if (test_case.source_bit_depth == 8) {
    run_rgb_to_y_case_typed<std::uint8_t>(test_case);
  } else if (test_case.source_bit_depth < 32) {
    run_rgb_to_y_case_typed<std::uint16_t>(test_case);
  } else {
    run_rgb_to_y_case_typed<float>(test_case);
  }
}

struct PublicRgbToYCase {
  int pixel_type{};
  int matrix{AVS_MATRIX_BT709};
  bool source_full{};
  bool destination_full{};
  int source_bit_depth{};
  int target_bit_depth{};
  bool quality{};
  std::size_t width{};
  std::size_t height{};
  std::string name;
};

inline void PrintTo(const PublicRgbToYCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline PublicRgbToYCase make_public_rgb_to_y_case(int pixel_type, int source_bit_depth,
                                                   int target_bit_depth, bool source_full,
                                                   bool destination_full, bool quality,
                                                   std::size_t width, std::size_t height) {
  PublicRgbToYCase result{pixel_type,
                          AVS_MATRIX_BT709,
                          source_full,
                          destination_full,
                          source_bit_depth,
                          target_bit_depth,
                          quality,
                          width,
                          height,
                          {}};
  std::ostringstream name;
  name << "RgbP" << source_bit_depth << "ToY_Src" << (source_full ? "Full" : "Limited")
       << "_Dst" << (destination_full ? "Full" : "Limited") << "_TargetBits"
       << target_bit_depth << "_" << (quality ? "QualityFloat" : "QualityInteger") << "_Width"
       << width << "_Height" << height << "_PatternRgbAndGrayAnchors_Dispatch";
  result.name = name.str();
  return result;
}

inline RgbToYCase as_reference_case(const PublicRgbToYCase& test_case) {
  return make_rgb_to_y_case(
      test_case.matrix, test_case.source_full, test_case.destination_full,
      test_case.source_bit_depth, test_case.target_bit_depth, test_case.quality, test_case.width,
      test_case.height, 0, 0,
      Variant<RgbToYVariant>{"c", RgbToYVariant::C, IsaRequirement::Scalar});
}

template <typename T>
inline void fill_public_rgb_input(PVideoFrame& frame, const RgbToYCase& test_case) {
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(frame, 0xA0 + plane, plane);
  }
  write_frame_plane<T>(frame, PLANAR_G, [&test_case](int x, int y) {
    const auto anchor = static_cast<std::size_t>((x + y * 8) % 8);
    const auto range = make_rgb_to_y_range(test_case.source_full, test_case.source_bit_depth);
    const auto low = range.offset;
    const auto high = range.offset + range.span;
    const auto middle = range.offset + range.span * 0.5 +
                        (test_case.source_bit_depth == 16 ? 64.0 : 0.0);
    if (anchor == 3 || anchor == 5) {
      return low;
    }
    if (anchor == 4) {
      return high;
    }
    return anchor == 7 ? middle : (anchor == 0 ? low : (anchor == 1 ? middle : high));
  });
  write_frame_plane<T>(frame, PLANAR_B, [&test_case](int x, int y) {
    const auto anchor = static_cast<std::size_t>((x + y * 8) % 8);
    const auto range = make_rgb_to_y_range(test_case.source_full, test_case.source_bit_depth);
    const auto low = range.offset;
    const auto high = range.offset + range.span;
    const auto middle = range.offset + range.span * 0.5 +
                        (test_case.source_bit_depth == 16 ? 64.0 : 0.0);
    if (anchor == 3 || anchor == 4 || anchor == 7) {
      return low;
    }
    if (anchor == 5) {
      return high;
    }
    return anchor == 0 ? low : (anchor == 1 ? middle : high);
  });
  write_frame_plane<T>(frame, PLANAR_R, [&test_case](int x, int y) {
    const auto anchor = static_cast<std::size_t>((x + y * 8) % 8);
    const auto range = make_rgb_to_y_range(test_case.source_full, test_case.source_bit_depth);
    const auto low = range.offset;
    const auto high = range.offset + range.span;
    const auto middle = range.offset + range.span * 0.5 +
                        (test_case.source_bit_depth == 16 ? 64.0 : 0.0);
    if (anchor == 3) {
      return high;
    }
    if (anchor == 4 || anchor == 5 || anchor == 6) {
      return low;
    }
    return anchor == 1 ? middle : high;
  });
}

template <typename SourceT, typename DestinationT>
inline void check_public_rgb_to_y_output(const PVideoFrame& output, const PVideoFrame& source,
                                         const PublicRgbToYCase& public_case,
                                         const RgbToYCase& reference_case,
                                         const ConversionMatrix& matrix) {
  const int width = output->GetRowSize(PLANAR_Y) / static_cast<int>(sizeof(DestinationT));
  ASSERT_EQ(width, static_cast<int>(public_case.width)) << public_case.name;
  ASSERT_EQ(output->GetHeight(PLANAR_Y), static_cast<int>(public_case.height)) << public_case.name;
  const auto* green = reinterpret_cast<const SourceT*>(source->GetReadPtr(PLANAR_G));
  const auto* blue = reinterpret_cast<const SourceT*>(source->GetReadPtr(PLANAR_B));
  const auto* red = reinterpret_cast<const SourceT*>(source->GetReadPtr(PLANAR_R));
  const int source_pitch = source->GetPitch(PLANAR_G) / static_cast<int>(sizeof(SourceT));
  const int destination_pitch = output->GetPitch(PLANAR_Y) / static_cast<int>(sizeof(DestinationT));
  const auto* actual = reinterpret_cast<const DestinationT*>(output->GetReadPtr(PLANAR_Y));
  for (int y = 0; y < static_cast<int>(public_case.height); ++y) {
    for (int x = 0; x < width; ++x) {
      const double expected = rgb_to_y_expected_sample(
          reference_case, matrix, green[y * source_pitch + x], blue[y * source_pitch + x],
          red[y * source_pitch + x]);
      if constexpr (std::is_floating_point_v<DestinationT>) {
        EXPECT_NEAR(actual[y * destination_pitch + x], expected, 2.0e-5)
            << public_case.name << " row=" << y << " column=" << x;
      } else {
        const auto rounded = static_cast<std::int64_t>(expected + 0.5);
        EXPECT_EQ(actual[y * destination_pitch + x],
                  static_cast<DestinationT>(std::clamp<std::int64_t>(
                      rounded, 0, (std::int64_t{1} << public_case.target_bit_depth) - 1)))
            << public_case.name << " row=" << y << " column=" << x;
      }
    }
  }
}

template <typename SourceT, typename DestinationT>
inline void run_public_rgb_to_y_case_typed(const PublicRgbToYCase& public_case) {
  AviSynthEnvironment environment;
  const auto source_vi = make_video_info(VideoInfoSpec{
      static_cast<int>(public_case.width), static_cast<int>(public_case.height),
      public_case.pixel_type,
      1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  const auto reference_case = as_reference_case(public_case);
  fill_public_rgb_input<SourceT>(source_frame, reference_case);
  if (!public_case.source_full) {
    AVSMap* properties = environment.get()->getFramePropsRW(source_frame);
    ASSERT_NE(properties, nullptr) << public_case.name;
    ASSERT_EQ(environment.get()->propSetInt(properties, "_ColorRange", AVS_COLORRANGE_LIMITED,
                                             AVSPropAppendMode::PROPAPPENDMODE_REPLACE),
              0)
        << public_case.name;
  }
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);
  bool bitdepth_converted = false;
  const std::string matrix_name = public_case.destination_full ? "709:full" : "709:limited";
  ConvertRGBToYUV444 filter(source, matrix_name.c_str(), false, public_case.target_bit_depth,
                            public_case.quality, bitdepth_converted, true, environment.get());
  ASSERT_TRUE(bitdepth_converted) << public_case.name;
  ASSERT_TRUE(filter.GetVideoInfo().IsY()) << public_case.name;
  ASSERT_EQ(filter.GetVideoInfo().BitsPerComponent(), public_case.target_bit_depth)
      << public_case.name;
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ConversionMatrix matrix{};
  ASSERT_TRUE(do_BuildMatrix_Rgb2Yuv(
      public_case.matrix,
      public_case.source_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED,
      public_case.destination_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED, 15,
      public_case.source_bit_depth, matrix))
      << public_case.name;
  check_public_rgb_to_y_output<SourceT, DestinationT>(output, source_frame, public_case,
                                                       reference_case, matrix);
  const AVSMap* output_props = environment.get()->getFramePropsRO(output);
  ASSERT_NE(output_props, nullptr) << public_case.name;
  int error = 0;
  EXPECT_EQ(environment.get()->propGetInt(output_props, "_ColorRange", 0, &error),
            public_case.destination_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED)
      << public_case.name;
  EXPECT_EQ(error, 0) << public_case.name;
  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>({0, 0})) << public_case.name;
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER) << public_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before) << public_case.name;
}

inline void run_public_rgb_to_y_case(const PublicRgbToYCase& public_case) {
  if (public_case.source_bit_depth == 8) {
    if (public_case.target_bit_depth == 8) {
      run_public_rgb_to_y_case_typed<std::uint8_t, std::uint8_t>(public_case);
    } else if (public_case.target_bit_depth < 32) {
      run_public_rgb_to_y_case_typed<std::uint8_t, std::uint16_t>(public_case);
    } else {
      run_public_rgb_to_y_case_typed<std::uint8_t, float>(public_case);
    }
  } else if (public_case.source_bit_depth == 10 || public_case.source_bit_depth == 16) {
    if (public_case.target_bit_depth == 8) {
      run_public_rgb_to_y_case_typed<std::uint16_t, std::uint8_t>(public_case);
    } else if (public_case.target_bit_depth < 32) {
      run_public_rgb_to_y_case_typed<std::uint16_t, std::uint16_t>(public_case);
    } else {
      run_public_rgb_to_y_case_typed<std::uint16_t, float>(public_case);
    }
  } else if (public_case.target_bit_depth == 32) {
    run_public_rgb_to_y_case_typed<float, float>(public_case);
  } else if (public_case.target_bit_depth == 8) {
    run_public_rgb_to_y_case_typed<float, std::uint8_t>(public_case);
  } else {
    run_public_rgb_to_y_case_typed<float, std::uint16_t>(public_case);
  }
}

}  // namespace avsut::test
