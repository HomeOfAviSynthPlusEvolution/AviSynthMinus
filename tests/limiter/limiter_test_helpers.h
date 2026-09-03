#pragma once

#include "filters/intel/limiter_sse.h"

#include "support/comparators.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace avsut::test {

using Limiter8FuncPtr = void (*)(BYTE*, int, int, int, int, int);
using Limiter16FuncPtr = void (*)(BYTE*, unsigned int, unsigned int, int, int);

struct Limiter8Case {
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t pitch_bytes{};
  std::uint8_t min_value{};
  std::uint8_t max_value{};
  Limiter8FuncPtr function{};
  Variant<Limiter8FuncPtr> variant;
  std::string expected_hash;
  std::string name;
};

struct Limiter16Case {
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t pitch_bytes{};
  std::uint16_t min_value{};
  std::uint16_t max_value{};
  Limiter16FuncPtr function{};
  Variant<Limiter16FuncPtr> variant;
  std::string expected_hash;
  std::string name;
};

template <typename Function>
inline std::string limiter_variant_name(const Variant<Function>& variant) {
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

inline std::string limiter8_case_name(std::size_t width_pixels, std::size_t height_pixels,
                                      std::size_t pitch_bytes, std::uint8_t min_value,
                                      std::uint8_t max_value,
                                      const Variant<Limiter8FuncPtr>& variant) {
  std::ostringstream stream;
  stream << "Plane8_Width" << width_pixels << "_Height" << height_pixels << "_Pitch" << pitch_bytes
         << "_Range" << static_cast<unsigned int>(min_value) << "To"
         << static_cast<unsigned int>(max_value) << "_PatternBoundaryValues_"
         << limiter_variant_name(variant);
  return stream.str();
}

inline std::string limiter16_case_name(std::size_t width_pixels, std::size_t height_pixels,
                                       std::size_t pitch_bytes, std::uint16_t min_value,
                                       std::uint16_t max_value,
                                       const Variant<Limiter16FuncPtr>& variant) {
  std::ostringstream stream;
  stream << "Plane16_Width" << width_pixels << "_Height" << height_pixels << "_Pitch" << pitch_bytes
         << "_Range" << min_value << "To" << max_value << "_PatternBoundaryValues_"
         << limiter_variant_name(variant);
  return stream.str();
}

inline Limiter8Case make_limiter8_case(std::size_t width_pixels, std::size_t height_pixels,
                                       std::size_t pitch_bytes, std::uint8_t min_value,
                                       std::uint8_t max_value, Limiter8FuncPtr function,
                                       Variant<Limiter8FuncPtr> variant,
                                       std::string expected_hash = {}) {
  Limiter8Case result{width_pixels,
                      height_pixels,
                      pitch_bytes,
                      min_value,
                      max_value,
                      function,
                      std::move(variant),
                      std::move(expected_hash),
                      {}};
  result.name = limiter8_case_name(result.width_pixels, result.height_pixels, result.pitch_bytes,
                                   result.min_value, result.max_value, result.variant);
  return result;
}

inline Limiter16Case make_limiter16_case(std::size_t width_pixels, std::size_t height_pixels,
                                         std::size_t pitch_bytes, std::uint16_t min_value,
                                         std::uint16_t max_value, Limiter16FuncPtr function,
                                         Variant<Limiter16FuncPtr> variant,
                                         std::string expected_hash = {}) {
  Limiter16Case result{width_pixels,
                       height_pixels,
                       pitch_bytes,
                       min_value,
                       max_value,
                       function,
                       std::move(variant),
                       std::move(expected_hash),
                       {}};
  result.name = limiter16_case_name(result.width_pixels, result.height_pixels, result.pitch_bytes,
                                    result.min_value, result.max_value, result.variant);
  return result;
}

inline void PrintTo(const Limiter8Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const Limiter16Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_limiter8_input(PlaneView<std::uint8_t> view, std::uint8_t min_value,
                                std::uint8_t max_value) {
  const std::array<std::uint8_t, 11> values{0,
                                            1,
                                            static_cast<std::uint8_t>(min_value - 1),
                                            min_value,
                                            static_cast<std::uint8_t>(min_value + 1),
                                            128,
                                            static_cast<std::uint8_t>(max_value - 1),
                                            max_value,
                                            static_cast<std::uint8_t>(max_value + 1),
                                            254,
                                            255};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      view.row(y)[x] = values[(y * view.width() + x) % values.size()];
    }
  }
}

inline void fill_limiter16_input(PlaneView<std::uint16_t> view, std::uint16_t min_value,
                                 std::uint16_t max_value) {
  const std::array<std::uint16_t, 11> values{0,
                                             1,
                                             static_cast<std::uint16_t>(min_value - 1),
                                             min_value,
                                             static_cast<std::uint16_t>(min_value + 1),
                                             32768,
                                             static_cast<std::uint16_t>(max_value - 1),
                                             max_value,
                                             static_cast<std::uint16_t>(max_value + 1),
                                             65534,
                                             65535};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      view.row(y)[x] = values[(y * view.width() + x) % values.size()];
    }
  }
}

template <typename T>
void copy_active_values(PlaneView<const T> source, PlaneView<T> destination) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    std::copy_n(source.row(y), source.width(), destination.row(y));
  }
}

template <typename T>
void apply_limiter_reference(PlaneView<T> view, T min_value, T max_value) {
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      view.row(y)[x] = std::min(max_value, std::max(min_value, view.row(y)[x]));
    }
  }
}

inline int pack_byte_limit(std::uint8_t value) {
  return static_cast<int>(value) | (static_cast<int>(value) << 8);
}

inline void run_limiter8_case(const Limiter8Case& test_case) {
  GuardedVideoBuffer<std::uint8_t> actual(test_case.width_pixels, test_case.height_pixels,
                                          test_case.pitch_bytes, 32);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_pixels, test_case.height_pixels,
                                            test_case.pitch_bytes, 32);

  fill_limiter8_input(actual.view(), test_case.min_value, test_case.max_value);
  copy_active_values(actual.view().as_const(), expected.view());
  apply_limiter_reference(expected.view(), test_case.min_value, test_case.max_value);

  test_case.function(
      reinterpret_cast<BYTE*>(actual.view().data()), pack_byte_limit(test_case.min_value),
      pack_byte_limit(test_case.max_value), static_cast<int>(actual.view().pitch_bytes()),
      static_cast<int>(actual.view().active_row_bytes()), static_cast<int>(actual.view().height()));

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(actual.guards_intact()) << test_case.name << " allocation guards were corrupted";
  if (!actual.padding_intact()) {
    GTEST_LOG_(INFO) << test_case.name << " output padding was modified by the full-pitch "
                      << test_case.variant.name << " implementation";
  }
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
}

inline void run_limiter16_case(const Limiter16Case& test_case) {
  GuardedVideoBuffer<std::uint16_t> actual(test_case.width_pixels, test_case.height_pixels,
                                           test_case.pitch_bytes, 32);
  GuardedVideoBuffer<std::uint16_t> expected(test_case.width_pixels, test_case.height_pixels,
                                             test_case.pitch_bytes, 32);

  fill_limiter16_input(actual.view(), test_case.min_value, test_case.max_value);
  copy_active_values(actual.view().as_const(), expected.view());
  apply_limiter_reference(expected.view(), test_case.min_value, test_case.max_value);

  test_case.function(reinterpret_cast<BYTE*>(actual.view().data()), test_case.min_value,
                     test_case.max_value, static_cast<int>(actual.view().pitch_bytes()),
                     static_cast<int>(actual.view().height()));

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(actual.guards_intact()) << test_case.name << " allocation guards were corrupted";
  if (!actual.padding_intact()) {
    GTEST_LOG_(INFO) << test_case.name << " output padding was modified by the full-pitch "
                      << test_case.variant.name << " implementation";
  }
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
}

}  // namespace avsut::test
