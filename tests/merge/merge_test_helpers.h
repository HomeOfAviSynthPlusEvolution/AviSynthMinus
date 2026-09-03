#pragma once

#include "filters/overlay/blend_common.h"

#include "support/comparators.h"
#include "support/cpu_features.h"
#include "support/deterministic_data.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace avsut::test {

using MergeFuncPtr = weighted_merge_fn_t*;

struct MergeCase {
  std::string format;
  int bits_per_pixel{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t destination_pitch{};
  std::size_t other_pitch{};
  int weight{};
  int inverse_weight{};
  std::uint32_t seed{};
  MergeFuncPtr scalar_function{};
  Variant<MergeFuncPtr> variant;
  std::string expected_hash;
  std::string name;
};

inline void PrintTo(const MergeCase& test_case, std::ostream* stream) { *stream << test_case.name; }

inline std::string merge_variant_name(const Variant<MergeFuncPtr>& variant) {
  std::string result = "Variant";
  bool capitalize = true;
  for (const char character : variant.name) {
    if (character == '_' || character == '-') {
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

inline std::string merge_case_name(const std::string& format, int bits_per_pixel,
                                   std::size_t width_pixels, std::size_t height_pixels,
                                   std::size_t destination_pitch, std::size_t other_pitch,
                                   int weight, std::uint32_t seed,
                                   const Variant<MergeFuncPtr>& variant) {
  std::ostringstream stream;
  stream << format << "Bpp" << bits_per_pixel << "_Width" << width_pixels << "_Height"
         << height_pixels << "_DstPitch" << destination_pitch << "_OtherPitch" << other_pitch
         << "_Weight" << weight << "_Seed" << std::uppercase << std::hex << seed << "_"
         << merge_variant_name(variant);
  return stream.str();
}

inline MergeCase make_merge_case(std::string format, int bits_per_pixel, std::size_t width_pixels,
                                 std::size_t height_pixels, std::size_t destination_pitch,
                                 std::size_t other_pitch, int weight, std::uint32_t seed,
                                 MergeFuncPtr scalar_function, Variant<MergeFuncPtr> variant,
                                 std::string expected_hash = {}) {
  if (weight <= 0 || weight >= 32768) {
    throw std::invalid_argument("Merge test weights must be interior values");
  }
  MergeCase result{std::move(format),
                   bits_per_pixel,
                   width_pixels,
                   height_pixels,
                   destination_pitch,
                   other_pitch,
                   weight,
                   32768 - weight,
                   seed,
                   scalar_function,
                   std::move(variant),
                   std::move(expected_hash),
                   {}};
  result.name = merge_case_name(result.format, result.bits_per_pixel, result.width_pixels,
                                result.height_pixels, result.destination_pitch, result.other_pitch,
                                result.weight, result.seed, result.variant);
  return result;
}

inline std::uint32_t merge_max_value(int bits_per_pixel) {
  if (bits_per_pixel <= 0 || bits_per_pixel > 16) {
    throw std::invalid_argument("Merge test bit depth must be in 1..16");
  }
  return (std::uint32_t{1} << bits_per_pixel) - 1;
}

template <typename T>
void fill_merge_random(PlaneView<T> view, std::uint32_t seed, std::uint32_t max_value) {
  static_assert(!std::is_const_v<T>);
  XorShift32 generator(seed);
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      view.row(y)[x] = static_cast<T>(generator.next() & max_value);
    }
  }
}

template <typename T>
void copy_active(PlaneView<const T> source, PlaneView<T> destination) {
  if (source.width() != destination.width() || source.height() != destination.height()) {
    throw std::invalid_argument("Merge buffer dimensions do not match");
  }
  for (std::size_t y = 0; y < source.height(); ++y) {
    std::copy_n(source.row(y), source.width(), destination.row(y));
  }
}

template <typename T>
void apply_merge_reference(const MergeCase& test_case, PlaneView<T> destination,
                           PlaneView<const T> other) {
  if (destination.width() != test_case.width_pixels ||
      destination.height() != test_case.height_pixels || other.width() != test_case.width_pixels ||
      other.height() != test_case.height_pixels) {
    throw std::invalid_argument("Merge reference dimensions do not match");
  }
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto first = static_cast<std::uint64_t>(destination.row(y)[x]);
      const auto second = static_cast<std::uint64_t>(other.row(y)[x]);
      destination.row(y)[x] =
          static_cast<T>((first * static_cast<std::uint64_t>(test_case.inverse_weight) +
                          second * static_cast<std::uint64_t>(test_case.weight) + 16384) >>
                         15);
    }
  }
}

template <typename T>
void invoke_merge(MergeFuncPtr function, PlaneView<T> destination, PlaneView<const T> other,
                  const MergeCase& test_case) {
  function(reinterpret_cast<BYTE*>(destination.data()), reinterpret_cast<const BYTE*>(other.data()),
           static_cast<int>(destination.pitch_bytes()), static_cast<int>(other.pitch_bytes()),
           static_cast<int>(test_case.width_pixels), static_cast<int>(test_case.height_pixels),
           test_case.weight, test_case.inverse_weight, test_case.bits_per_pixel);
}

template <typename T>
void run_merge_case_typed(const MergeCase& test_case) {
  const auto max_value = merge_max_value(test_case.bits_per_pixel);
  GuardedVideoBuffer<T> destination(test_case.width_pixels, test_case.height_pixels,
                                    test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> other(test_case.width_pixels, test_case.height_pixels,
                              test_case.other_pitch, 32);
  GuardedVideoBuffer<T> expected(test_case.width_pixels, test_case.height_pixels,
                                 test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> scalar(test_case.width_pixels, test_case.height_pixels,
                               test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> actual(test_case.width_pixels, test_case.height_pixels,
                               test_case.destination_pitch, 32);

  fill_merge_random(destination.view(), test_case.seed, max_value);
  fill_merge_random(other.view(), test_case.seed ^ 0xA5A5A5A5U, max_value);
  const auto other_snapshot = other.snapshot_active();
  copy_active(destination.view().as_const(), expected.view());
  copy_active(destination.view().as_const(), scalar.view());
  copy_active(destination.view().as_const(), actual.view());

  apply_merge_reference(test_case, expected.view(), other.view().as_const());
  invoke_merge(test_case.scalar_function, scalar.view(), other.view().as_const(), test_case);
  invoke_merge(test_case.variant.function, actual.view(), other.view().as_const(), test_case);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), scalar.view().as_const()))
      << test_case.name << " scalar reference mismatch";
  EXPECT_TRUE(compare_exact(scalar.view().as_const(), actual.view().as_const()))
      << test_case.name << " variant differential mismatch";
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(scalar.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(other.active_matches(other_snapshot))
      << test_case.name << " modified the second input";
  EXPECT_TRUE(destination.memory_intact());
  EXPECT_TRUE(other.memory_intact());
  EXPECT_TRUE(expected.memory_intact());
  EXPECT_TRUE(scalar.memory_intact());
  EXPECT_TRUE(actual.memory_intact());
}

inline void run_merge_case(const MergeCase& test_case) {
  if (test_case.bits_per_pixel == 8) {
    run_merge_case_typed<std::uint8_t>(test_case);
  } else {
    run_merge_case_typed<std::uint16_t>(test_case);
  }
}

}  // namespace avsut::test
