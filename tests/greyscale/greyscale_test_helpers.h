#pragma once

#include "filters/intel/greyscale_sse.h"

#include "convert/convert_matrix.h"
#include "convert/convert_helper.h"
#include "support/compat_375.h"

#include "support/comparators.h"
#include "support/deterministic_data.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace avsut::test {

using GreyscaleYuy2FuncPtr = void (*)(BYTE*, std::size_t, std::size_t, std::size_t);
using GreyscaleRgb32FuncPtr = void (*)(BYTE*, std::size_t, std::size_t, std::size_t,
                                       ConversionMatrix&);
using GreyscaleRgb64FuncPtr = void (*)(BYTE*, std::size_t, std::size_t, std::size_t,
                                       ConversionMatrix&);

struct GreyscaleYuy2Case {
  std::size_t width_bytes{};
  std::size_t height{};
  std::size_t pitch{};
  Variant<GreyscaleYuy2FuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

struct GreyscaleRgb32Case {
  std::string matrix_name;
  std::string range_name;
  int matrix_id{};
  int color_range{};
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t pitch{};
  ConversionMatrix matrix{};
  Variant<GreyscaleRgb32FuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

struct GreyscaleRgb64Case {
  std::string matrix_name;
  std::string range_name;
  int matrix_id{};
  int color_range{};
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t pitch{};
  ConversionMatrix matrix{};
  Variant<GreyscaleRgb64FuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

template <typename Function>
inline std::string greyscale_variant_name(const Variant<Function>& variant) {
  std::string result = "Variant";
  bool capitalize = true;
  for (const char character : variant.name) {
    if (character == '_' || character == '-' || character == '.') {
      capitalize = true;
      continue;
    }
    result.push_back(capitalize && character >= 'a' && character <= 'z'
                         ? static_cast<char>(character - ('a' - 'A'))
                         : character);
    capitalize = false;
  }
  return result;
}

inline ConversionMatrix build_greyscale_matrix(int matrix_id, int color_range, int bits_per_pixel) {
  ConversionMatrix matrix{};
  if (!do_BuildMatrix_Rgb2Yuv(matrix_id, color_range, color_range, 15, bits_per_pixel, matrix)) {
    throw std::invalid_argument("unsupported Greyscale matrix fixture");
  }
  return matrix;
}

inline std::string greyscale_yuy2_case_name(std::size_t width_bytes, std::size_t height,
                                            std::size_t pitch,
                                            const Variant<GreyscaleYuy2FuncPtr>& variant,
                                            std::uint32_t seed) {
  std::ostringstream stream;
  stream << "Yuy2_Width" << width_bytes << "Bytes_Height" << height << "_Pitch" << pitch;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternNeutralChroma_" : "_PatternFixedRandom_")
         << greyscale_variant_name(variant);
  return stream.str();
}

template <typename Function>
inline std::string greyscale_rgb_case_name(const char* format, const std::string& matrix_name,
                                           const std::string& range_name, std::size_t width_pixels,
                                           std::size_t height, std::size_t pitch,
                                           const Variant<Function>& variant,
                                           std::uint32_t seed) {
  std::ostringstream stream;
  stream << format << "_" << matrix_name << "_" << range_name << "_Width" << width_pixels
         << "_Height" << height << "_Pitch" << pitch;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternMatrixLuma_" : "_PatternFixedRandom_")
         << greyscale_variant_name(variant);
  return stream.str();
}

inline GreyscaleYuy2Case make_greyscale_yuy2_case(std::size_t width_bytes, std::size_t height,
                                                  std::size_t pitch,
                                                  Variant<GreyscaleYuy2FuncPtr> variant,
                                                  std::string expected_hash,
                                                  std::uint32_t seed = 0) {
  GreyscaleYuy2Case result{width_bytes, height, pitch, std::move(variant), std::move(expected_hash),
                           seed, {}};
  result.name =
      greyscale_yuy2_case_name(result.width_bytes, result.height, result.pitch, result.variant,
                               result.seed);
  return result;
}

inline GreyscaleRgb32Case make_greyscale_rgb32_case(std::string matrix_name, std::string range_name,
                                                    int matrix_id, int color_range,
                                                    std::size_t width_pixels, std::size_t height,
                                                    std::size_t pitch,
                                                    Variant<GreyscaleRgb32FuncPtr> variant,
                                                    std::string expected_hash,
                                                    std::uint32_t seed = 0) {
  GreyscaleRgb32Case result{std::move(matrix_name),
                            std::move(range_name),
                            matrix_id,
                            color_range,
                            width_pixels,
                            height,
                            pitch,
                            build_greyscale_matrix(matrix_id, color_range, 8),
                            std::move(variant),
                            std::move(expected_hash),
                            seed,
                            {}};
  result.name = greyscale_rgb_case_name("Rgb32", result.matrix_name, result.range_name,
                                        result.width_pixels, result.height, result.pitch,
                                        result.variant, result.seed);
  return result;
}

inline GreyscaleRgb64Case make_greyscale_rgb64_case(std::string matrix_name, std::string range_name,
                                                    int matrix_id, int color_range,
                                                    std::size_t width_pixels, std::size_t height,
                                                    std::size_t pitch,
                                                    Variant<GreyscaleRgb64FuncPtr> variant,
                                                    std::string expected_hash,
                                                    std::uint32_t seed = 0) {
  GreyscaleRgb64Case result{std::move(matrix_name),
                            std::move(range_name),
                            matrix_id,
                            color_range,
                            width_pixels,
                            height,
                            pitch,
                            build_greyscale_matrix(matrix_id, color_range, 16),
                            std::move(variant),
                            std::move(expected_hash),
                            seed,
                            {}};
  result.name = greyscale_rgb_case_name("Rgb64", result.matrix_name, result.range_name,
                                        result.width_pixels, result.height, result.pitch,
                                        result.variant, result.seed);
  return result;
}

inline void PrintTo(const GreyscaleYuy2Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const GreyscaleRgb32Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const GreyscaleRgb64Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_greyscale_yuy2_input(PlaneView<std::uint8_t> view) {
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      view.row(y)[x] = static_cast<std::uint8_t>(7 + y * 29 + x * 13);
    }
  }
}

inline void apply_greyscale_yuy2_reference(PlaneView<const std::uint8_t> source,
                                           PlaneView<std::uint8_t> destination) {
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < source.width(); ++x) {
      destination.row(y)[x] = (x & 1U) == 0 ? source.row(y)[x] : 128;
    }
  }
}

inline void run_greyscale_yuy2_case(const GreyscaleYuy2Case& test_case) {
  GuardedVideoBuffer<std::uint8_t> actual(test_case.width_bytes, test_case.height, test_case.pitch,
                                          32);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_bytes, test_case.height,
                                            test_case.pitch, 32);
  if (test_case.seed == 0) {
    fill_greyscale_yuy2_input(actual.view());
  } else {
    fill_random(actual.view(), test_case.seed);
  }
  apply_greyscale_yuy2_reference(actual.view().as_const(), expected.view());

  test_case.variant.function(actual.view().data(), test_case.width_bytes, test_case.height,
                             test_case.pitch);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
}

template <typename T>
void fill_greyscale_rgb_input(PlaneView<T> view, std::size_t width_pixels, int color_range,
                              int bits_per_pixel) {
  constexpr std::size_t kComponents = 4;
  const auto full_max = (std::uint32_t{1} << bits_per_pixel) - 1U;
  const auto shift = bits_per_pixel - 8;
  const auto range_min =
      color_range == ColorRange_Compat_e::AVS_COLORRANGE_LIMITED ? (16U << shift) : 0U;
  const auto range_max =
      color_range == ColorRange_Compat_e::AVS_COLORRANGE_LIMITED ? (235U << shift) : full_max;
  const auto span = range_max - range_min + 1U;
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto pixel_index = y * width_pixels + x;
      for (std::size_t channel = 0; channel < 3; ++channel) {
        const auto value =
            range_min + (static_cast<std::uint32_t>(pixel_index * 37 + channel * 71 + 11) % span);
        view.row(y)[x * kComponents + channel] = static_cast<T>(value);
      }
      const auto alpha = bits_per_pixel == 8
                             ? (0x40U + static_cast<std::uint32_t>(pixel_index * 19)) & 0xFFU
                             : (0x9000U + static_cast<std::uint32_t>(pixel_index * 193)) & 0xFFFFU;
      view.row(y)[x * kComponents + 3] = static_cast<T>(alpha);
    }
  }
}

template <typename T>
void apply_greyscale_rgb_reference(PlaneView<T> view, std::size_t width_pixels,
                                   const ConversionMatrix& matrix) {
  const bool has_offset_rgb = matrix.offset_rgb != 0;
  const auto rounder_and_luma_offset =
      static_cast<std::int64_t>(1 << 14) + (static_cast<std::int64_t>(matrix.offset_y) << 15);
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      auto* pixel = view.row(y) + x * 4;
      std::int64_t b = pixel[0];
      std::int64_t g = pixel[1];
      std::int64_t r = pixel[2];
      if (has_offset_rgb) {
        b += matrix.offset_rgb;
        g += matrix.offset_rgb;
        r += matrix.offset_rgb;
      }
      const auto luma =
          (matrix.y_b * b + matrix.y_g * g + matrix.y_r * r + rounder_and_luma_offset) >> 15;
      pixel[0] = pixel[1] = pixel[2] = static_cast<T>(luma);
    }
  }
}

template <typename T>
void copy_active_greyscale(PlaneView<const T> source, PlaneView<T> destination);

template <typename T, typename Case>
void run_greyscale_rgb_case(const Case& test_case) {
  const auto source_width = test_case.width_pixels * 4;
  GuardedVideoBuffer<T> actual(source_width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<T> expected(source_width, test_case.height, test_case.pitch, 32);
  if (test_case.seed == 0) {
    fill_greyscale_rgb_input(actual.view(), test_case.width_pixels, test_case.color_range,
                             sizeof(T) == 1 ? 8 : 16);
  } else {
    fill_random(actual.view(), test_case.seed);
  }
  copy_active_greyscale(actual.view().as_const(), expected.view());
  apply_greyscale_rgb_reference(expected.view(), test_case.width_pixels, test_case.matrix);
  auto matrix = test_case.matrix;

  test_case.variant.function(reinterpret_cast<BYTE*>(actual.view().data()), test_case.width_pixels,
                             test_case.height, test_case.pitch, matrix);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
}

template <typename T>
void copy_active_greyscale(PlaneView<const T> source, PlaneView<T> destination) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    std::copy_n(source.row(y), source.width(), destination.row(y));
  }
}

}  // namespace avsut::test
