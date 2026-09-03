#pragma once

#include "core/internal.h"

#ifndef AVS_UNUSED
#define AVS_UNUSED(value) (void)(value)
#define AVSUT_LAYER_DEFINED_AVS_UNUSED
#endif
#include "filters/intel/layer_avx2.h"
#include "filters/intel/layer_sse41.h"
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
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

#ifdef AVSUT_LAYER_DEFINED_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_LAYER_DEFINED_AVS_UNUSED
#endif

namespace avsut::test {

using LayerYuvAddFuncPtr = layer_yuv_add_c_t*;

struct LayerYuvAddCase {
  MaskMode mask_mode{};
  int placement{};
  bool is_chroma{};
  int bits_per_pixel{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t destination_pitch{};
  std::size_t mask_width_pixels{};
  std::size_t mask_height_pixels{};
  std::size_t mask_pitch{};
  int opacity{};
  std::string opacity_name;
  LayerYuvAddFuncPtr scalar_function{};
  Variant<LayerYuvAddFuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline const char* layer_mask_mode_name(MaskMode mode) {
  switch (mode) {
    case MASK444:
      return "Mask444";
    case MASK420:
      return "Mask420";
    case MASK420_MPEG2:
      return "Mask420Mpeg2";
    case MASK420_TOPLEFT:
      return "Mask420TopLeft";
    case MASK422:
      return "Mask422";
    case MASK422_MPEG2:
      return "Mask422Mpeg2";
    case MASK422_TOPLEFT:
      return "Mask422TopLeft";
    default:
      return "MaskOther";
  }
}

inline const char* layer_placement_name(int placement) {
  switch (placement) {
    case PLACEMENT_MPEG1:
      return "Mpeg1";
    case PLACEMENT_MPEG2:
      return "Mpeg2";
    case PLACEMENT_TOPLEFT:
      return "TopLeft";
    default:
      return "Unknown";
  }
}

template <typename Function>
inline std::string layer_variant_name(const Variant<Function>& variant) {
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

inline std::string layer_yuv_add_case_name(const LayerYuvAddCase& test_case) {
  std::ostringstream stream;
  stream << layer_mask_mode_name(test_case.mask_mode) << "_"
         << layer_placement_name(test_case.placement) << "_Bpp" << test_case.bits_per_pixel
         << "_Width" << test_case.width_pixels << "_Height" << test_case.height_pixels
         << "_DstPitch" << test_case.destination_pitch << "_MaskPitch" << test_case.mask_pitch
         << "_Opacity" << test_case.opacity_name;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryValues_" : "_PatternFixedRandom_")
         << layer_variant_name(test_case.variant);
  return stream.str();
}

inline std::size_t layer_round_up(std::size_t value, std::size_t alignment) {
  return ((value + alignment - 1) / alignment) * alignment;
}

inline MaskMode layer_mask_mode(bool is_chroma, int colorspace, int placement) {
  if (!is_chroma) {
    return MASK444;
  }
  if (colorspace == 420) {
    return placement == PLACEMENT_MPEG1     ? MASK420
           : placement == PLACEMENT_TOPLEFT ? MASK420_TOPLEFT
                                            : MASK420_MPEG2;
  }
  return placement == PLACEMENT_MPEG1     ? MASK422
         : placement == PLACEMENT_TOPLEFT ? MASK422_TOPLEFT
                                          : MASK422_MPEG2;
}

inline std::size_t layer_mask_width(MaskMode mode, std::size_t width) {
  return mode == MASK444 ? width : width * 2;
}

inline std::size_t layer_mask_height(MaskMode mode, std::size_t height) {
  return mode == MASK420 || mode == MASK420_MPEG2 || mode == MASK420_TOPLEFT ? height * 2 : height;
}

inline VideoInfo layer_video_info(int colorspace, int bits_per_pixel) {
  VideoInfo vi{};
  vi.width = 64;
  vi.height = 32;
  vi.num_frames = 1;
  vi.fps_numerator = 25;
  vi.fps_denominator = 1;
  if (colorspace == 420) {
    vi.pixel_type = bits_per_pixel == 8 ? VideoInfo::CS_YV12 : VideoInfo::CS_YUV420P16;
  } else {
    vi.pixel_type = bits_per_pixel == 8 ? VideoInfo::CS_YV16 : VideoInfo::CS_YUV422P16;
  }
  return vi;
}

inline LayerYuvAddFuncPtr layer_sse41_function(bool is_chroma, int colorspace, int placement,
                                               int bits_per_pixel) {
  auto vi = layer_video_info(colorspace, bits_per_pixel);
  layer_yuv_add_c_t* function = nullptr;
  layer_yuv_add_f_c_t* float_function = nullptr;
  get_layer_yuv_masked_add_functions_sse41(is_chroma, placement, vi, bits_per_pixel, &function,
                                           &float_function);
  return function;
}

inline LayerYuvAddFuncPtr layer_avx2_function(bool is_chroma, int colorspace, int placement,
                                              int bits_per_pixel) {
  auto vi = layer_video_info(colorspace, bits_per_pixel);
  masked_merge_fn_t* function = nullptr;
  masked_merge_float_fn_t* float_function = nullptr;
  get_layer_yuv_masked_add_functions_avx2(is_chroma, placement, vi, bits_per_pixel, &function,
                                          &float_function);
  return function;
}

inline LayerYuvAddCase make_layer_yuv_add_case(bool is_chroma, int colorspace, int placement,
                                               int bits_per_pixel, std::size_t width_pixels,
                                               std::size_t height_pixels, int opacity,
                                               std::string opacity_name,
                                               Variant<LayerYuvAddFuncPtr> variant,
                                               std::string expected_hash,
                                               std::uint32_t seed = 0) {
  const auto bytes_per_pixel = bits_per_pixel == 8 ? std::size_t{1} : std::size_t{2};
  const auto mode = layer_mask_mode(is_chroma, colorspace, placement);
  const auto mask_width = layer_mask_width(mode, width_pixels);
  const auto mask_height = layer_mask_height(mode, height_pixels);
  LayerYuvAddCase result{mode,
                         placement,
                         is_chroma,
                         bits_per_pixel,
                         width_pixels,
                         height_pixels,
                         layer_round_up(width_pixels * bytes_per_pixel, 32),
                         mask_width,
                         mask_height,
                         layer_round_up(mask_width * bytes_per_pixel, 32),
                         opacity,
                         std::move(opacity_name),
                         get_overlay_blend_masked_fn_c(is_chroma, mode),
                         std::move(variant),
                         std::move(expected_hash),
                         seed,
                         {}};
  result.name = layer_yuv_add_case_name(result);
  return result;
}

inline void PrintTo(const LayerYuvAddCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_layer_plane(PlaneView<T> view, std::uint32_t max_value, std::size_t offset) {
  constexpr std::array<std::uint32_t, 10> anchors{0, 1, 64, 127, 128, 192, 254, 255, 511, 1023};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      const auto index = y * view.width() + x + offset;
      const auto anchor = anchors[index % anchors.size()];
      const auto normalized = std::min(anchor, 255U);
      view.row(y)[x] =
          static_cast<T>(max_value == 255 ? normalized : (normalized * max_value) / 255U);
    }
  }
}

template <typename T>
inline std::uint32_t layer_effective_mask(const LayerYuvAddCase& test_case, PlaneView<const T> mask,
                                          std::size_t x, std::size_t y) {
  const auto* row = mask.row(y);
  std::uint32_t raw = 0;
  switch (test_case.mask_mode) {
    case MASK444:
      raw = row[x];
      break;
    case MASK420: {
      const auto* first = mask.row(y * 2);
      const auto* second = mask.row(y * 2 + 1);
      raw = (first[x * 2] + first[x * 2 + 1] + second[x * 2] + second[x * 2 + 1] + 2U) >> 2;
      break;
    }
    case MASK420_MPEG2: {
      const auto* row1 = mask.row(y * 2 + 1);
      const auto* first = mask.row(y * 2);
      const auto* second = row1;
      const auto left = x == 0 ? first[0] + second[0] : first[x * 2 - 1] + second[x * 2 - 1];
      const auto mid = first[x * 2] + second[x * 2];
      const auto right = first[x * 2 + 1] + second[x * 2 + 1];
      raw = (left + 2U * mid + right + 4U) >> 3;
      break;
    }
    case MASK420_TOPLEFT:
      raw = mask.row(y * 2)[x * 2];
      break;
    case MASK422:
      raw = (row[x * 2] + row[x * 2 + 1] + 1U) >> 1;
      break;
    case MASK422_MPEG2: {
      const auto left = x == 0 ? row[0] : row[x * 2 - 1];
      const auto mid = row[x * 2];
      const auto right = row[x * 2 + 1];
      raw = (left + 2U * mid + right + 2U) >> 2;
      break;
    }
    case MASK422_TOPLEFT:
      raw = row[x * 2];
      break;
    default:
      break;
  }
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  if (test_case.opacity == static_cast<int>(max_value)) {
    return raw;
  }
  return (raw * static_cast<std::uint32_t>(test_case.opacity) + max_value / 2U) / max_value;
}

template <typename T>
void apply_layer_reference(const LayerYuvAddCase& test_case, PlaneView<T> destination,
                           PlaneView<const T> overlay, PlaneView<const T> mask) {
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto effective = layer_effective_mask(test_case, mask, x, y);
      const auto first = static_cast<std::uint32_t>(destination.row(y)[x]);
      const auto second = static_cast<std::uint32_t>(overlay.row(y)[x]);
      destination.row(y)[x] = static_cast<T>(
          (first * (max_value - effective) + second * effective + max_value / 2U) / max_value);
    }
  }
}

template <typename T>
void run_layer_yuv_add_case(const LayerYuvAddCase& test_case) {
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  GuardedVideoBuffer<T> destination(test_case.width_pixels, test_case.height_pixels,
                                    test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> overlay(test_case.width_pixels, test_case.height_pixels,
                                test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> mask(test_case.mask_width_pixels, test_case.mask_height_pixels,
                             test_case.mask_pitch, 32);
  GuardedVideoBuffer<T> expected(test_case.width_pixels, test_case.height_pixels,
                                 test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> actual(test_case.width_pixels, test_case.height_pixels,
                               test_case.destination_pitch, 32);

  if (test_case.seed == 0) {
    fill_layer_plane(destination.view(), max_value, 0);
    fill_layer_plane(overlay.view(), max_value, 3);
    fill_layer_plane(mask.view(), max_value, 6);
  } else {
    fill_random(destination.view(), test_case.seed);
    fill_random(overlay.view(), test_case.seed ^ 0xA5A5A5A5U);
    fill_random(mask.view(), test_case.seed ^ 0x5A5A5A5AU);
  }
  const auto overlay_snapshot = overlay.snapshot_active();
  const auto mask_snapshot = mask.snapshot_active();
  for (std::size_t y = 0; y < destination.view().height(); ++y) {
    std::copy_n(destination.view().row(y), destination.view().width(), expected.view().row(y));
    std::copy_n(destination.view().row(y), destination.view().width(), actual.view().row(y));
  }
  apply_layer_reference(test_case, expected.view(), overlay.view().as_const(),
                        mask.view().as_const());

  test_case.scalar_function(
      reinterpret_cast<BYTE*>(actual.view().data()),
      reinterpret_cast<const BYTE*>(overlay.view().data()),
      reinterpret_cast<const BYTE*>(mask.view().data()),
      static_cast<int>(actual.view().pitch_bytes()), static_cast<int>(overlay.view().pitch_bytes()),
      static_cast<int>(mask.view().pitch_bytes()), static_cast<int>(test_case.width_pixels),
      static_cast<int>(test_case.height_pixels), test_case.opacity, test_case.bits_per_pixel);
  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for scalar baseline";

  for (std::size_t y = 0; y < destination.view().height(); ++y) {
    std::copy_n(destination.view().row(y), destination.view().width(), actual.view().row(y));
  }
  test_case.variant.function(
      reinterpret_cast<BYTE*>(actual.view().data()),
      reinterpret_cast<const BYTE*>(overlay.view().data()),
      reinterpret_cast<const BYTE*>(mask.view().data()),
      static_cast<int>(actual.view().pitch_bytes()), static_cast<int>(overlay.view().pitch_bytes()),
      static_cast<int>(mask.view().pitch_bytes()), static_cast<int>(test_case.width_pixels),
      static_cast<int>(test_case.height_pixels), test_case.opacity, test_case.bits_per_pixel);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch";
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
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
}

}  // namespace avsut::test
