#pragma once

#include "filters/overlay/blend_common.h"
#include "filters/overlay/intel/blend_common_avx2.h"
#include "filters/overlay/intel/blend_common_sse.h"

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
#include <string>
#include <type_traits>
#include <utility>

namespace avsut::test {

using OverlayMaskedFuncPtr = masked_merge_fn_t*;
using OverlayMaskedFloatFuncPtr = masked_merge_float_fn_t*;

struct OverlayIntegerCase {
  MaskMode mask_mode{};
  bool is_chroma{};
  int bits_per_pixel{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t destination_pitch{};
  std::size_t mask_width_pixels{};
  std::size_t mask_height_pixels{};
  std::size_t mask_pitch{};
  int opacity{};
  std::string opacity_label;
  OverlayMaskedFuncPtr scalar_function{};
  Variant<OverlayMaskedFuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

struct OverlayFloatCase {
  MaskMode mask_mode{};
  bool is_chroma{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t destination_pitch{};
  std::size_t mask_width_pixels{};
  std::size_t mask_height_pixels{};
  std::size_t mask_pitch{};
  float opacity{};
  std::string opacity_label;
  OverlayMaskedFloatFuncPtr scalar_function{};
  Variant<OverlayMaskedFloatFuncPtr> variant;
  std::string name;
};

template <typename Function>
inline std::string overlay_variant_name(const Variant<Function>& variant) {
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

inline const char* overlay_mask_mode_name(MaskMode mode) {
  switch (mode) {
    case MASK420:
      return "Mask420";
    case MASK422:
      return "Mask422";
    case MASK444:
      return "Mask444";
    default:
      return "MaskOther";
  }
}

inline std::size_t overlay_round_up(std::size_t value, std::size_t alignment) {
  return ((value + alignment - 1) / alignment) * alignment;
}

inline std::size_t overlay_mask_width(MaskMode mode, std::size_t width) {
  return mode == MASK444 ? width : width * 2;
}

inline std::size_t overlay_mask_height(MaskMode mode, std::size_t height) {
  return mode == MASK420 ? height * 2 : height;
}

inline std::size_t overlay_pitch(std::size_t width, std::size_t bytes_per_pixel) {
  return overlay_round_up(width * bytes_per_pixel, 32);
}

inline std::string overlay_integer_case_name(const OverlayIntegerCase& test_case) {
  std::ostringstream stream;
  stream << overlay_mask_mode_name(test_case.mask_mode) << "_Bpp" << test_case.bits_per_pixel
         << "_Width" << test_case.width_pixels << "_Height" << test_case.height_pixels
         << "_DstPitch" << test_case.destination_pitch << "_MaskPitch" << test_case.mask_pitch
         << "_Opacity" << test_case.opacity_label;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryValues_" : "_PatternFixedRandom_")
         << overlay_variant_name(test_case.variant);
  return stream.str();
}

inline std::string overlay_float_case_name(const OverlayFloatCase& test_case) {
  std::ostringstream stream;
  stream << overlay_mask_mode_name(test_case.mask_mode) << "Float_Width" << test_case.width_pixels
         << "_Height" << test_case.height_pixels << "_DstPitch" << test_case.destination_pitch
         << "_MaskPitch" << test_case.mask_pitch << "_Opacity" << test_case.opacity_label
         << "_PatternBoundaryValues_" << overlay_variant_name(test_case.variant);
  return stream.str();
}

inline OverlayIntegerCase make_overlay_integer_case(
    MaskMode mask_mode, int bits_per_pixel, std::size_t width_pixels, std::size_t height_pixels,
    int opacity, std::string opacity_label, OverlayMaskedFuncPtr scalar_function,
    Variant<OverlayMaskedFuncPtr> variant, std::string expected_hash = {},
    std::uint32_t seed = 0) {
  const auto bytes_per_pixel = bits_per_pixel == 8 ? std::size_t{1} : std::size_t{2};
  const auto mask_width_pixels = overlay_mask_width(mask_mode, width_pixels);
  const auto mask_height_pixels = overlay_mask_height(mask_mode, height_pixels);
  OverlayIntegerCase result{mask_mode,
                            mask_mode != MASK444,
                            bits_per_pixel,
                            width_pixels,
                            height_pixels,
                            overlay_pitch(width_pixels, bytes_per_pixel),
                            mask_width_pixels,
                            mask_height_pixels,
                            overlay_pitch(mask_width_pixels, bytes_per_pixel),
                            opacity,
                            std::move(opacity_label),
                            scalar_function,
                            std::move(variant),
                            std::move(expected_hash),
                            seed,
                            {}};
  result.name = overlay_integer_case_name(result);
  return result;
}

inline OverlayFloatCase make_overlay_float_case(MaskMode mask_mode, std::size_t width_pixels,
                                                std::size_t height_pixels, float opacity,
                                                std::string opacity_label,
                                                OverlayMaskedFloatFuncPtr scalar_function,
                                                Variant<OverlayMaskedFloatFuncPtr> variant) {
  const auto mask_width_pixels = overlay_mask_width(mask_mode, width_pixels);
  const auto mask_height_pixels = overlay_mask_height(mask_mode, height_pixels);
  OverlayFloatCase result{mask_mode,
                          mask_mode != MASK444,
                          width_pixels,
                          height_pixels,
                          overlay_pitch(width_pixels, sizeof(float)),
                          mask_width_pixels,
                          mask_height_pixels,
                          overlay_pitch(mask_width_pixels, sizeof(float)),
                          opacity,
                          std::move(opacity_label),
                          scalar_function,
                          std::move(variant),
                          {}};
  result.name = overlay_float_case_name(result);
  return result;
}

inline void PrintTo(const OverlayIntegerCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const OverlayFloatCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_overlay_integer_plane(PlaneView<T> view, std::uint32_t max_value, std::size_t offset) {
  static_assert(std::is_integral_v<T>);
  const std::array<std::uint32_t, 10> values{0,
                                             1,
                                             max_value / 4,
                                             max_value / 3,
                                             max_value / 2,
                                             (max_value * 2) / 3,
                                             (max_value * 3) / 4,
                                             max_value - 1,
                                             max_value,
                                             max_value / 8};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      view.row(y)[x] = static_cast<T>(values[(y * view.width() + x + offset) % values.size()]);
    }
  }
}

inline void fill_overlay_float_plane(PlaneView<float> view, std::size_t offset, bool mask) {
  constexpr std::array<float, 10> values{-1024.0F, -64.0F, -1.0F, -0.125F, 0.0F,
                                         0.125F,   1.0F,   64.0F, 1024.0F, 0.5F};
  constexpr std::array<float, 8> masks{0.0F, 0.125F, 0.25F, 0.5F, 0.625F, 0.75F, 0.875F, 1.0F};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      const auto index = y * view.width() + x + offset;
      view.row(y)[x] = mask ? masks[index % masks.size()] : values[index % values.size()];
    }
  }
}

template <typename T>
void copy_overlay_active(PlaneView<const T> source, PlaneView<T> destination) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    std::copy_n(source.row(y), source.width(), destination.row(y));
  }
}

inline std::uint32_t overlay_divide_by_max(std::uint64_t numerator, std::uint32_t max_value) {
  return static_cast<std::uint32_t>(numerator / max_value);
}

template <typename T>
std::uint32_t overlay_effective_mask(const OverlayIntegerCase& test_case, PlaneView<const T> mask,
                                     std::size_t x, std::size_t y) {
  std::uint32_t raw = 0;
  if (test_case.mask_mode == MASK444) {
    raw = mask.row(y)[x];
  } else if (test_case.mask_mode == MASK422) {
    raw = (static_cast<std::uint32_t>(mask.row(y)[x * 2]) +
           static_cast<std::uint32_t>(mask.row(y)[x * 2 + 1]) + 1U) >>
          1;
  } else if (test_case.mask_mode == MASK420) {
    const auto* row0 = mask.row(y * 2);
    const auto* row1 = mask.row(y * 2 + 1);
    raw = (static_cast<std::uint32_t>(row0[x * 2]) + static_cast<std::uint32_t>(row0[x * 2 + 1]) +
           static_cast<std::uint32_t>(row1[x * 2]) + static_cast<std::uint32_t>(row1[x * 2 + 1]) +
           2U) >>
          2;
  }

  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  if (test_case.opacity == static_cast<int>(max_value)) {
    return raw;
  }
  return overlay_divide_by_max(
      static_cast<std::uint64_t>(raw) * static_cast<std::uint32_t>(test_case.opacity) +
          max_value / 2U,
      max_value);
}

template <typename T>
void apply_overlay_integer_reference(const OverlayIntegerCase& test_case, PlaneView<T> destination,
                                     PlaneView<const T> overlay, PlaneView<const T> mask) {
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto effective_mask = overlay_effective_mask(test_case, mask, x, y);
      const auto first = static_cast<std::uint32_t>(destination.row(y)[x]);
      const auto second = static_cast<std::uint32_t>(overlay.row(y)[x]);
      const auto numerator = static_cast<std::uint64_t>(first) * (max_value - effective_mask) +
                             static_cast<std::uint64_t>(second) * effective_mask + max_value / 2U;
      destination.row(y)[x] = static_cast<T>(overlay_divide_by_max(numerator, max_value));
    }
  }
}

template <typename T>
void run_overlay_integer_case_typed(const OverlayIntegerCase& test_case) {
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  GuardedVideoBuffer<T> destination(test_case.width_pixels, test_case.height_pixels,
                                    test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> overlay(test_case.width_pixels, test_case.height_pixels,
                                test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> mask(test_case.mask_width_pixels, test_case.mask_height_pixels,
                             test_case.mask_pitch, 32);
  GuardedVideoBuffer<T> expected(test_case.width_pixels, test_case.height_pixels,
                                 test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> scalar(test_case.width_pixels, test_case.height_pixels,
                               test_case.destination_pitch, 32);

  if (test_case.seed != 0) {
    fill_random(destination.view(), test_case.seed);
    fill_random(overlay.view(), test_case.seed ^ 0xA5A5A5A5U);
    fill_random(mask.view(), test_case.seed ^ 0x5A5A5A5AU);
  } else {
    fill_overlay_integer_plane(destination.view(), max_value, 0);
    fill_overlay_integer_plane(overlay.view(), max_value, 3);
    fill_overlay_integer_plane(mask.view(), max_value, 6);
  }
  const auto overlay_snapshot = overlay.snapshot_active();
  const auto mask_snapshot = mask.snapshot_active();
  copy_overlay_active(destination.view().as_const(), expected.view());
  copy_overlay_active(destination.view().as_const(), scalar.view());
  apply_overlay_integer_reference(test_case, expected.view(), overlay.view().as_const(),
                                  mask.view().as_const());

  test_case.scalar_function(
      reinterpret_cast<BYTE*>(scalar.view().data()),
      reinterpret_cast<const BYTE*>(overlay.view().data()),
      reinterpret_cast<const BYTE*>(mask.view().data()),
      static_cast<int>(scalar.view().pitch_bytes()), static_cast<int>(overlay.view().pitch_bytes()),
      static_cast<int>(mask.view().pitch_bytes()), static_cast<int>(test_case.width_pixels),
      static_cast<int>(test_case.height_pixels), test_case.opacity, test_case.bits_per_pixel);
  test_case.variant.function(
      reinterpret_cast<BYTE*>(destination.view().data()),
      reinterpret_cast<const BYTE*>(overlay.view().data()),
      reinterpret_cast<const BYTE*>(mask.view().data()),
      static_cast<int>(destination.view().pitch_bytes()),
      static_cast<int>(overlay.view().pitch_bytes()), static_cast<int>(mask.view().pitch_bytes()),
      static_cast<int>(test_case.width_pixels), static_cast<int>(test_case.height_pixels),
      test_case.opacity, test_case.bits_per_pixel);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), scalar.view().as_const()))
      << test_case.name << " reference mismatch for C implementation";
  EXPECT_TRUE(compare_exact(scalar.view().as_const(), destination.view().as_const()))
      << test_case.name << " C/SIMD differential mismatch for variant " << test_case.variant.name;
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(overlay.active_matches(overlay_snapshot))
      << test_case.name << " modified the overlay input";
  EXPECT_TRUE(mask.active_matches(mask_snapshot)) << test_case.name << " modified the mask input";
  EXPECT_TRUE(destination.memory_intact())
      << test_case.name << " destination padding or guards were corrupted";
  EXPECT_TRUE(overlay.memory_intact())
      << test_case.name << " overlay padding or guards were corrupted";
  EXPECT_TRUE(mask.memory_intact()) << test_case.name << " mask padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(scalar.memory_intact())
      << test_case.name << " C output padding or guards were corrupted";
}

inline std::uint64_t overlay_float_ulp_distance(float lhs, float rhs) noexcept {
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

inline ::testing::AssertionResult compare_overlay_float(PlaneView<const float> expected,
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
      const auto ulps = overlay_float_ulp_distance(lhs, rhs);
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

inline ::testing::AssertionResult overlay_float_finite(PlaneView<const float> view) {
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      if (!std::isfinite(view.row(y)[x])) {
        return ::testing::AssertionFailure()
               << "row=" << y << " col=" << x << " non-finite output=" << view.row(y)[x];
      }
    }
  }
  return ::testing::AssertionSuccess();
}

inline float overlay_float_mask_sample(const OverlayFloatCase& test_case,
                                       PlaneView<const float> mask, std::size_t x, std::size_t y) {
  if (test_case.mask_mode == MASK444) {
    return mask.row(y)[x];
  }
  if (test_case.mask_mode == MASK422) {
    return (mask.row(y)[x * 2] + mask.row(y)[x * 2 + 1]) * 0.5F;
  }
  const auto* row0 = mask.row(y * 2);
  const auto* row1 = mask.row(y * 2 + 1);
  return (row0[x * 2] + row0[x * 2 + 1] + row1[x * 2] + row1[x * 2 + 1]) * 0.25F;
}

inline void apply_overlay_float_reference(const OverlayFloatCase& test_case,
                                          PlaneView<float> destination,
                                          PlaneView<const float> overlay,
                                          PlaneView<const float> mask) {
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const double effective_mask =
          static_cast<double>(overlay_float_mask_sample(test_case, mask, x, y)) *
          static_cast<double>(test_case.opacity);
      const double first = destination.row(y)[x];
      const double second = overlay.row(y)[x];
      destination.row(y)[x] = static_cast<float>(first + effective_mask * (second - first));
    }
  }
}

inline void run_overlay_float_case(const OverlayFloatCase& test_case) {
  GuardedVideoBuffer<float> destination(test_case.width_pixels, test_case.height_pixels,
                                        test_case.destination_pitch, 32, 4);
  GuardedVideoBuffer<float> overlay(test_case.width_pixels, test_case.height_pixels,
                                    test_case.destination_pitch, 32, 4);
  GuardedVideoBuffer<float> mask(test_case.mask_width_pixels, test_case.mask_height_pixels,
                                 test_case.mask_pitch, 32, 4);
  GuardedVideoBuffer<float> expected(test_case.width_pixels, test_case.height_pixels,
                                     test_case.destination_pitch, 32, 4);
  GuardedVideoBuffer<float> scalar(test_case.width_pixels, test_case.height_pixels,
                                   test_case.destination_pitch, 32, 4);

  fill_overlay_float_plane(destination.view(), 0, false);
  fill_overlay_float_plane(overlay.view(), 3, false);
  fill_overlay_float_plane(mask.view(), 6, true);
  const auto overlay_snapshot = overlay.snapshot_active();
  const auto mask_snapshot = mask.snapshot_active();
  copy_overlay_active(destination.view().as_const(), expected.view());
  copy_overlay_active(destination.view().as_const(), scalar.view());
  apply_overlay_float_reference(test_case, expected.view(), overlay.view().as_const(),
                                mask.view().as_const());

  test_case.scalar_function(
      reinterpret_cast<BYTE*>(scalar.view().data()),
      reinterpret_cast<const BYTE*>(overlay.view().data()),
      reinterpret_cast<const BYTE*>(mask.view().data()),
      static_cast<int>(scalar.view().pitch_bytes()), static_cast<int>(overlay.view().pitch_bytes()),
      static_cast<int>(mask.view().pitch_bytes()), static_cast<int>(test_case.width_pixels),
      static_cast<int>(test_case.height_pixels), test_case.opacity);
  test_case.variant.function(reinterpret_cast<BYTE*>(destination.view().data()),
                             reinterpret_cast<const BYTE*>(overlay.view().data()),
                             reinterpret_cast<const BYTE*>(mask.view().data()),
                             static_cast<int>(destination.view().pitch_bytes()),
                             static_cast<int>(overlay.view().pitch_bytes()),
                             static_cast<int>(mask.view().pitch_bytes()),
                             static_cast<int>(test_case.width_pixels),
                             static_cast<int>(test_case.height_pixels), test_case.opacity);

  ASSERT_TRUE(overlay_float_finite(scalar.view().as_const())) << test_case.name;
  ASSERT_TRUE(overlay_float_finite(destination.view().as_const())) << test_case.name;
  EXPECT_TRUE(compare_overlay_float(expected.view().as_const(), scalar.view().as_const()))
      << test_case.name << " reference mismatch for C implementation";
  EXPECT_TRUE(compare_overlay_float(scalar.view().as_const(), destination.view().as_const()))
      << test_case.name << " C/SIMD differential mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(overlay.active_matches(overlay_snapshot))
      << test_case.name << " modified the overlay input";
  EXPECT_TRUE(mask.active_matches(mask_snapshot)) << test_case.name << " modified the mask input";
  EXPECT_TRUE(destination.memory_intact())
      << test_case.name << " destination padding or guards were corrupted";
  EXPECT_TRUE(overlay.memory_intact())
      << test_case.name << " overlay padding or guards were corrupted";
  EXPECT_TRUE(mask.memory_intact()) << test_case.name << " mask padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(scalar.memory_intact())
      << test_case.name << " C output padding or guards were corrupted";
}

}  // namespace avsut::test
