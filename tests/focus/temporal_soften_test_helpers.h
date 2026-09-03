#pragma once

#include "filters/intel/focus_sse.h"

#include "support/comparators.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace avsut::test {

using TemporalSoften8Function = void (*)(BYTE*, const BYTE**, int, std::size_t, int, int);
using TemporalSoften16Function = void (*)(BYTE*, const BYTE**, int, std::size_t, int, int, int);

struct TemporalSoften8Case {
  std::size_t width{};
  std::size_t pitch{};
  int source_planes{};
  std::uint8_t threshold{};
  Variant<TemporalSoften8Function> variant;
  std::string expected_hash;
  std::string name;
};

struct TemporalSoften16Case {
  std::size_t width{};
  std::size_t pitch{};
  int source_planes{};
  std::uint8_t threshold{};
  int bits_per_pixel{};
  Variant<TemporalSoften16Function> variant;
  std::string expected_hash;
  std::string name;
};

template <typename Function>
inline std::string temporal_soften_variant_name(const Variant<Function>& variant) {
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
inline std::string temporal_soften_case_name(const char* format, std::size_t width,
                                             std::size_t pitch, int source_planes,
                                             std::uint8_t threshold,
                                             const Variant<Function>& variant) {
  std::ostringstream stream;
  stream << format << "_Width" << width << "_Pitch" << pitch << "_Planes" << source_planes
         << "_Threshold" << static_cast<unsigned>(threshold) << "_Pattern"
         << (threshold == 255U ? "AverageAnchors" : "ThresholdEdges") << "_"
         << temporal_soften_variant_name(variant);
  return stream.str();
}

inline std::string temporal_soften16_case_name(std::size_t width, std::size_t pitch,
                                               int source_planes, std::uint8_t threshold,
                                               int bits_per_pixel,
                                               const Variant<TemporalSoften16Function>& variant) {
  std::ostringstream stream;
  stream << temporal_soften_case_name("Plane16", width, pitch, source_planes, threshold, variant)
         << "_Bits" << bits_per_pixel;
  return stream.str();
}

inline TemporalSoften8Case make_temporal_soften8_case(std::size_t width, std::size_t pitch,
                                                      int source_planes, std::uint8_t threshold,
                                                      Variant<TemporalSoften8Function> variant,
                                                      std::string expected_hash) {
  TemporalSoften8Case result{
      width, pitch, source_planes, threshold, std::move(variant), std::move(expected_hash), {}};
  result.name = temporal_soften_case_name("Plane8", result.width, result.pitch,
                                          result.source_planes, result.threshold, result.variant);
  return result;
}

inline TemporalSoften16Case make_temporal_soften16_case(std::size_t width, std::size_t pitch,
                                                        int source_planes, std::uint8_t threshold,
                                                        int bits_per_pixel,
                                                        Variant<TemporalSoften16Function> variant,
                                                        std::string expected_hash) {
  TemporalSoften16Case result{width,
                              pitch,
                              source_planes,
                              threshold,
                              bits_per_pixel,
                              std::move(variant),
                              std::move(expected_hash),
                              {}};
  result.name =
      temporal_soften16_case_name(result.width, result.pitch, result.source_planes,
                                  result.threshold, result.bits_per_pixel, result.variant);
  return result;
}

inline void PrintTo(const TemporalSoften8Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const TemporalSoften16Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_temporal_soften_inputs(
    PlaneView<T> current, const std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 3>& sources,
    int source_planes, std::uint32_t max_value, std::uint32_t threshold, bool max_threshold) {
  ASSERT_EQ(current.height(), 1U);
  ASSERT_GE(source_planes, 1);
  ASSERT_LE(source_planes, static_cast<int>(sources.size()));

  for (int plane = 0; plane < source_planes; ++plane) {
    ASSERT_NE(sources[static_cast<std::size_t>(plane)], nullptr);
    ASSERT_EQ(sources[static_cast<std::size_t>(plane)]->view().width(), current.width());
  }

  const auto value_range = static_cast<std::uint64_t>(max_value) + 1U;
  for (std::size_t x = 0; x < current.width(); ++x) {
    if (max_threshold) {
      current.row(0)[x] = static_cast<T>((x * 97U + 43U) % value_range);
      for (int plane = 0; plane < source_planes; ++plane) {
        sources[static_cast<std::size_t>(plane)]->view().row(0)[x] =
            static_cast<T>((x * 131U + static_cast<std::size_t>(plane) * 719U + 11U) % value_range);
      }
      continue;
    }

    const auto safety_margin = threshold * 3U + 32U;
    ASSERT_LT(safety_margin * 2U, max_value);
    const auto span = max_value - safety_margin * 2U + 1U;
    const auto current_value = safety_margin + static_cast<std::uint32_t>((x * 37U + 19U) % span);
    current.row(0)[x] = static_cast<T>(current_value);

    constexpr std::array<int, 8> delta_multipliers{-2, -1, 0, 1, 2, -3, 3, -1};
    for (int plane = 0; plane < source_planes; ++plane) {
      const auto multiplier =
          delta_multipliers[(x + static_cast<std::size_t>(plane) * 3U) % delta_multipliers.size()];
      const auto magnitude =
          static_cast<int>(threshold) + ((x + static_cast<std::size_t>(plane)) % 3U == 0U ? 0 : 1);
      const auto source_value = static_cast<int>(current_value) + multiplier * magnitude;
      ASSERT_GE(source_value, 0);
      ASSERT_LE(source_value, static_cast<int>(max_value));
      sources[static_cast<std::size_t>(plane)]->view().row(0)[x] = static_cast<T>(source_value);
    }
  }
}

template <typename T>
void copy_temporal_soften_active(PlaneView<const T> source, PlaneView<T> destination) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    std::copy_n(source.row(y), source.width(), destination.row(y));
  }
}

template <typename T>
void apply_temporal_soften_reference(
    PlaneView<T> destination, const std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 3>& sources,
    int source_planes, std::uint32_t threshold, bool max_threshold) {
  ASSERT_EQ(destination.height(), 1U);
  const auto divisor = static_cast<std::uint64_t>(source_planes + 1);

  for (std::size_t x = 0; x < destination.width(); ++x) {
    const auto current = static_cast<std::uint64_t>(destination.row(0)[x]);
    auto sum = current;
    for (int plane = source_planes - 1; plane >= 0; --plane) {
      const auto source =
          static_cast<std::uint64_t>(sources[static_cast<std::size_t>(plane)]->view().row(0)[x]);
      const auto difference = current > source ? current - source : source - current;
      sum += max_threshold || difference <= threshold ? source : current;
    }
    const auto quotient = sum / divisor;
    const auto remainder = sum % divisor;
    const auto rounded = quotient + (remainder * 2U > divisor ||
                                     (remainder * 2U == divisor && (quotient & 1U) != 0U));
    destination.row(0)[x] = static_cast<T>(rounded);
  }
}

template <typename T, typename Case, typename Function>
void run_temporal_soften_case(const Case& test_case, Function function, std::uint32_t max_value,
                              std::uint32_t threshold) {
  constexpr std::size_t kAlignment = 64;
  GuardedVideoBuffer<T> actual(test_case.width, 1, test_case.pitch, kAlignment);
  GuardedVideoBuffer<T> expected(test_case.width, 1, test_case.pitch, kAlignment);
  std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 3> sources;
  for (int plane = 0; plane < test_case.source_planes; ++plane) {
    sources[static_cast<std::size_t>(plane)] =
        std::make_unique<GuardedVideoBuffer<T>>(test_case.width, 1, test_case.pitch, kAlignment);
  }

  const bool max_threshold = test_case.threshold == 255U;
  fill_temporal_soften_inputs(actual.view(), sources, test_case.source_planes, max_value, threshold,
                              max_threshold);
  copy_temporal_soften_active(actual.view().as_const(), expected.view());
  apply_temporal_soften_reference(expected.view(), sources, test_case.source_planes, threshold,
                                  max_threshold);

  std::array<const BYTE*, 3> plane_pointers{};
  std::array<std::vector<std::uint8_t>, 3> source_snapshots;
  for (int plane = 0; plane < test_case.source_planes; ++plane) {
    const auto source_index = static_cast<std::size_t>(plane);
    plane_pointers[source_index] =
        reinterpret_cast<const BYTE*>(sources[source_index]->view().data());
    source_snapshots[source_index] = sources[source_index]->snapshot_active();
  }

  function(reinterpret_cast<BYTE*>(actual.view().data()), plane_pointers.data(),
           test_case.source_planes);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch";
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  for (int plane = 0; plane < test_case.source_planes; ++plane) {
    const auto source_index = static_cast<std::size_t>(plane);
    EXPECT_TRUE(sources[source_index]->active_matches(source_snapshots[source_index]))
        << test_case.name << " modified source plane " << plane;
    EXPECT_TRUE(sources[source_index]->memory_intact())
        << test_case.name << " corrupted source padding or guards for plane " << plane;
  }
}

inline void run_temporal_soften8_case(const TemporalSoften8Case& test_case) {
  const auto packed_threshold =
      static_cast<int>(test_case.threshold) | (static_cast<int>(test_case.threshold) << 8U);
  const auto divisor = 32768 / (test_case.source_planes + 1);
  run_temporal_soften_case<std::uint8_t>(
      test_case,
      [&](BYTE* current, const BYTE** sources, int source_planes) {
        test_case.variant.function(current, sources, source_planes, test_case.width,
                                   packed_threshold, divisor);
      },
      std::numeric_limits<std::uint8_t>::max(), static_cast<std::uint32_t>(test_case.threshold));
}

inline void run_temporal_soften16_case(const TemporalSoften16Case& test_case) {
  const auto threshold = static_cast<std::uint32_t>(test_case.threshold)
                         << static_cast<unsigned>(test_case.bits_per_pixel - 8);
  const auto divisor = 32768 / (test_case.source_planes + 1);
  const auto max_value = (std::uint32_t{1} << static_cast<unsigned>(test_case.bits_per_pixel)) - 1U;
  run_temporal_soften_case<std::uint16_t>(
      test_case,
      [&](BYTE* current, const BYTE** sources, int source_planes) {
        test_case.variant.function(current, sources, source_planes,
                                   test_case.width * sizeof(std::uint16_t),
                                   static_cast<int>(threshold), divisor, test_case.bits_per_pixel);
      },
      max_value, threshold);
}

}  // namespace avsut::test
