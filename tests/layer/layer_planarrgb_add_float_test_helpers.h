#pragma once

#include "filters/intel/layer_avx2.h"

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

using LayerPlanarRgbAddFloatFunction = layer_planarrgb_add_f_c_t*;

struct LayerPlanarRgbAddFloatCase {
  bool chroma{};
  bool has_alpha{};
  bool blend_alpha{};
  std::size_t width_samples{};
  std::size_t height{};
  std::size_t destination_pitch{};
  std::size_t overlay_pitch{};
  std::size_t mask_pitch{};
  float opacity{};
  std::string opacity_name;
  Variant<LayerPlanarRgbAddFloatFunction> variant;
  std::string name;
};

inline LayerPlanarRgbAddFloatFunction layer_planarrgb_add_float_function(bool chroma,
                                                                        bool has_alpha,
                                                                        bool blend_alpha) {
  layer_planarrgb_add_c_t* integer_function = nullptr;
  LayerPlanarRgbAddFloatFunction function = nullptr;
  get_layer_planarrgb_add_functions_avx2(chroma, has_alpha, blend_alpha, 32, &integer_function,
                                         &function);
  return function;
}

inline std::string layer_planarrgb_add_float_variant_name(
    const Variant<LayerPlanarRgbAddFloatFunction>& variant) {
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

inline LayerPlanarRgbAddFloatCase make_layer_planarrgb_add_float_case(
    bool chroma, bool has_alpha, bool blend_alpha, std::size_t width_samples, std::size_t height,
    std::size_t destination_pitch, std::size_t overlay_pitch, std::size_t mask_pitch,
    float opacity, std::string opacity_name) {
  if (width_samples == 0 || height == 0 ||
      destination_pitch < width_samples * sizeof(float) ||
      overlay_pitch < width_samples * sizeof(float) || opacity <= 0.0F || opacity >= 1.0F ||
      (blend_alpha && !has_alpha) ||
      (has_alpha && mask_pitch < width_samples * sizeof(float))) {
    throw std::invalid_argument("invalid float Layer planar RGB add dimensions");
  }
  LayerPlanarRgbAddFloatCase result{
      chroma,
      has_alpha,
      blend_alpha,
      width_samples,
      height,
      destination_pitch,
      overlay_pitch,
      mask_pitch,
      opacity,
      std::move(opacity_name),
      Variant<LayerPlanarRgbAddFloatFunction>{
          "avx2", layer_planarrgb_add_float_function(chroma, has_alpha, blend_alpha),
          IsaRequirement::Avx2},
      {}};
  std::ostringstream stream;
  stream << "Base" << (blend_alpha ? "Rgba" : "Rgb") << "_Overlay"
         << (has_alpha ? "Rgba" : "Rgb") << (chroma ? "_Chroma" : "_Luma")
         << "_Float_Width" << width_samples << "_Height" << height << "_DstPitch"
         << destination_pitch << "_OverlayPitch" << overlay_pitch;
  if (has_alpha) {
    stream << "_MaskPitch" << mask_pitch;
  } else {
    stream << "_MaskNone";
  }
  stream << "_Opacity" << result.opacity_name << "_PatternFiniteAnchors_"
         << layer_planarrgb_add_float_variant_name(result.variant);
  result.name = stream.str();
  return result;
}

inline void PrintTo(const LayerPlanarRgbAddFloatCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_layer_planarrgb_add_float_inputs(
    const LayerPlanarRgbAddFloatCase& test_case,
    std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& destination,
    std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& overlay,
    std::unique_ptr<GuardedVideoBuffer<T>>& mask) {
  constexpr std::array<float, 12> anchors{
      0.0F, 0.03125F, 0.125F, 0.2F, 0.25F, 0.375F,
      0.5F,  0.625F,   0.75F,  0.8F,  0.875F, 1.0F};
  const auto destination_planes = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};
  const auto overlay_planes = test_case.has_alpha ? std::size_t{4} : std::size_t{3};
  for (std::size_t plane = 0; plane < destination_planes; ++plane) {
    for (std::size_t y = 0; y < test_case.height; ++y) {
      for (std::size_t x = 0; x < test_case.width_samples; ++x) {
        const auto index = x + y * test_case.width_samples;
        destination[plane]->view().row(y)[x] =
            static_cast<T>(anchors[(index + plane * 3U) % anchors.size()]);
      }
    }
  }
  for (std::size_t plane = 0; plane < overlay_planes; ++plane) {
    for (std::size_t y = 0; y < test_case.height; ++y) {
      for (std::size_t x = 0; x < test_case.width_samples; ++x) {
        const auto index = x + y * test_case.width_samples;
        overlay[plane]->view().row(y)[x] =
            static_cast<T>(anchors[(index * 5U + plane * 2U + 1U) % anchors.size()]);
      }
    }
  }
  if (test_case.has_alpha) {
    mask = std::make_unique<GuardedVideoBuffer<T>>(test_case.width_samples, test_case.height,
                                                   test_case.mask_pitch, 64);
    for (std::size_t y = 0; y < test_case.height; ++y) {
      for (std::size_t x = 0; x < test_case.width_samples; ++x) {
        const auto index = x + y * test_case.width_samples;
        mask->view().row(y)[x] = static_cast<T>(anchors[(index * 7U + y + 2U) % anchors.size()]);
      }
    }
  }
}

template <typename T>
void copy_layer_planarrgb_add_float_planes(
    const LayerPlanarRgbAddFloatCase& test_case,
    const std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& source,
    std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& destination) {
  const auto plane_count = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};
  for (std::size_t plane = 0; plane < plane_count; ++plane) {
    for (std::size_t y = 0; y < test_case.height; ++y) {
      std::copy_n(source[plane]->view().row(y), test_case.width_samples,
                  destination[plane]->view().row(y));
    }
  }
}

void apply_layer_planarrgb_add_float_reference(
    const LayerPlanarRgbAddFloatCase& test_case,
    const std::array<std::unique_ptr<GuardedVideoBuffer<float>>, 4>& overlay,
    const std::unique_ptr<GuardedVideoBuffer<float>>& mask,
    std::array<std::unique_ptr<GuardedVideoBuffer<float>>, 4>& destination) {
  constexpr double kCyb = 0.114;
  constexpr double kCyg = 0.587;
  constexpr double kCyr = 0.299;
  const auto plane_count = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width_samples; ++x) {
      const auto alpha = test_case.has_alpha
                             ? static_cast<double>(mask->view().row(y)[x]) * test_case.opacity
                             : static_cast<double>(test_case.opacity);
      const auto luma =
          kCyb * overlay[1]->view().row(y)[x] + kCyg * overlay[0]->view().row(y)[x] +
          kCyr * overlay[2]->view().row(y)[x];
      for (std::size_t plane = 0; plane < plane_count; ++plane) {
        const auto destination_value = static_cast<double>(destination[plane]->view().row(y)[x]);
        const auto target = test_case.chroma
                                ? static_cast<double>(overlay[plane]->view().row(y)[x])
                                : (plane < 3
                                       ? luma
                                       : static_cast<double>(overlay[3]->view().row(y)[x]));
        destination[plane]->view().row(y)[x] =
            static_cast<float>(destination_value + (target - destination_value) * alpha);
      }
    }
  }
}

void run_layer_planarrgb_add_float_case(const LayerPlanarRgbAddFloatCase& test_case) {
  const auto destination_planes = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};
  const auto overlay_planes = test_case.has_alpha ? std::size_t{4} : std::size_t{3};
  std::array<std::unique_ptr<GuardedVideoBuffer<float>>, 4> destination;
  std::array<std::unique_ptr<GuardedVideoBuffer<float>>, 4> overlay;
  std::array<std::unique_ptr<GuardedVideoBuffer<float>>, 4> expected;
  std::array<std::unique_ptr<GuardedVideoBuffer<float>>, 4> actual;
  for (std::size_t plane = 0; plane < destination_planes; ++plane) {
    destination[plane] = std::make_unique<GuardedVideoBuffer<float>>(
        test_case.width_samples, test_case.height, test_case.destination_pitch, 64);
    expected[plane] = std::make_unique<GuardedVideoBuffer<float>>(
        test_case.width_samples, test_case.height, test_case.destination_pitch, 64);
    actual[plane] = std::make_unique<GuardedVideoBuffer<float>>(
        test_case.width_samples, test_case.height, test_case.destination_pitch, 64);
  }
  for (std::size_t plane = 0; plane < overlay_planes; ++plane) {
    overlay[plane] = std::make_unique<GuardedVideoBuffer<float>>(
        test_case.width_samples, test_case.height, test_case.overlay_pitch, 64);
  }
  std::unique_ptr<GuardedVideoBuffer<float>> mask;

  fill_layer_planarrgb_add_float_inputs(test_case, destination, overlay, mask);
  copy_layer_planarrgb_add_float_planes(test_case, destination, expected);
  copy_layer_planarrgb_add_float_planes(test_case, destination, actual);
  apply_layer_planarrgb_add_float_reference(test_case, overlay, mask, expected);

  std::array<BYTE*, 4> actual_ptrs{};
  std::array<const BYTE*, 4> overlay_ptrs{};
  for (std::size_t plane = 0; plane < destination_planes; ++plane) {
    actual_ptrs[plane] = reinterpret_cast<BYTE*>(actual[plane]->view().data());
  }
  for (std::size_t plane = 0; plane < overlay_planes; ++plane) {
    overlay_ptrs[plane] = reinterpret_cast<const BYTE*>(overlay[plane]->view().data());
  }
  const auto overlay_snapshots = [&]() {
    std::array<std::vector<std::uint8_t>, 4> snapshots;
    for (std::size_t plane = 0; plane < overlay_planes; ++plane) {
      snapshots[plane] = overlay[plane]->snapshot_active();
    }
    return snapshots;
  }();
  const auto mask_snapshot = mask ? mask->snapshot_active() : std::vector<std::uint8_t>{};

  ASSERT_NE(test_case.variant.function, nullptr)
      << test_case.name << " AVX2 getter returned no float Planar RGB add function";
  test_case.variant.function(
      actual_ptrs.data(), overlay_ptrs.data(),
      mask ? reinterpret_cast<const BYTE*>(mask->view().data()) : nullptr,
      static_cast<int>(test_case.destination_pitch), static_cast<int>(test_case.overlay_pitch),
      static_cast<int>(test_case.mask_pitch), static_cast<int>(test_case.width_samples),
      static_cast<int>(test_case.height), test_case.opacity);

  for (std::size_t plane = 0; plane < destination_planes; ++plane) {
    EXPECT_TRUE(compare_float(expected[plane]->view().as_const(), actual[plane]->view().as_const(),
                              FloatTolerance{0.00001F, 0.00002F}))
        << test_case.name << " reference mismatch for plane " << plane << " variant "
        << test_case.variant.name;
  }
  for (std::size_t plane = 0; plane < destination_planes; ++plane) {
    EXPECT_TRUE(destination[plane]->memory_intact())
        << test_case.name << " destination input storage was corrupted for plane " << plane;
    EXPECT_TRUE(expected[plane]->memory_intact())
        << test_case.name << " reference storage was corrupted for plane " << plane;
    EXPECT_TRUE(actual[plane]->memory_intact())
        << test_case.name << " output storage was corrupted for plane " << plane;
  }
  for (std::size_t plane = 0; plane < overlay_planes; ++plane) {
    EXPECT_TRUE(overlay[plane]->active_matches(overlay_snapshots[plane]))
        << test_case.name << " modified overlay plane " << plane;
    EXPECT_TRUE(overlay[plane]->memory_intact())
        << test_case.name << " corrupted overlay plane " << plane;
  }
  if (mask) {
    EXPECT_TRUE(mask->active_matches(mask_snapshot)) << test_case.name << " modified alpha mask";
    EXPECT_TRUE(mask->memory_intact()) << test_case.name << " corrupted alpha mask";
  }
}

}  // namespace avsut::test
