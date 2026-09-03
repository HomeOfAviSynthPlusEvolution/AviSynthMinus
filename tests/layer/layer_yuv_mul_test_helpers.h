#pragma once

#include "layer_test_helpers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace avsut::test {

using LayerYuvMulFunction = layer_yuv_mul_c_t*;

struct LayerYuvMulCase {
  std::string format_name;
  bool is_chroma{};
  MaskMode mask_mode{};
  int placement{};
  int bits_per_pixel{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t destination_pitch{};
  std::size_t overlay_pitch{};
  std::size_t mask_width_pixels{};
  std::size_t mask_height_pixels{};
  std::size_t mask_pitch{};
  int opacity{};
  std::string opacity_name;
  Variant<LayerYuvMulFunction> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline std::string layer_yuv_mul_case_name(const LayerYuvMulCase& test_case) {
  std::ostringstream stream;
  stream << test_case.format_name << (test_case.is_chroma ? "_Chroma" : "_Luma") << "_"
         << layer_mask_mode_name(test_case.mask_mode) << "_"
         << layer_placement_name(test_case.placement) << "_Bpp" << test_case.bits_per_pixel
         << "_Width" << test_case.width_pixels << "_Height" << test_case.height_pixels
         << "_DstPitch" << test_case.destination_pitch << "_OverlayPitch"
         << test_case.overlay_pitch << "_MaskWidth" << test_case.mask_width_pixels
         << "_MaskHeight" << test_case.mask_height_pixels << "_MaskPitch"
         << test_case.mask_pitch << "_Opacity" << test_case.opacity_name;
  stream << "_Seed" << std::uppercase << std::hex << test_case.seed
         << "_PatternFixedRandom_" << layer_variant_name(test_case.variant);
  return stream.str();
}

inline LayerYuvMulFunction layer_yuv_mul_avx2_function(bool is_chroma, int colorspace,
                                                        int placement, int bits_per_pixel) {
  auto vi = layer_video_info(colorspace, bits_per_pixel);
  layer_yuv_mul_c_t* function = nullptr;
  layer_yuv_mul_f_c_t* float_function = nullptr;
  get_layer_yuv_mul_functions_avx2(is_chroma, true, placement, vi, bits_per_pixel, &function,
                                    &float_function);
  return function;
}

inline LayerYuvMulCase make_layer_yuv_mul_case(
    std::string format_name, bool is_chroma, int colorspace, int placement, int bits_per_pixel,
    std::size_t width_pixels, std::size_t height_pixels, std::size_t destination_pitch,
    std::size_t overlay_pitch, std::size_t mask_pitch, int opacity, std::string opacity_name,
    Variant<LayerYuvMulFunction> variant, std::string expected_hash, std::uint32_t seed) {
  if (bits_per_pixel != 16 || width_pixels == 0 || height_pixels == 0 ||
      destination_pitch < width_pixels * sizeof(std::uint16_t) ||
      overlay_pitch < width_pixels * sizeof(std::uint16_t)) {
    throw std::invalid_argument("invalid Layer YUV multiply dimensions");
  }
  const auto mask_mode = layer_mask_mode(is_chroma, colorspace, placement);
  const auto mask_width = layer_mask_width(mask_mode, width_pixels);
  const auto mask_height = layer_mask_height(mask_mode, height_pixels);
  if (mask_pitch < mask_width * sizeof(std::uint16_t) || opacity <= 0 || opacity >= 65535) {
    throw std::invalid_argument("invalid Layer YUV multiply mask or opacity");
  }

  LayerYuvMulCase result{std::move(format_name),
                         is_chroma,
                         mask_mode,
                         placement,
                         bits_per_pixel,
                         width_pixels,
                         height_pixels,
                         destination_pitch,
                         overlay_pitch,
                         mask_width,
                         mask_height,
                         mask_pitch,
                         opacity,
                         std::move(opacity_name),
                         std::move(variant),
                         std::move(expected_hash),
                         seed,
                         {}};
  result.name = layer_yuv_mul_case_name(result);
  return result;
}

inline void PrintTo(const LayerYuvMulCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
inline std::uint32_t layer_yuv_mul_effective_mask(const LayerYuvMulCase& test_case,
                                                  PlaneView<const T> mask, std::size_t x,
                                                  std::size_t y) {
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  std::uint32_t spatial_mask = 0;
  switch (test_case.mask_mode) {
    case MASK444:
      spatial_mask = mask.row(y)[x];
      break;
    case MASK420_MPEG2: {
      const auto* first = mask.row(y * 2);
      const auto* second = mask.row(y * 2 + 1);
      const auto left = x == 0 ? first[0] + second[0]
                               : first[x * 2 - 1] + second[x * 2 - 1];
      const auto middle = first[x * 2] + second[x * 2];
      const auto right = first[x * 2 + 1] + second[x * 2 + 1];
      spatial_mask = (left + 2U * middle + right + 4U) >> 3;
      break;
    }
    default:
      throw std::invalid_argument("unsupported Layer YUV multiply mask mode in reference");
  }
  return static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(spatial_mask) * static_cast<std::uint32_t>(test_case.opacity) +
       max_value / 2U) /
      max_value);
}

template <typename T>
void apply_layer_yuv_mul_reference(const LayerYuvMulCase& test_case, PlaneView<T> destination,
                                   PlaneView<const T> overlay, PlaneView<const T> mask) {
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto alpha = layer_yuv_mul_effective_mask(test_case, mask, x, y);
      const auto base = static_cast<std::uint32_t>(destination.row(y)[x]);
      const auto overlay_value = static_cast<std::uint32_t>(overlay.row(y)[x]);
      const auto target = test_case.is_chroma
                              ? overlay_value
                              : (overlay_value * static_cast<std::uint64_t>(base)) /
                                 max_value;
      const auto result = (static_cast<std::uint64_t>(base) * (max_value - alpha) +
                           static_cast<std::uint64_t>(target) * alpha + max_value / 2U) /
                          max_value;
      destination.row(y)[x] = static_cast<T>(result);
    }
  }
}

template <typename T>
void copy_layer_yuv_mul_active(PlaneView<const T> source, PlaneView<T> destination) {
  for (std::size_t y = 0; y < source.height(); ++y) {
    std::copy_n(source.row(y), source.width(), destination.row(y));
  }
}

template <typename T>
void run_layer_yuv_mul_case(const LayerYuvMulCase& test_case) {
  GuardedVideoBuffer<T> destination(test_case.width_pixels, test_case.height_pixels,
                                    test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> overlay(test_case.width_pixels, test_case.height_pixels,
                                test_case.overlay_pitch, 32);
  GuardedVideoBuffer<T> mask(test_case.mask_width_pixels, test_case.mask_height_pixels,
                             test_case.mask_pitch, 32);
  GuardedVideoBuffer<T> expected(test_case.width_pixels, test_case.height_pixels,
                                 test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> actual(test_case.width_pixels, test_case.height_pixels,
                               test_case.destination_pitch, 32);

  fill_random(destination.view(), test_case.seed);
  fill_random(overlay.view(), test_case.seed ^ 0xA5A5A5A5U);
  fill_random(mask.view(), test_case.seed ^ 0x5A5A5A5AU);
  copy_layer_yuv_mul_active(destination.view().as_const(), expected.view());
  copy_layer_yuv_mul_active(destination.view().as_const(), actual.view());

  apply_layer_yuv_mul_reference(test_case, expected.view(), overlay.view().as_const(),
                                mask.view().as_const());
  const auto overlay_snapshot = overlay.snapshot_active();
  const auto mask_snapshot = mask.snapshot_active();

  ASSERT_NE(test_case.variant.function, nullptr)
      << test_case.name << " AVX2 Layer YUV multiply getter returned no integer function";
  test_case.variant.function(
      reinterpret_cast<BYTE*>(actual.view().data()),
      reinterpret_cast<const BYTE*>(overlay.view().data()),
      reinterpret_cast<const BYTE*>(mask.view().data()),
      static_cast<int>(actual.view().pitch_bytes()), static_cast<int>(overlay.view().pitch_bytes()),
      static_cast<int>(mask.view().pitch_bytes()), static_cast<int>(test_case.width_pixels),
      static_cast<int>(test_case.height_pixels), test_case.opacity, test_case.bits_per_pixel);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for AVX2 variant";
  const auto actual_hash = format_hash(hash_active(actual.view().as_const()));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(overlay.active_matches(overlay_snapshot))
      << test_case.name << " modified the overlay input";
  EXPECT_TRUE(mask.active_matches(mask_snapshot)) << test_case.name << " modified the alpha mask";
  EXPECT_TRUE(destination.memory_intact()) << test_case.name << " destination was corrupted";
  EXPECT_TRUE(overlay.memory_intact()) << test_case.name << " overlay was corrupted";
  EXPECT_TRUE(mask.memory_intact()) << test_case.name << " alpha mask was corrupted";
  EXPECT_TRUE(expected.memory_intact()) << test_case.name << " reference storage was corrupted";
  EXPECT_TRUE(actual.memory_intact()) << test_case.name << " output storage was corrupted";
}

}  // namespace avsut::test
