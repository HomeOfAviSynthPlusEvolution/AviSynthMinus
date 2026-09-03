#pragma once

#include "filters/overlay/intel/OF_multiply_avx2.h"
#include "filters/overlay/intel/OF_multiply_sse.h"

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
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace avsut::test {

using OverlayMultiplyFunction = void (*)(int, float, int, int, int, const void*, int, void*, void*,
                                         void*, int, const void*, const void*, const void*, int);

template <typename T, bool OpacityIsFull, bool HasMask>
inline void overlay_multiply_sse_wrapper(int bits_per_pixel, float opacity_f, int opacity,
                                         int width, int height, const void* overlay,
                                         int overlay_pitch, void* base_y, void* base_u,
                                         void* base_v, int base_pitch, const void* mask_y,
                                         const void* mask_u, const void* mask_v, int mask_pitch) {
  of_multiply_sse41<T, OpacityIsFull, HasMask>(
      bits_per_pixel, opacity_f, opacity, width, height, static_cast<const T*>(overlay),
      overlay_pitch, static_cast<T*>(base_y), static_cast<T*>(base_u), static_cast<T*>(base_v),
      base_pitch, static_cast<const T*>(mask_y), static_cast<const T*>(mask_u),
      static_cast<const T*>(mask_v), mask_pitch);
}

template <typename T, bool OpacityIsFull, bool HasMask>
inline void overlay_multiply_avx2_wrapper(int bits_per_pixel, float opacity_f, int opacity,
                                          int width, int height, const void* overlay,
                                          int overlay_pitch, void* base_y, void* base_u,
                                          void* base_v, int base_pitch, const void* mask_y,
                                          const void* mask_u, const void* mask_v, int mask_pitch) {
  of_multiply_avx2<T, OpacityIsFull, HasMask>(
      bits_per_pixel, opacity_f, opacity, width, height, static_cast<const T*>(overlay),
      overlay_pitch, static_cast<T*>(base_y), static_cast<T*>(base_u), static_cast<T*>(base_v),
      base_pitch, static_cast<const T*>(mask_y), static_cast<const T*>(mask_u),
      static_cast<const T*>(mask_v), mask_pitch);
}

struct OverlayMultiplyCase {
  int bits_per_pixel{};
  bool has_mask{};
  bool opacity_is_full{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t base_pitch_bytes{};
  std::size_t overlay_pitch_bytes{};
  std::size_t mask_pitch_bytes{};
  std::size_t base_alignment_offset{};
  std::size_t overlay_alignment_offset{};
  std::size_t mask_alignment_offset{};
  float opacity_f{};
  int opacity{};
  std::string opacity_label;
  Variant<OverlayMultiplyFunction> variant;
  std::array<std::string, 3> expected_hashes;
  std::uint32_t seed{};
  std::string name;
};

inline std::string overlay_multiply_variant_name(const Variant<OverlayMultiplyFunction>& variant) {
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

inline std::string overlay_multiply_case_name(const OverlayMultiplyCase& test_case) {
  std::ostringstream stream;
  stream << (test_case.has_mask ? "Masked" : "Unmasked") << "_Bpp" << test_case.bits_per_pixel
         << "_Width" << test_case.width_pixels << "_Height" << test_case.height_pixels
         << "_BasePitch" << test_case.base_pitch_bytes << "_OverlayPitch"
         << test_case.overlay_pitch_bytes;
  if (test_case.has_mask) {
    stream << "_MaskPitch" << test_case.mask_pitch_bytes;
  }
  stream << "_BaseOffset" << test_case.base_alignment_offset << "_OverlayOffset"
         << test_case.overlay_alignment_offset;
  if (test_case.has_mask) {
    stream << "_MaskOffset" << test_case.mask_alignment_offset;
  }
  stream << "_Opacity" << test_case.opacity_label;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryAnchors_" : "_PatternFixedRandom_")
         << overlay_multiply_variant_name(test_case.variant);
  return stream.str();
}

inline OverlayMultiplyCase make_overlay_multiply_case(
    int bits_per_pixel, bool has_mask, bool opacity_is_full, std::size_t width_pixels,
    std::size_t height_pixels, std::size_t base_pitch_bytes, std::size_t overlay_pitch_bytes,
    std::size_t mask_pitch_bytes, std::size_t base_alignment_offset,
    std::size_t overlay_alignment_offset, std::size_t mask_alignment_offset, float opacity_f,
    int opacity, std::string opacity_label, Variant<OverlayMultiplyFunction> variant,
    std::array<std::string, 3> expected_hashes = {}, std::uint32_t seed = 0) {
  const auto bytes_per_pixel = bits_per_pixel == 8 ? std::size_t{1} : std::size_t{2};
  const auto active_row_bytes = width_pixels * bytes_per_pixel;
  if ((bits_per_pixel != 8 && bits_per_pixel != 10 && bits_per_pixel != 16) || width_pixels == 0 ||
      height_pixels == 0 || base_pitch_bytes < active_row_bytes ||
      overlay_pitch_bytes < active_row_bytes || (has_mask && mask_pitch_bytes < active_row_bytes) ||
      base_alignment_offset >= 32 || overlay_alignment_offset >= 32 ||
      mask_alignment_offset >= 32 || opacity_f < 0.0F || opacity_f > 1.0F) {
    throw std::invalid_argument("invalid Overlay multiply test dimensions or parameters");
  }
  OverlayMultiplyCase result{bits_per_pixel,
                             has_mask,
                             opacity_is_full,
                             width_pixels,
                             height_pixels,
                             base_pitch_bytes,
                             overlay_pitch_bytes,
                             mask_pitch_bytes,
                             base_alignment_offset,
                             overlay_alignment_offset,
                             mask_alignment_offset,
                             opacity_f,
                             opacity,
                             std::move(opacity_label),
                             std::move(variant),
                             std::move(expected_hashes),
                             seed,
                             {}};
  result.name = overlay_multiply_case_name(result);
  return result;
}

inline void PrintTo(const OverlayMultiplyCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_overlay_multiply_plane(PlaneView<T> view, std::uint32_t max_value, std::size_t offset) {
  constexpr std::array<std::uint32_t, 11> fractions{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      const auto fraction = fractions[(x + 3 * y + offset) % fractions.size()];
      view.row(y)[x] = static_cast<T>((fraction * max_value + 5U) / 10U);
    }
  }
}

template <typename T>
void copy_overlay_multiply_active(PlaneView<const T> source, PlaneView<T> destination) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    std::copy_n(source.row(y), source.width(), destination.row(y));
  }
}

template <typename T>
void apply_overlay_multiply_reference(const OverlayMultiplyCase& test_case, PlaneView<T> base_y,
                                      PlaneView<T> base_u, PlaneView<T> base_v,
                                      PlaneView<const T> overlay, PlaneView<const T> mask_y,
                                      PlaneView<const T> mask_u, PlaneView<const T> mask_v) {
  const auto max_pixel_value =
      sizeof(T) == 1 ? 255U : ((std::uint32_t{1} << test_case.bits_per_pixel) - 1U);
  const int half_i = sizeof(T) == 1 ? 128 : (1 << (test_case.bits_per_pixel - 1));
  const float factor = 1.0F / static_cast<float>(max_pixel_value);
  const float factor_mul_opacity =
      factor * (test_case.opacity_is_full ? 1.0F : test_case.opacity_f);
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const float overlay_opacity_minus1 = static_cast<float>(overlay.row(y)[x]) * factor - 1.0F;
      if (test_case.has_mask) {
        const float y_factor = 1.0F + overlay_opacity_minus1 *
                                          static_cast<float>(mask_y.row(y)[x]) * factor_mul_opacity;
        const float u_factor = 1.0F + overlay_opacity_minus1 *
                                          static_cast<float>(mask_u.row(y)[x]) * factor_mul_opacity;
        const float v_factor = 1.0F + overlay_opacity_minus1 *
                                          static_cast<float>(mask_v.row(y)[x]) * factor_mul_opacity;
        base_y.row(y)[x] = static_cast<T>(static_cast<int>(base_y.row(y)[x] * y_factor + 0.5F));
        base_u.row(y)[x] = static_cast<T>(static_cast<int>(
            (static_cast<float>(base_u.row(y)[x]) - static_cast<float>(half_i)) * u_factor +
            static_cast<float>(half_i) + 0.5F));
        base_v.row(y)[x] = static_cast<T>(static_cast<int>(
            (static_cast<float>(base_v.row(y)[x]) - static_cast<float>(half_i)) * v_factor +
            static_cast<float>(half_i) + 0.5F));
      } else {
        const float common_factor = 1.0F + overlay_opacity_minus1 * test_case.opacity_f;
        base_y.row(y)[x] =
            static_cast<T>(static_cast<int>(base_y.row(y)[x] * common_factor + 0.5F));
        base_u.row(y)[x] = static_cast<T>(static_cast<int>(
            (static_cast<float>(base_u.row(y)[x]) - static_cast<float>(half_i)) * common_factor +
            static_cast<float>(half_i) + 0.5F));
        base_v.row(y)[x] = static_cast<T>(static_cast<int>(
            (static_cast<float>(base_v.row(y)[x]) - static_cast<float>(half_i)) * common_factor +
            static_cast<float>(half_i) + 0.5F));
      }
    }
  }
}

template <typename T>
void run_overlay_multiply_case_typed(const OverlayMultiplyCase& test_case) {
  const auto base_pitch = test_case.base_pitch_bytes / sizeof(T);
  const auto overlay_pitch = test_case.overlay_pitch_bytes / sizeof(T);
  const auto mask_pitch = test_case.mask_pitch_bytes / sizeof(T);
  GuardedVideoBuffer<T> base_y(test_case.width_pixels, test_case.height_pixels,
                               test_case.base_pitch_bytes, 32, test_case.base_alignment_offset);
  GuardedVideoBuffer<T> base_u(test_case.width_pixels, test_case.height_pixels,
                               test_case.base_pitch_bytes, 32, test_case.base_alignment_offset);
  GuardedVideoBuffer<T> base_v(test_case.width_pixels, test_case.height_pixels,
                               test_case.base_pitch_bytes, 32, test_case.base_alignment_offset);
  GuardedVideoBuffer<T> overlay(test_case.width_pixels, test_case.height_pixels,
                                test_case.overlay_pitch_bytes, 32,
                                test_case.overlay_alignment_offset);
  GuardedVideoBuffer<T> mask_y(test_case.width_pixels, test_case.height_pixels,
                               test_case.mask_pitch_bytes, 32, test_case.mask_alignment_offset);
  GuardedVideoBuffer<T> mask_u(test_case.width_pixels, test_case.height_pixels,
                               test_case.mask_pitch_bytes, 32, test_case.mask_alignment_offset);
  GuardedVideoBuffer<T> mask_v(test_case.width_pixels, test_case.height_pixels,
                               test_case.mask_pitch_bytes, 32, test_case.mask_alignment_offset);
  GuardedVideoBuffer<T> expected_y(test_case.width_pixels, test_case.height_pixels,
                                   test_case.base_pitch_bytes, 32, test_case.base_alignment_offset);
  GuardedVideoBuffer<T> expected_u(test_case.width_pixels, test_case.height_pixels,
                                   test_case.base_pitch_bytes, 32, test_case.base_alignment_offset);
  GuardedVideoBuffer<T> expected_v(test_case.width_pixels, test_case.height_pixels,
                                   test_case.base_pitch_bytes, 32, test_case.base_alignment_offset);

  const auto max_value =
      sizeof(T) == 1 ? 255U : ((std::uint32_t{1} << test_case.bits_per_pixel) - 1U);
  if (test_case.seed != 0) {
    fill_random(base_y.view(), test_case.seed ^ 0x01010101U);
    fill_random(base_u.view(), test_case.seed ^ 0x02020202U);
    fill_random(base_v.view(), test_case.seed ^ 0x03030303U);
    fill_random(overlay.view(), test_case.seed ^ 0xA5A5A5A5U);
    fill_random(mask_y.view(), test_case.seed ^ 0x11111111U);
    fill_random(mask_u.view(), test_case.seed ^ 0x22222222U);
    fill_random(mask_v.view(), test_case.seed ^ 0x33333333U);
  } else {
    fill_overlay_multiply_plane(base_y.view(), max_value, 0);
    fill_overlay_multiply_plane(base_u.view(), max_value, 3);
    fill_overlay_multiply_plane(base_v.view(), max_value, 6);
    fill_overlay_multiply_plane(overlay.view(), max_value, 2);
    fill_overlay_multiply_plane(mask_y.view(), max_value, 1);
    fill_overlay_multiply_plane(mask_u.view(), max_value, 4);
    fill_overlay_multiply_plane(mask_v.view(), max_value, 7);
  }
  copy_overlay_multiply_active(base_y.view().as_const(), expected_y.view());
  copy_overlay_multiply_active(base_u.view().as_const(), expected_u.view());
  copy_overlay_multiply_active(base_v.view().as_const(), expected_v.view());
  apply_overlay_multiply_reference(
      test_case, expected_y.view(), expected_u.view(), expected_v.view(), overlay.view().as_const(),
      mask_y.view().as_const(), mask_u.view().as_const(), mask_v.view().as_const());

  const auto overlay_snapshot = overlay.snapshot_active();
  const auto mask_y_snapshot = mask_y.snapshot_active();
  const auto mask_u_snapshot = mask_u.snapshot_active();
  const auto mask_v_snapshot = mask_v.snapshot_active();
  test_case.variant.function(
      test_case.bits_per_pixel, test_case.opacity_f, test_case.opacity,
      static_cast<int>(test_case.width_pixels), static_cast<int>(test_case.height_pixels),
      overlay.view().data(), static_cast<int>(overlay_pitch), base_y.view().data(),
      base_u.view().data(), base_v.view().data(), static_cast<int>(base_pitch),
      test_case.has_mask ? mask_y.view().data() : nullptr,
      test_case.has_mask ? mask_u.view().data() : nullptr,
      test_case.has_mask ? mask_v.view().data() : nullptr,
      test_case.has_mask ? static_cast<int>(mask_pitch) : 0);

  EXPECT_TRUE(compare_exact(expected_y.view().as_const(), base_y.view().as_const()))
      << test_case.name << " Y reference mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(compare_exact(expected_u.view().as_const(), base_u.view().as_const()))
      << test_case.name << " U reference mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(compare_exact(expected_v.view().as_const(), base_v.view().as_const()))
      << test_case.name << " V reference mismatch for variant " << test_case.variant.name;

  const std::array<std::string, 3> actual_hashes{
      format_hash(hash_active(base_y.view().as_const())),
      format_hash(hash_active(base_u.view().as_const())),
      format_hash(hash_active(base_v.view().as_const()))};
  for (std::size_t plane = 0; plane < actual_hashes.size(); ++plane) {
    if (!test_case.expected_hashes[plane].empty()) {
      EXPECT_EQ(actual_hashes[plane], test_case.expected_hashes[plane])
          << test_case.name << " stable hash mismatch for plane " << plane
          << "; actual=" << actual_hashes[plane];
    }
  }

  EXPECT_TRUE(overlay.active_matches(overlay_snapshot)) << test_case.name << " modified overlay";
  EXPECT_TRUE(mask_y.active_matches(mask_y_snapshot)) << test_case.name << " modified mask Y";
  EXPECT_TRUE(mask_u.active_matches(mask_u_snapshot)) << test_case.name << " modified mask U";
  EXPECT_TRUE(mask_v.active_matches(mask_v_snapshot)) << test_case.name << " modified mask V";
  EXPECT_TRUE(base_y.memory_intact()) << test_case.name << " corrupted base Y padding or guards";
  EXPECT_TRUE(base_u.memory_intact()) << test_case.name << " corrupted base U padding or guards";
  EXPECT_TRUE(base_v.memory_intact()) << test_case.name << " corrupted base V padding or guards";
  EXPECT_TRUE(overlay.memory_intact()) << test_case.name << " corrupted overlay padding or guards";
  EXPECT_TRUE(mask_y.memory_intact()) << test_case.name << " corrupted mask Y padding or guards";
  EXPECT_TRUE(mask_u.memory_intact()) << test_case.name << " corrupted mask U padding or guards";
  EXPECT_TRUE(mask_v.memory_intact()) << test_case.name << " corrupted mask V padding or guards";
  EXPECT_TRUE(expected_y.memory_intact()) << test_case.name << " corrupted expected Y";
  EXPECT_TRUE(expected_u.memory_intact()) << test_case.name << " corrupted expected U";
  EXPECT_TRUE(expected_v.memory_intact()) << test_case.name << " corrupted expected V";
}

}  // namespace avsut::test
