#pragma once

#include "focus_test_helpers.h"

#include <cmath>
#include <cstring>
#include <type_traits>

namespace avsut::test {

using FocusHorizontalFloatFuncPtr = void (*)(BYTE*, std::size_t, std::size_t, std::size_t, float);
using FocusVerticalFloatFuncPtr = void (*)(BYTE*, BYTE*, int, int, int, float);

struct FocusHorizontalFloatCase {
  std::size_t width{};
  std::size_t height{};
  std::size_t pitch{};
  float amount{};
  std::string amount_name;
  Variant<FocusHorizontalFloatFuncPtr> variant;
  std::string name;
};

struct FocusVerticalFloatCase {
  std::size_t width{};
  std::size_t height{};
  std::size_t pitch{};
  float amount{};
  std::string amount_name;
  Variant<FocusVerticalFloatFuncPtr> variant;
  std::string name;
};

template <typename Function>
inline std::string focus_float_case_name(const char* format, std::size_t width, std::size_t height,
                                         std::size_t pitch, const std::string& amount_name,
                                         const Variant<Function>& variant) {
  std::ostringstream stream;
  stream << format << "_Width" << width << "_Height" << height << "_Pitch" << pitch << "_Amount"
         << amount_name << "_PatternFiniteAnchors_" << focus_variant_name(variant);
  return stream.str();
}

inline FocusHorizontalFloatCase make_focus_horizontal_float_case(
    std::size_t width, std::size_t height, std::size_t pitch, float amount, std::string amount_name,
    Variant<FocusHorizontalFloatFuncPtr> variant) {
  FocusHorizontalFloatCase result{
      width, height, pitch, amount, std::move(amount_name), std::move(variant), {}};
  result.name = focus_float_case_name("PlaneFloatHorizontal", result.width, result.height,
                                      result.pitch, result.amount_name, result.variant);
  return result;
}

inline FocusVerticalFloatCase make_focus_vertical_float_case(
    std::size_t width, std::size_t height, std::size_t pitch, float amount, std::string amount_name,
    Variant<FocusVerticalFloatFuncPtr> variant) {
  FocusVerticalFloatCase result{
      width, height, pitch, amount, std::move(amount_name), std::move(variant), {}};
  result.name = focus_float_case_name("PlaneFloatVertical", result.width, result.height,
                                      result.pitch, result.amount_name, result.variant);
  return result;
}

inline void PrintTo(const FocusHorizontalFloatCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const FocusVerticalFloatCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_focus_float_input(PlaneView<float> view) {
  constexpr std::array<float, 9> anchors{-1000.0F, -64.0F, -1.0F, -0.5F,  0.0F,
                                         0.5F,     1.0F,   64.0F, 1000.0F};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      const auto index = y * view.width() + x;
      if ((index % 5U) == 0U) {
        view.row(y)[x] = anchors[(index / 5U) % anchors.size()];
      } else {
        const auto value = static_cast<int>((x * 37U + y * 101U + index * 13U) % 17U) - 8;
        view.row(y)[x] = static_cast<float>(value) * 0.375F;
      }
    }
  }
}

inline float focus_float_reference_pixel(float left, float center, float right, float amount) {
  const float outer = (1.0F - amount) / 2.0F;
  return static_cast<float>(static_cast<double>(center) * amount +
                            (static_cast<double>(left) + right) * outer);
}

inline ::testing::AssertionResult compare_focus_float(PlaneView<const float> expected,
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
      const float difference = std::abs(lhs - rhs);
      if (difference <= absolute_floor) {
        continue;
      }
      std::uint32_t lhs_bits{};
      std::uint32_t rhs_bits{};
      std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
      std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
      constexpr std::uint32_t sign_bit = 0x80000000U;
      const auto ulps = (lhs_bits & sign_bit) != (rhs_bits & sign_bit)
                            ? std::numeric_limits<std::uint64_t>::max()
                            : static_cast<std::uint64_t>(
                                  lhs_bits >= rhs_bits ? lhs_bits - rhs_bits : rhs_bits - lhs_bits);
      if (ulps > maximum_ulps) {
        return ::testing::AssertionFailure()
               << "row=" << y << " col=" << x << " expected=" << lhs << " actual=" << rhs
               << " difference=" << difference << " ulps=" << ulps;
      }
    }
  }
  return ::testing::AssertionSuccess();
}

inline void apply_focus_horizontal_float_reference(PlaneView<const float> source,
                                                   PlaneView<float> destination, float amount) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < source.width(); ++x) {
      const float left = source.row(y)[x == 0 ? 0 : x - 1];
      const float right = source.row(y)[x + 1 == source.width() ? x : x + 1];
      destination.row(y)[x] = focus_float_reference_pixel(left, source.row(y)[x], right, amount);
    }
  }
}

inline void apply_focus_vertical_float_reference(PlaneView<const float> source,
                                                 PlaneView<float> destination, float amount) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    const auto* upper = source.row(y == 0 ? 0 : y - 1);
    const auto* center = source.row(y);
    const auto* lower = source.row(y + 1 == source.height() ? y : y + 1);
    for (std::size_t x = 0; x < source.width(); ++x) {
      destination.row(y)[x] = focus_float_reference_pixel(upper[x], center[x], lower[x], amount);
    }
  }
}

inline void run_focus_horizontal_float_case(const FocusHorizontalFloatCase& test_case) {
  GuardedVideoBuffer<float> input(test_case.width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<float> actual(test_case.width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<float> expected(test_case.width, test_case.height, test_case.pitch, 32);
  fill_focus_float_input(input.view());
  copy_focus_active(input.view().as_const(), actual.view());
  copy_focus_active(input.view().as_const(), expected.view());
  apply_focus_horizontal_float_reference(input.view().as_const(), expected.view(),
                                         test_case.amount);

  test_case.variant.function(reinterpret_cast<BYTE*>(actual.view().data()), test_case.height,
                             test_case.pitch, test_case.width * sizeof(float), test_case.amount);

  EXPECT_TRUE(compare_focus_float(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(input.memory_intact()) << test_case.name << " input padding or guards were corrupted";
}

inline void run_focus_vertical_float_case(const FocusVerticalFloatCase& test_case) {
  const auto active_row_bytes = test_case.width * sizeof(float);
  GuardedVideoBuffer<float> actual(test_case.width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<float> expected(test_case.width, test_case.height, test_case.pitch, 32);
  GuardedVideoBuffer<std::uint8_t> line_buffer(active_row_bytes, 1, active_row_bytes, 32);
  fill_focus_float_input(actual.view());
  copy_focus_active(actual.view().as_const(), expected.view());
  apply_focus_vertical_float_reference(actual.view().as_const(), expected.view(), test_case.amount);
  std::copy_n(reinterpret_cast<const std::uint8_t*>(actual.view().data()), active_row_bytes,
              line_buffer.view().data());

  test_case.variant.function(line_buffer.view().data(),
                             reinterpret_cast<BYTE*>(actual.view().data()),
                             static_cast<int>(test_case.height), static_cast<int>(test_case.pitch),
                             static_cast<int>(active_row_bytes), test_case.amount);

  EXPECT_TRUE(compare_focus_float(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(line_buffer.memory_intact())
      << test_case.name << " line buffer padding or guards were corrupted";
}

}  // namespace avsut::test
