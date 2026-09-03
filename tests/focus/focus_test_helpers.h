#pragma once

#include "filters/intel/focus_avx2.h"
#include "filters/intel/focus_sse.h"

#include "support/comparators.h"
#include "support/deterministic_data.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace avsut::test {

using FocusHorizontal8FuncPtr = void (*)(BYTE*, std::size_t, std::size_t, std::size_t, std::size_t);
using FocusHorizontal16FuncPtr = void (*)(BYTE*, std::size_t, std::size_t, std::size_t, std::size_t,
                                          int);
using FocusVertical8FuncPtr = void (*)(BYTE*, BYTE*, int, int, int, int);
using FocusVertical16FuncPtr = void (*)(BYTE*, BYTE*, int, int, int, int);

struct FocusHorizontal8Case {
  std::size_t width{};
  std::size_t height{};
  std::size_t pitch{};
  std::size_t amount{};
  Variant<FocusHorizontal8FuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

struct FocusHorizontal16Case {
  std::size_t width{};
  std::size_t height{};
  std::size_t pitch{};
  std::size_t amount{};
  int bits_per_pixel{};
  Variant<FocusHorizontal16FuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

struct FocusVertical8Case {
  std::size_t width{};
  std::size_t height{};
  std::size_t pitch{};
  std::size_t amount{};
  Variant<FocusVertical8FuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

struct FocusVertical16Case {
  std::size_t width{};
  std::size_t height{};
  std::size_t pitch{};
  std::size_t amount{};
  Variant<FocusVertical16FuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

template <typename Function>
inline std::string focus_variant_name(const Variant<Function>& variant) {
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

template <typename Function>
inline std::string focus_case_name(const char* format, std::size_t width, std::size_t height,
                                   std::size_t pitch, std::size_t amount,
                                   const Variant<Function>& variant, std::uint32_t seed) {
  std::ostringstream stream;
  stream << format << "_Width" << width << "_Height" << height << "_Pitch" << pitch << "_Amount"
         << amount;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternBoundaryRamp_" : "_PatternFixedRandom_")
         << focus_variant_name(variant);
  return stream.str();
}

inline std::string focus_horizontal16_case_name(std::size_t width, std::size_t height,
                                                std::size_t pitch, std::size_t amount,
                                                int bits_per_pixel,
                                                const Variant<FocusHorizontal16FuncPtr>& variant,
                                                std::uint32_t seed) {
  std::ostringstream stream;
  stream << "Plane16Horizontal_Width" << width << "_Height" << height << "_Pitch" << pitch
         << "_Amount" << amount << "_Bits" << bits_per_pixel;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternBoundaryRamp_" : "_PatternFixedRandom_")
         << focus_variant_name(variant);
  return stream.str();
}

inline FocusHorizontal8Case make_focus_horizontal8_case(std::size_t width, std::size_t height,
                                                        std::size_t pitch, std::size_t amount,
                                                        Variant<FocusHorizontal8FuncPtr> variant,
                                                        std::string expected_hash,
                                                        std::uint32_t seed = 0) {
  FocusHorizontal8Case result{
      width, height, pitch, amount, std::move(variant), std::move(expected_hash), seed, {}};
  result.name = focus_case_name("Plane8Horizontal", result.width, result.height, result.pitch,
                                result.amount, result.variant, result.seed);
  return result;
}

inline FocusHorizontal16Case make_focus_horizontal16_case(std::size_t width, std::size_t height,
                                                          std::size_t pitch, std::size_t amount,
                                                          int bits_per_pixel,
                                                          Variant<FocusHorizontal16FuncPtr> variant,
                                                          std::string expected_hash,
                                                          std::uint32_t seed = 0) {
  FocusHorizontal16Case result{
      width, height, pitch, amount, bits_per_pixel, std::move(variant), std::move(expected_hash),
      seed, {}};
  result.name = focus_horizontal16_case_name(result.width, result.height, result.pitch,
                                             result.amount, result.bits_per_pixel, result.variant,
                                             result.seed);
  return result;
}

inline FocusVertical8Case make_focus_vertical8_case(std::size_t width, std::size_t height,
                                                    std::size_t pitch, std::size_t amount,
                                                    Variant<FocusVertical8FuncPtr> variant,
                                                    std::string expected_hash,
                                                    std::uint32_t seed = 0) {
  FocusVertical8Case result{
      width, height, pitch, amount, std::move(variant), std::move(expected_hash), seed, {}};
  result.name = focus_case_name("Plane8Vertical", result.width, result.height, result.pitch,
                                result.amount, result.variant, result.seed);
  return result;
}

inline FocusVertical16Case make_focus_vertical16_case(std::size_t width, std::size_t height,
                                                      std::size_t pitch, std::size_t amount,
                                                      Variant<FocusVertical16FuncPtr> variant,
                                                      std::string expected_hash,
                                                      std::uint32_t seed = 0) {
  FocusVertical16Case result{
      width, height, pitch, amount, std::move(variant), std::move(expected_hash), seed, {}};
  result.name = focus_case_name("Plane16Vertical", result.width, result.height, result.pitch,
                                result.amount, result.variant, result.seed);
  return result;
}

inline void PrintTo(const FocusHorizontal8Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const FocusHorizontal16Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const FocusVertical8Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const FocusVertical16Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_focus_input(PlaneView<T> view, std::uint32_t seed = 0) {
  static_assert(std::is_integral_v<T>);
  if (seed != 0) {
    fill_random(view, seed);
    return;
  }
  const auto max_value =
      static_cast<std::uint32_t>(std::numeric_limits<std::remove_const_t<T>>::max());
  const std::array<std::uint32_t, 10> anchors{0U,
                                              1U,
                                              max_value / 4U,
                                              max_value / 2U,
                                              max_value - 1U,
                                              max_value,
                                              17U,
                                              max_value / 3U,
                                              (max_value * 3U) / 4U,
                                              max_value > 31U ? max_value - 31U : max_value};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      const auto index = y * view.width() + x;
      if ((index % 7U) == 0U) {
        view.row(y)[x] = static_cast<T>(anchors[(index / 7U) % anchors.size()]);
      } else {
        const auto ramp =
            static_cast<std::uint32_t>((x * 37U + y * 101U + index * 13U) % (max_value + 1U));
        view.row(y)[x] = static_cast<T>(ramp);
      }
    }
  }
}

inline std::int64_t focus_floor_shift7(std::int64_t value) {
  if (value >= 0) {
    return value / 128;
  }
  return -(((-value) + 127) / 128);
}

template <typename T>
T focus_reference_pixel(T left, T center, T right, std::size_t amount) {
  const auto t = static_cast<std::int64_t>((amount + 256U) >> 9U);
  const auto numerator = static_cast<std::int64_t>(center) * (2 * t) +
                         (static_cast<std::int64_t>(left) + right) * (64 - t) + 64;
  const auto rounded = focus_floor_shift7(numerator);
  const auto max_value =
      static_cast<std::int64_t>(std::numeric_limits<std::remove_const_t<T>>::max());
  return static_cast<T>(std::clamp<std::int64_t>(rounded, 0, max_value));
}

template <typename T>
void copy_focus_active(PlaneView<const T> source, PlaneView<T> destination) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    std::copy_n(source.row(y), source.width(), destination.row(y));
  }
}

template <typename T>
void apply_focus_horizontal_reference(PlaneView<const T> source, PlaneView<T> destination,
                                      std::size_t amount) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < source.width(); ++x) {
      const auto left = source.row(y)[x == 0 ? 0 : x - 1];
      const auto right = source.row(y)[x + 1 == source.width() ? x : x + 1];
      destination.row(y)[x] = focus_reference_pixel(left, source.row(y)[x], right, amount);
    }
  }
}

template <typename T>
void apply_focus_vertical_reference(PlaneView<const T> source, PlaneView<T> destination,
                                    std::size_t amount) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    const auto* upper = source.row(y == 0 ? 0 : y - 1);
    const auto* center = source.row(y);
    const auto* lower = source.row(y + 1 == source.height() ? y : y + 1);
    for (std::size_t x = 0; x < source.width(); ++x) {
      destination.row(y)[x] = focus_reference_pixel(upper[x], center[x], lower[x], amount);
    }
  }
}

inline void run_focus_horizontal8_case(const FocusHorizontal8Case& test_case) {
  GuardedVideoBuffer<std::uint8_t> input(test_case.width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<std::uint8_t> actual(test_case.width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width, test_case.height, test_case.pitch, 32);
  fill_focus_input(input.view(), test_case.seed);
  copy_focus_active(input.view().as_const(), actual.view());
  copy_focus_active(input.view().as_const(), expected.view());
  apply_focus_horizontal_reference(input.view().as_const(), expected.view(), test_case.amount);

  test_case.variant.function(actual.view().data(), test_case.height, test_case.pitch,
                             test_case.width, test_case.amount);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch";
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(input.memory_intact()) << test_case.name << " input padding or guards were corrupted";
}

inline void run_focus_horizontal16_case(const FocusHorizontal16Case& test_case) {
  GuardedVideoBuffer<std::uint16_t> input(test_case.width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<std::uint16_t> actual(test_case.width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<std::uint16_t> expected(test_case.width, test_case.height, test_case.pitch,
                                             32);
  fill_focus_input(input.view(), test_case.seed);
  copy_focus_active(input.view().as_const(), actual.view());
  copy_focus_active(input.view().as_const(), expected.view());
  apply_focus_horizontal_reference(input.view().as_const(), expected.view(), test_case.amount);

  test_case.variant.function(reinterpret_cast<BYTE*>(actual.view().data()), test_case.height,
                             test_case.pitch, test_case.width * sizeof(std::uint16_t),
                             test_case.amount, test_case.bits_per_pixel);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch";
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(input.memory_intact()) << test_case.name << " input padding or guards were corrupted";
}

template <typename T, typename Case, typename Function>
void run_focus_vertical_case(const Case& test_case, Function function) {
  const auto active_row_bytes = test_case.width * sizeof(T);
  GuardedVideoBuffer<T> actual(test_case.width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<T> expected(test_case.width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<std::uint8_t> line_buffer(active_row_bytes, 1, active_row_bytes, 32);
  fill_focus_input(actual.view(), test_case.seed);
  copy_focus_active(actual.view().as_const(), expected.view());
  apply_focus_vertical_reference(actual.view().as_const(), expected.view(), test_case.amount);
  std::copy_n(reinterpret_cast<const std::uint8_t*>(actual.view().data()), active_row_bytes,
              line_buffer.view().data());

  function(line_buffer.view().data(), reinterpret_cast<BYTE*>(actual.view().data()),
           static_cast<int>(test_case.height), static_cast<int>(test_case.pitch),
           static_cast<int>(active_row_bytes), static_cast<int>(test_case.amount));

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch";
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(line_buffer.memory_intact())
      << test_case.name << " line buffer padding or guards were corrupted";
}

}  // namespace avsut::test
