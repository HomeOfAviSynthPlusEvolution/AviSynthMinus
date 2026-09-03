#pragma once

#include "filters/intel/layer_avx2.h"
#include "filters/intel/layer_sse.h"

#include "support/comparators.h"
#include "support/deterministic_data.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace avsut::test {

using LayerInvertFuncPtr = void (*)(std::uint8_t*, const std::uint8_t*, int, int, int, int, int);

enum class LayerInvertElement { UInt8, UInt16, Float32 };

struct LayerInvertCase {
  LayerInvertElement element{};
  bool chroma{};
  int bits_per_pixel{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<LayerInvertFuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline const char* layer_invert_element_name(LayerInvertElement element) {
  switch (element) {
    case LayerInvertElement::UInt8:
      return "Plane8";
    case LayerInvertElement::UInt16:
      return "Plane16";
    case LayerInvertElement::Float32:
      return "PlaneFloat32";
  }
  return "PlaneUnknown";
}

inline std::string layer_invert_variant_name(const Variant<LayerInvertFuncPtr>& variant) {
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

inline std::string layer_invert_case_name(const LayerInvertCase& test_case) {
  std::ostringstream stream;
  stream << layer_invert_element_name(test_case.element) << "_"
         << (test_case.chroma ? "Chroma" : "Luma") << "_Bits" << test_case.bits_per_pixel
         << "_Width" << test_case.width_pixels << "_Height" << test_case.height_pixels
         << "_SrcPitch" << test_case.source_pitch << "_DstPitch" << test_case.destination_pitch;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryAnchors_" : "_PatternFixedRandom_")
         << layer_invert_variant_name(test_case.variant);
  return stream.str();
}

inline LayerInvertCase make_layer_invert_case(LayerInvertElement element, bool chroma,
                                              int bits_per_pixel, std::size_t width_pixels,
                                              std::size_t height_pixels, std::size_t source_pitch,
                                              std::size_t destination_pitch,
                                              Variant<LayerInvertFuncPtr> variant,
                                              std::string expected_hash = {},
                                              std::uint32_t seed = 0) {
  const auto bytes_per_pixel = element == LayerInvertElement::UInt8    ? std::size_t{1}
                               : element == LayerInvertElement::UInt16 ? std::size_t{2}
                                                                       : std::size_t{4};
  if (source_pitch < width_pixels * bytes_per_pixel ||
      destination_pitch < width_pixels * bytes_per_pixel) {
    throw std::invalid_argument("Layer invert pitches must contain the active row");
  }
  LayerInvertCase result{element,
                         chroma,
                         bits_per_pixel,
                         width_pixels,
                         height_pixels,
                         source_pitch,
                         destination_pitch,
                         std::move(variant),
                         std::move(expected_hash),
                         seed,
                         {}};
  result.name = layer_invert_case_name(result);
  return result;
}

inline void PrintTo(const LayerInvertCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_layer_invert_integer_input(PlaneView<T> view, std::uint32_t max_value,
                                     std::uint32_t seed = 0) {
  static_assert(std::is_integral_v<T>);
  if (seed != 0) {
    fill_random(view, seed);
    for (std::size_t y = 0; y < view.height(); ++y) {
      for (std::size_t x = 0; x < view.width(); ++x) {
        view.row(y)[x] = static_cast<T>(static_cast<std::uint32_t>(view.row(y)[x]) & max_value);
      }
    }
    return;
  }
  const std::array<std::uint32_t, 9> anchors{0,
                                             1,
                                             2,
                                             max_value / 2U - 1U,
                                             max_value / 2U,
                                             max_value / 2U + 1U,
                                             max_value - 2U,
                                             max_value - 1U,
                                             max_value};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      view.row(y)[x] = static_cast<T>(anchors[(y * view.width() + x) % anchors.size()]);
    }
  }
}

inline void fill_layer_invert_float_input(PlaneView<float> view, std::uint32_t seed = 0) {
  if (seed != 0) {
    fill_random(view, seed);
    return;
  }
  constexpr std::array<float, 10> anchors{-3.25F, -1.0F, -0.5F, -0.0F, 0.0F,
                                          0.25F,  0.5F,  1.0F,  2.5F,  17.0F};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      view.row(y)[x] = anchors[(y * view.width() + x) % anchors.size()];
    }
  }
}

template <typename T>
void apply_layer_invert_integer_reference(const LayerInvertCase& test_case,
                                          PlaneView<const T> source, PlaneView<T> destination) {
  static_assert(std::is_integral_v<T>);
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto source_value = static_cast<std::uint32_t>(source.row(y)[x]);
      const auto result =
          test_case.chroma
              ? std::min(max_value, (std::uint32_t{1} << test_case.bits_per_pixel) - source_value)
              : max_value - source_value;
      destination.row(y)[x] = static_cast<T>(result);
    }
  }
}

inline void apply_layer_invert_float_reference(const LayerInvertCase& test_case,
                                               PlaneView<const float> source,
                                               PlaneView<float> destination) {
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const float source_value = source.row(y)[x];
      destination.row(y)[x] = test_case.chroma ? -source_value : 1.0F - source_value;
    }
  }
}

inline std::uint64_t layer_invert_float_ulp_distance(float lhs, float rhs) noexcept {
  if (lhs == rhs) {
    return 0;
  }
  std::uint32_t lhs_bits{};
  std::uint32_t rhs_bits{};
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  constexpr std::uint32_t sign_bit = 0x80000000U;
  if ((lhs_bits & sign_bit) != (rhs_bits & sign_bit)) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  const auto lhs_magnitude = lhs_bits & ~sign_bit;
  const auto rhs_magnitude = rhs_bits & ~sign_bit;
  return lhs_magnitude >= rhs_magnitude ? static_cast<std::uint64_t>(lhs_magnitude - rhs_magnitude)
                                        : static_cast<std::uint64_t>(rhs_magnitude - lhs_magnitude);
}

inline ::testing::AssertionResult compare_layer_invert_float(PlaneView<const float> expected,
                                                             PlaneView<const float> actual) {
  if (expected.width() != actual.width() || expected.height() != actual.height()) {
    return ::testing::AssertionFailure() << "dimension mismatch";
  }
  constexpr std::uint64_t maximum_ulps = 4;
  constexpr float absolute_floor = 1.0e-4F;
  for (std::size_t y = 0; y < expected.height(); ++y) {
    for (std::size_t x = 0; x < expected.width(); ++x) {
      const float lhs = expected.row(y)[x];
      const float rhs = actual.row(y)[x];
      if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
        return ::testing::AssertionFailure() << "row=" << y << " col=" << x
                                             << " non-finite expected=" << lhs << " actual=" << rhs;
      }
      if (lhs == rhs) {
        continue;
      }
      const float absolute_error = std::abs(lhs - rhs);
      const auto ulps = layer_invert_float_ulp_distance(lhs, rhs);
      if (absolute_error <= absolute_floor || ulps <= maximum_ulps) {
        continue;
      }
      return ::testing::AssertionFailure()
             << "row=" << y << " col=" << x << " expected=" << lhs << " actual=" << rhs
             << " absolute_error=" << absolute_error << " ulps=" << ulps
             << " allowed_ulps=" << maximum_ulps << " absolute_floor=" << absolute_floor;
    }
  }
  return ::testing::AssertionSuccess();
}

template <typename T>
void run_layer_invert_integer_case(const LayerInvertCase& test_case) {
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  GuardedVideoBuffer<T> source(test_case.width_pixels, test_case.height_pixels,
                               test_case.source_pitch, 32);
  GuardedVideoBuffer<T> expected(test_case.width_pixels, test_case.height_pixels,
                                 test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> actual(test_case.width_pixels, test_case.height_pixels,
                               test_case.destination_pitch, 32);
  fill_layer_invert_integer_input(source.view(), max_value, test_case.seed);
  apply_layer_invert_integer_reference(test_case, source.view().as_const(), expected.view());
  const auto source_snapshot = source.snapshot_active();

  test_case.variant.function(reinterpret_cast<std::uint8_t*>(actual.view().data()),
                             reinterpret_cast<const std::uint8_t*>(source.view().data()),
                             static_cast<int>(source.view().pitch_bytes()),
                             static_cast<int>(actual.view().pitch_bytes()),
                             static_cast<int>(test_case.width_pixels),
                             static_cast<int>(test_case.height_pixels), test_case.bits_per_pixel);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch (actual hash is reported above)";
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name << " modified source input";
  EXPECT_TRUE(source.memory_intact())
      << test_case.name << " source padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
}

inline void run_layer_invert_float_case(const LayerInvertCase& test_case) {
  GuardedVideoBuffer<float> source(test_case.width_pixels, test_case.height_pixels,
                                   test_case.source_pitch, 32);
  GuardedVideoBuffer<float> expected(test_case.width_pixels, test_case.height_pixels,
                                     test_case.destination_pitch, 32);
  GuardedVideoBuffer<float> actual(test_case.width_pixels, test_case.height_pixels,
                                   test_case.destination_pitch, 32);
  fill_layer_invert_float_input(source.view(), test_case.seed);
  apply_layer_invert_float_reference(test_case, source.view().as_const(), expected.view());
  const auto source_snapshot = source.snapshot_active();

  test_case.variant.function(reinterpret_cast<std::uint8_t*>(actual.view().data()),
                             reinterpret_cast<const std::uint8_t*>(source.view().data()),
                             static_cast<int>(source.view().pitch_bytes()),
                             static_cast<int>(actual.view().pitch_bytes()),
                             static_cast<int>(test_case.width_pixels),
                             static_cast<int>(test_case.height_pixels), test_case.bits_per_pixel);

  EXPECT_TRUE(compare_layer_invert_float(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name << " modified source input";
  EXPECT_TRUE(source.memory_intact())
      << test_case.name << " source padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
}

inline void run_layer_invert_case(const LayerInvertCase& test_case) {
  if (test_case.element == LayerInvertElement::UInt8) {
    run_layer_invert_integer_case<std::uint8_t>(test_case);
  } else if (test_case.element == LayerInvertElement::UInt16) {
    run_layer_invert_integer_case<std::uint16_t>(test_case);
  } else {
    run_layer_invert_float_case(test_case);
  }
}

}  // namespace avsut::test
