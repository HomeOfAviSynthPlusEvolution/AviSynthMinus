#pragma once

#include "merge_test_helpers.h"

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
#include <utility>

namespace avsut::test {

using MergeFloatFuncPtr = weighted_merge_float_fn_t*;

enum class FloatInputPattern {
  RandomBounded,
  MixedMagnitudeCancellation,
};

struct MergeFloatCase {
  std::string pattern;
  FloatInputPattern input_pattern{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t destination_pitch{};
  std::size_t other_pitch{};
  float weight{};
  std::string weight_label;
  std::uint32_t seed{};
  MergeFloatFuncPtr scalar_function{};
  Variant<MergeFloatFuncPtr> variant;
  std::string name;
};

inline void PrintTo(const MergeFloatCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline std::string merge_float_variant_name(const Variant<MergeFloatFuncPtr>& variant) {
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

inline std::string float_pattern_name(FloatInputPattern pattern) {
  switch (pattern) {
    case FloatInputPattern::RandomBounded:
      return "Random";
    case FloatInputPattern::MixedMagnitudeCancellation:
      return "MixedMagnitudeCancellation";
  }
  return "Unknown";
}

inline std::string merge_float_case_name(const std::string& pattern, std::size_t width_pixels,
                                         std::size_t height_pixels, std::size_t destination_pitch,
                                         std::size_t other_pitch, const std::string& weight_label,
                                         std::uint32_t seed,
                                         const Variant<MergeFloatFuncPtr>& variant) {
  std::ostringstream stream;
  stream << pattern << "_Width" << width_pixels << "_Height" << height_pixels << "_DstPitch"
         << destination_pitch << "_OtherPitch" << other_pitch << "_Weight" << weight_label
         << "_Seed" << std::uppercase << std::hex << seed << "_"
         << merge_float_variant_name(variant);
  return stream.str();
}

inline MergeFloatCase make_merge_float_case(FloatInputPattern input_pattern,
                                            std::size_t width_pixels, std::size_t height_pixels,
                                            std::size_t destination_pitch, std::size_t other_pitch,
                                            float weight, std::string weight_label,
                                            std::uint32_t seed, MergeFloatFuncPtr scalar_function,
                                            Variant<MergeFloatFuncPtr> variant) {
  if (!std::isfinite(weight) || weight <= 0.0f || weight >= 1.0f) {
    throw std::invalid_argument("Merge float weights must be finite and interior values");
  }
  const auto pattern = float_pattern_name(input_pattern);
  MergeFloatCase result{pattern,         input_pattern,           width_pixels,
                        height_pixels,   destination_pitch,       other_pitch,
                        weight,          std::move(weight_label), seed,
                        scalar_function, std::move(variant),      {}};
  result.name = merge_float_case_name(result.pattern, result.width_pixels, result.height_pixels,
                                      result.destination_pitch, result.other_pitch,
                                      result.weight_label, result.seed, result.variant);
  return result;
}

inline float bounded_random_float(XorShift32& generator) {
  const double unit = static_cast<double>(generator.next()) /
                      static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  return static_cast<float>((unit * 2.0 - 1.0) * 1024.0);
}

inline void fill_merge_float_inputs(const MergeFloatCase& test_case, PlaneView<float> destination,
                                    PlaneView<float> other) {
  if (destination.width() != test_case.width_pixels ||
      destination.height() != test_case.height_pixels || other.width() != test_case.width_pixels ||
      other.height() != test_case.height_pixels) {
    throw std::invalid_argument("Merge float input dimensions do not match");
  }

  XorShift32 generator(test_case.seed);
  constexpr std::array<float, 13> anchors{-1024.0F, -768.0F, -512.0F, -64.0F, -1.0F,  -0.001F, 0.0F,
                                          0.001F,   1.0F,    64.0F,   512.0F, 768.0F, 1024.0F};
  const auto anchor_offset = static_cast<std::size_t>(test_case.seed % anchors.size());

  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto index = y * test_case.width_pixels + x;
      if (test_case.input_pattern == FloatInputPattern::RandomBounded) {
        destination.row(y)[x] = bounded_random_float(generator);
        other.row(y)[x] = bounded_random_float(generator);
      } else {
        const float first = anchors[(index + anchor_offset) % anchors.size()];
        const float delta = anchors[(index * 5 + 3) % anchors.size()] * 0.0078125F;
        destination.row(y)[x] = first;
        other.row(y)[x] = std::clamp(-first + delta, -1024.0F, 1024.0F);
      }
    }
  }
}

inline void apply_merge_float_reference(const MergeFloatCase& test_case,
                                        PlaneView<float> destination,
                                        PlaneView<const float> other) {
  if (destination.width() != test_case.width_pixels ||
      destination.height() != test_case.height_pixels || other.width() != test_case.width_pixels ||
      other.height() != test_case.height_pixels) {
    throw std::invalid_argument("Merge float reference dimensions do not match");
  }

  // Keep the upstream operation's float complement, but evaluate the
  // independent reference products and sum in double before rounding to float.
  const float inverse_weight = 1.0F - test_case.weight;
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const double first = static_cast<double>(destination.row(y)[x]);
      const double second = static_cast<double>(other.row(y)[x]);
      destination.row(y)[x] = static_cast<float>(first * static_cast<double>(inverse_weight) +
                                                 second * static_cast<double>(test_case.weight));
    }
  }
}

inline void invoke_merge_float(MergeFloatFuncPtr function, PlaneView<float> destination,
                               PlaneView<const float> other, const MergeFloatCase& test_case) {
  function(reinterpret_cast<BYTE*>(destination.data()), reinterpret_cast<const BYTE*>(other.data()),
           static_cast<int>(destination.pitch_bytes()), static_cast<int>(other.pitch_bytes()),
           static_cast<int>(test_case.width_pixels), static_cast<int>(test_case.height_pixels),
           test_case.weight);
}

inline std::uint64_t float_ulp_distance(float lhs, float rhs) noexcept {
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
  const std::uint32_t lhs_magnitude = lhs_bits & ~sign_bit;
  const std::uint32_t rhs_magnitude = rhs_bits & ~sign_bit;
  return lhs_magnitude >= rhs_magnitude ? static_cast<std::uint64_t>(lhs_magnitude - rhs_magnitude)
                                        : static_cast<std::uint64_t>(rhs_magnitude - lhs_magnitude);
}

inline ::testing::AssertionResult require_finite(PlaneView<const float> actual) {
  for (std::size_t y = 0; y < actual.height(); ++y) {
    for (std::size_t x = 0; x < actual.width(); ++x) {
      const float value = actual.row(y)[x];
      if (!std::isfinite(value)) {
        return ::testing::AssertionFailure()
               << "row=" << y << " col=" << x << " non-finite output=" << value;
      }
    }
  }
  return ::testing::AssertionSuccess();
}

inline ::testing::AssertionResult compare_float_ulp(PlaneView<const float> expected,
                                                    PlaneView<const float> actual,
                                                    std::uint64_t maximum_ulps = 4,
                                                    float absolute_floor = 1.0e-4F) {
  if (expected.width() != actual.width() || expected.height() != actual.height()) {
    return ::testing::AssertionFailure() << "dimension mismatch";
  }
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
      const auto ulps = float_ulp_distance(lhs, rhs);
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

inline void run_merge_float_case(const MergeFloatCase& test_case) {
  GuardedVideoBuffer<float> destination(test_case.width_pixels, test_case.height_pixels,
                                        test_case.destination_pitch, 32, 4);
  GuardedVideoBuffer<float> other(test_case.width_pixels, test_case.height_pixels,
                                  test_case.other_pitch, 32, 4);
  GuardedVideoBuffer<float> expected(test_case.width_pixels, test_case.height_pixels,
                                     test_case.destination_pitch, 32, 4);
  GuardedVideoBuffer<float> scalar(test_case.width_pixels, test_case.height_pixels,
                                   test_case.destination_pitch, 32, 4);
  GuardedVideoBuffer<float> actual(test_case.width_pixels, test_case.height_pixels,
                                   test_case.destination_pitch, 32, 4);

  fill_merge_float_inputs(test_case, destination.view(), other.view());
  const auto other_snapshot = other.snapshot_active();
  copy_active(destination.view().as_const(), expected.view());
  copy_active(destination.view().as_const(), scalar.view());
  copy_active(destination.view().as_const(), actual.view());

  apply_merge_float_reference(test_case, expected.view(), other.view().as_const());
  invoke_merge_float(test_case.scalar_function, scalar.view(), other.view().as_const(), test_case);
  invoke_merge_float(test_case.variant.function, actual.view(), other.view().as_const(), test_case);

  ASSERT_TRUE(require_finite(scalar.view().as_const())) << test_case.name;
  ASSERT_TRUE(require_finite(actual.view().as_const())) << test_case.name;
  EXPECT_TRUE(compare_float_ulp(expected.view().as_const(), scalar.view().as_const()))
      << test_case.name << " scalar reference mismatch";
  EXPECT_TRUE(compare_float_ulp(scalar.view().as_const(), actual.view().as_const()))
      << test_case.name << " variant differential mismatch";
  EXPECT_TRUE(other.active_matches(other_snapshot))
      << test_case.name << " modified the second input";
  EXPECT_TRUE(destination.memory_intact())
      << test_case.name << " initial destination memory was corrupted";
  EXPECT_TRUE(other.memory_intact())
      << test_case.name << " second input padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(scalar.memory_intact())
      << test_case.name << " scalar padding or guards were corrupted";
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " variant padding or guards were corrupted";
}

}  // namespace avsut::test
