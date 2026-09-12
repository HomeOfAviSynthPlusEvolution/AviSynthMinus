#pragma once
#include <avisynth.h>
#include "filters/intel/resize_sse.h"
#include "support/comparators.h"
#include "support/deterministic_data.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
namespace avsut::test {
using VerticalReduceFunction = void (*)(BYTE*, const BYTE*, int, int, std::size_t, std::size_t);
struct VerticalReduceCase {
  std::size_t width_bytes{};
  std::size_t source_height{};
  std::size_t target_height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<VerticalReduceFunction> variant;
  std::string expected_hash;
  std::string name;
};

template <typename Function>
std::string resize_variant_name(const Variant<Function>& variant) {
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

inline std::string vertical_reduce_case_name(const VerticalReduceCase& test_case) {
  std::ostringstream stream;
  stream << "VerticalReduce_WidthBytes" << test_case.width_bytes << "_SourceHeight"
         << test_case.source_height << "_TargetHeight" << test_case.target_height << "_SrcPitch"
         << test_case.source_pitch << "_DstPitch" << test_case.destination_pitch
         << "_PatternBoundaryRamp_" << resize_variant_name(test_case.variant);
  return stream.str();
}

inline VerticalReduceCase make_vertical_reduce_case(
    std::size_t width_bytes, std::size_t source_height, std::size_t target_height,
    std::size_t source_pitch, std::size_t destination_pitch,
    Variant<VerticalReduceFunction> variant, std::string expected_hash = {}) {
  VerticalReduceCase result{width_bytes,
                            source_height,
                            target_height,
                            source_pitch,
                            destination_pitch,
                            std::move(variant),
                            std::move(expected_hash),
                            {}};
  result.name = vertical_reduce_case_name(result);
  return result;
}

inline void PrintTo(const VerticalReduceCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_resize_input(PlaneView<T> view, int bits_per_pixel, std::uint32_t seed = 0) {
  static_assert(!std::is_const_v<T>);
  using Value = std::remove_const_t<T>;
  const std::uint64_t max_value = (std::uint64_t{1} << bits_per_pixel) - 1U;
  const std::array<std::uint64_t, 10> anchors{0U,
                                              1U,
                                              max_value / 7U,
                                              max_value / 3U,
                                              max_value / 2U,
                                              max_value - 2U,
                                              max_value - 1U,
                                              max_value,
                                              17U,
                                              193U};

  if (seed != 0) {
    XorShift32 generator(seed);
    for (std::size_t y = 0; y < view.height(); ++y) {
      for (std::size_t x = 0; x < view.width(); ++x) {
        view.row(y)[x] = static_cast<Value>(generator.next() & max_value);
      }
    }
    return;
  }

  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      const std::size_t index = y * view.width() + x;
      const std::uint64_t value = (index % anchors.size() == 0U)
                                      ? anchors[(index / anchors.size()) % anchors.size()]
                                      : (x * 37U + y * 101U + index * 13U) % (max_value + 1U);
      view.row(y)[x] = static_cast<Value>(value);
    }
  }
}

inline void apply_vertical_reduce_reference(PlaneView<const std::uint8_t> source,
                                            PlaneView<std::uint8_t> destination) {
  for (std::size_t y = 0; y < destination.height(); ++y) {
    const auto source_y = y * 2;
    for (std::size_t x = 0; x < destination.width(); ++x) {
      const auto first = source.row(source_y)[x];
      const auto second = source.row(source_y + 1)[x];
      if (y + 1 < destination.height()) {
        const auto third = source.row(source_y + 2)[x];
        destination.row(y)[x] = static_cast<std::uint8_t>((first + 2 * second + third + 2) / 4);
      } else {
        destination.row(y)[x] = static_cast<std::uint8_t>((first + 3 * second + 2) / 4);
      }
    }
  }
}

inline void run_vertical_reduce_case(const VerticalReduceCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source(test_case.width_bytes, test_case.source_height,
                                          test_case.source_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_bytes, test_case.target_height,
                                            test_case.destination_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual(test_case.width_bytes, test_case.target_height,
                                          test_case.destination_pitch, 64);
  fill_resize_input(source.view(), 8);
  const auto source_snapshot = source.snapshot_active();
  apply_vertical_reduce_reference(source.view().as_const(), expected.view());

  test_case.variant.function(reinterpret_cast<BYTE*>(actual.view().data()),
                             reinterpret_cast<const BYTE*>(source.view().data()),
                             static_cast<int>(actual.view().pitch_bytes()),
                             static_cast<int>(source.view().pitch_bytes()), test_case.width_bytes,
                             test_case.target_height);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(actual.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << test_case.name << " modified source pixels";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " corrupted source padding or guards";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " corrupted reference padding or guards";
  EXPECT_TRUE(actual.memory_intact()) << test_case.name << " corrupted output padding or guards";
}

} // namespace avsut::test
