#pragma once

#include "layer_test_helpers.h"

#include "support/comparators.h"
#include "support/guarded_video_buffer.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace avsut::test {

using LayerYuvMulFloatFunction = layer_yuv_mul_f_c_t*;

struct LayerYuvMulFloatCase {
  std::string format_name;
  bool is_chroma{};
  bool has_alpha{};
  MaskMode mask_mode{};
  int placement{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t destination_pitch{};
  std::size_t overlay_pitch{};
  std::size_t mask_width_pixels{};
  std::size_t mask_height_pixels{};
  std::size_t mask_pitch{};
  float opacity{};
  std::string opacity_name;
  Variant<LayerYuvMulFloatFunction> variant;
  std::string name;
};

inline VideoInfo layer_yuv_mul_float_video_info(int colorspace) {
  auto vi = layer_video_info(colorspace, 16);
  vi.pixel_type = colorspace == 420 ? VideoInfo::CS_YUV420PS : VideoInfo::CS_YUV444PS;
  return vi;
}

inline LayerYuvMulFloatFunction layer_yuv_mul_float_avx2_function(bool is_chroma, bool has_alpha,
                                                                  int colorspace, int placement) {
  auto vi = layer_yuv_mul_float_video_info(colorspace);
  layer_yuv_mul_c_t* integer_function = nullptr;
  LayerYuvMulFloatFunction function = nullptr;
  get_layer_yuv_mul_functions_avx2(is_chroma, has_alpha, placement, vi, 32, &integer_function,
                                   &function);
  return function;
}

inline std::string layer_yuv_mul_float_variant_name(
    const Variant<LayerYuvMulFloatFunction>& variant) {
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

inline LayerYuvMulFloatCase make_layer_yuv_mul_float_case(
    std::string format_name, bool is_chroma, bool has_alpha, int colorspace, int placement,
    std::size_t width_pixels, std::size_t height_pixels, std::size_t destination_pitch,
    std::size_t overlay_pitch, std::size_t mask_pitch, float opacity, std::string opacity_name) {
  if (width_pixels == 0 || height_pixels == 0 || destination_pitch < width_pixels * sizeof(float) ||
      overlay_pitch < width_pixels * sizeof(float) || opacity <= 0.0F || opacity >= 1.0F) {
    throw std::invalid_argument("invalid float Layer YUV multiply dimensions");
  }
  const auto mask_mode = layer_mask_mode(is_chroma, colorspace, placement);
  const auto mask_width = layer_mask_width(mask_mode, width_pixels);
  const auto mask_height = layer_mask_height(mask_mode, height_pixels);
  if (has_alpha && mask_pitch < mask_width * sizeof(float)) {
    throw std::invalid_argument("invalid float Layer YUV multiply mask pitch");
  }

  LayerYuvMulFloatCase result{
      std::move(format_name),
      is_chroma,
      has_alpha,
      mask_mode,
      placement,
      width_pixels,
      height_pixels,
      destination_pitch,
      overlay_pitch,
      mask_width,
      mask_height,
      mask_pitch,
      opacity,
      std::move(opacity_name),
      Variant<LayerYuvMulFloatFunction>{
          "avx2", layer_yuv_mul_float_avx2_function(is_chroma, has_alpha, colorspace, placement),
          IsaRequirement::Avx2},
      {}};
  std::ostringstream stream;
  stream << result.format_name << (result.is_chroma ? "_Chroma" : "_Luma") << "_"
         << layer_mask_mode_name(result.mask_mode) << "_" << layer_placement_name(result.placement)
         << "_Float_Width" << result.width_pixels << "_Height" << result.height_pixels
         << "_DstPitch" << result.destination_pitch << "_OverlayPitch" << result.overlay_pitch;
  if (result.has_alpha) {
    stream << "_MaskWidth" << result.mask_width_pixels << "_MaskHeight" << result.mask_height_pixels
           << "_MaskPitch" << result.mask_pitch;
  } else {
    stream << "_MaskNone";
  }
  stream << "_Opacity" << result.opacity_name << "_PatternFiniteAnchors_"
         << layer_yuv_mul_float_variant_name(result.variant);
  result.name = stream.str();
  return result;
}

inline void PrintTo(const LayerYuvMulFloatCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline float layer_yuv_mul_float_effective_mask(const LayerYuvMulFloatCase& test_case,
                                                PlaneView<const float> mask, std::size_t x,
                                                std::size_t y) {
  switch (test_case.mask_mode) {
    case MASK444:
      return mask.row(y)[x];
    case MASK420_MPEG2: {
      const auto* first = mask.row(y * 2);
      const auto* second = mask.row(y * 2 + 1);
      const auto left = x == 0 ? first[0] + second[0] : first[x * 2 - 1] + second[x * 2 - 1];
      const auto middle = first[x * 2] + second[x * 2];
      const auto right = first[x * 2 + 1] + second[x * 2 + 1];
      return (left + 2.0F * middle + right) * 0.125F;
    }
    default:
      throw std::invalid_argument("unsupported float Layer YUV multiply mask mode in reference");
  }
}

inline void fill_layer_yuv_mul_float_inputs(const LayerYuvMulFloatCase& test_case,
                                            GuardedVideoBuffer<float>& destination,
                                            GuardedVideoBuffer<float>& overlay,
                                            GuardedVideoBuffer<float>* mask) {
  constexpr std::array<float, 12> anchors{0.0F, 0.03125F, 0.125F, 0.2F, 0.25F,  0.375F,
                                          0.5F, 0.625F,   0.75F,  0.8F, 0.875F, 1.0F};
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto index = x + y * test_case.width_pixels;
      destination.view().row(y)[x] = anchors[(index + 2U) % anchors.size()];
      overlay.view().row(y)[x] = anchors[(index * 5U + 1U) % anchors.size()];
    }
  }
  if (mask != nullptr) {
    for (std::size_t y = 0; y < test_case.mask_height_pixels; ++y) {
      for (std::size_t x = 0; x < test_case.mask_width_pixels; ++x) {
        const auto index = x + y * test_case.mask_width_pixels;
        mask->view().row(y)[x] = anchors[(index * 7U + y + 3U) % anchors.size()];
      }
    }
  }
}

inline void apply_layer_yuv_mul_float_reference(const LayerYuvMulFloatCase& test_case,
                                                PlaneView<const float> overlay,
                                                PlaneView<const float> mask,
                                                PlaneView<float> destination) {
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto mask_value =
          test_case.has_alpha
              ? static_cast<double>(layer_yuv_mul_float_effective_mask(test_case, mask, x, y))
              : 1.0;
      const auto alpha = mask_value * static_cast<double>(test_case.opacity);
      const auto base = static_cast<double>(destination.row(y)[x]);
      const auto overlay_value = static_cast<double>(overlay.row(y)[x]);
      const auto target = test_case.is_chroma ? overlay_value : overlay_value * base;
      destination.row(y)[x] = static_cast<float>(base + (target - base) * alpha);
    }
  }
}

inline void run_layer_yuv_mul_float_case(const LayerYuvMulFloatCase& test_case) {
  GuardedVideoBuffer<float> destination(test_case.width_pixels, test_case.height_pixels,
                                        test_case.destination_pitch, 64);
  GuardedVideoBuffer<float> overlay(test_case.width_pixels, test_case.height_pixels,
                                    test_case.overlay_pitch, 64);
  GuardedVideoBuffer<float> expected(test_case.width_pixels, test_case.height_pixels,
                                     test_case.destination_pitch, 64);
  GuardedVideoBuffer<float> actual(test_case.width_pixels, test_case.height_pixels,
                                   test_case.destination_pitch, 64);
  std::unique_ptr<GuardedVideoBuffer<float>> mask;
  if (test_case.has_alpha) {
    mask = std::make_unique<GuardedVideoBuffer<float>>(
        test_case.mask_width_pixels, test_case.mask_height_pixels, test_case.mask_pitch, 64);
  }

  fill_layer_yuv_mul_float_inputs(test_case, destination, overlay, mask.get());
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    std::copy_n(destination.view().row(y), test_case.width_pixels, expected.view().row(y));
    std::copy_n(destination.view().row(y), test_case.width_pixels, actual.view().row(y));
  }
  apply_layer_yuv_mul_float_reference(
      test_case, overlay.view().as_const(),
      mask ? mask->view().as_const() : PlaneView<const float>(nullptr, 0, 0, 0), expected.view());

  const auto overlay_snapshot = overlay.snapshot_active();
  const auto mask_snapshot = mask ? mask->snapshot_active() : std::vector<std::uint8_t>{};
  ASSERT_NE(test_case.variant.function, nullptr)
      << test_case.name << " AVX2 getter returned no float Layer YUV multiply function";
  test_case.variant.function(
      reinterpret_cast<BYTE*>(actual.view().data()),
      reinterpret_cast<const BYTE*>(overlay.view().data()),
      mask ? reinterpret_cast<const BYTE*>(mask->view().data()) : nullptr,
      static_cast<int>(test_case.destination_pitch), static_cast<int>(test_case.overlay_pitch),
      static_cast<int>(test_case.mask_pitch), static_cast<int>(test_case.width_pixels),
      static_cast<int>(test_case.height_pixels), test_case.opacity);

  EXPECT_TRUE(compare_float(expected.view().as_const(), actual.view().as_const(),
                            FloatTolerance{0.00001F, 0.00002F}))
      << test_case.name << " reference mismatch for AVX2 variant";
  EXPECT_TRUE(destination.memory_intact())
      << test_case.name << " destination reference input was corrupted";
  EXPECT_TRUE(expected.memory_intact()) << test_case.name << " reference storage was corrupted";
  EXPECT_TRUE(actual.memory_intact()) << test_case.name << " output storage was corrupted";
  EXPECT_TRUE(overlay.active_matches(overlay_snapshot))
      << test_case.name << " modified the overlay input";
  EXPECT_TRUE(overlay.memory_intact()) << test_case.name << " overlay storage was corrupted";
  if (mask) {
    EXPECT_TRUE(mask->active_matches(mask_snapshot))
        << test_case.name << " modified the alpha mask";
    EXPECT_TRUE(mask->memory_intact()) << test_case.name << " alpha mask storage was corrupted";
  }
}

}  // namespace avsut::test
