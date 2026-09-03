#pragma once

#include "filters/intel/layer_avx2.h"

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
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace avsut::test {

using LayerPlanarRgbLightenDarkenFunction = layer_planarrgb_lighten_darken_c_t*;

enum class LayerPlanarRgbLightenDarkenMode { Lighten, Darken };

struct LayerPlanarRgbLightenDarkenCase {
  LayerPlanarRgbLightenDarkenMode mode{};
  bool has_alpha{};
  bool blend_alpha{};
  int bits_per_pixel{};
  std::size_t width_samples{};
  std::size_t height{};
  std::size_t destination_pitch{};
  std::size_t overlay_pitch{};
  std::size_t mask_pitch{};
  int opacity{};
  std::string opacity_name;
  int threshold{};
  std::string threshold_name;
  Variant<LayerPlanarRgbLightenDarkenFunction> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline std::string layer_planarrgb_lighten_darken_variant_name(
    const Variant<LayerPlanarRgbLightenDarkenFunction>& variant) {
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

inline const char* layer_planarrgb_lighten_darken_mode_name(
    LayerPlanarRgbLightenDarkenMode mode) {
  return mode == LayerPlanarRgbLightenDarkenMode::Lighten ? "Lighten" : "Darken";
}

inline LayerPlanarRgbLightenDarkenFunction layer_planarrgb_lighten_darken_function(
    LayerPlanarRgbLightenDarkenMode mode, bool has_alpha, bool blend_alpha, int bits_per_pixel) {
  LayerPlanarRgbLightenDarkenFunction function = nullptr;
  layer_planarrgb_lighten_darken_f_c_t* float_function = nullptr;
  get_layer_planarrgb_lighten_darken_functions_avx2(
      mode == LayerPlanarRgbLightenDarkenMode::Lighten, has_alpha, blend_alpha, bits_per_pixel,
      &function, &float_function);
  return function;
}

inline std::string layer_planarrgb_lighten_darken_case_name(
    const LayerPlanarRgbLightenDarkenCase& test_case) {
  std::ostringstream stream;
  stream << "Base" << (test_case.blend_alpha ? "Rgba" : "Rgb") << "_Overlay"
         << (test_case.has_alpha ? "Rgba" : "Rgb") << "_"
         << layer_planarrgb_lighten_darken_mode_name(test_case.mode) << "_Bpp"
         << test_case.bits_per_pixel << "_Width" << test_case.width_samples << "_Height"
         << test_case.height << "_DstPitch" << test_case.destination_pitch << "_OverlayPitch"
         << test_case.overlay_pitch;
  if (test_case.has_alpha) {
    stream << "_MaskPitch" << test_case.mask_pitch;
  } else {
    stream << "_MaskNone";
  }
  stream << "_Opacity" << test_case.opacity_name << "_Threshold" << test_case.threshold_name
         << "_Seed" << std::uppercase << std::hex << test_case.seed
         << "_PatternFixedRandom_"
         << layer_planarrgb_lighten_darken_variant_name(test_case.variant);
  return stream.str();
}

inline LayerPlanarRgbLightenDarkenCase make_layer_planarrgb_lighten_darken_case(
    LayerPlanarRgbLightenDarkenMode mode, bool has_alpha, bool blend_alpha, int bits_per_pixel,
    std::size_t width_samples, std::size_t height, std::size_t destination_pitch,
    std::size_t overlay_pitch, std::size_t mask_pitch, int opacity, std::string opacity_name,
    int threshold, std::string threshold_name, std::uint32_t seed,
    std::string expected_hash = {}) {
  const auto bytes_per_sample = bits_per_pixel == 8 ? std::size_t{1} : std::size_t{2};
  const auto max_value = (1 << bits_per_pixel) - 1;
  if ((bits_per_pixel != 8 && bits_per_pixel != 16) || width_samples == 0 || height == 0 ||
      destination_pitch < width_samples * bytes_per_sample ||
      overlay_pitch < width_samples * bytes_per_sample || opacity <= 0 || opacity >= max_value ||
      threshold < 0 || threshold > max_value || (blend_alpha && !has_alpha) ||
      (has_alpha && mask_pitch < width_samples * bytes_per_sample)) {
    throw std::invalid_argument("invalid Layer planar RGB lighten/darken dimensions");
  }

  LayerPlanarRgbLightenDarkenCase result{
      mode,
      has_alpha,
      blend_alpha,
      bits_per_pixel,
      width_samples,
      height,
      destination_pitch,
      overlay_pitch,
      mask_pitch,
      opacity,
      std::move(opacity_name),
      threshold,
      std::move(threshold_name),
      Variant<LayerPlanarRgbLightenDarkenFunction>{
          "avx2",
          layer_planarrgb_lighten_darken_function(mode, has_alpha, blend_alpha, bits_per_pixel),
          IsaRequirement::Avx2},
      std::move(expected_hash),
      seed,
      {}};
  result.name = layer_planarrgb_lighten_darken_case_name(result);
  return result;
}

inline void PrintTo(const LayerPlanarRgbLightenDarkenCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_layer_planarrgb_lighten_darken_inputs(
    const LayerPlanarRgbLightenDarkenCase& test_case,
    std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& destination,
    std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& overlay,
    std::unique_ptr<GuardedVideoBuffer<T>>& mask) {
  const auto destination_planes = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};
  const auto overlay_planes = test_case.has_alpha ? std::size_t{4} : std::size_t{3};
  for (std::size_t plane = 0; plane < destination_planes; ++plane) {
    fill_random(destination[plane]->view(),
                test_case.seed ^ (static_cast<std::uint32_t>(plane + 1) * 0x10001U));
  }
  for (std::size_t plane = 0; plane < overlay_planes; ++plane) {
    fill_random(overlay[plane]->view(),
                test_case.seed ^ 0xA5A5A5A5U ^
                    (static_cast<std::uint32_t>(plane + 1) * 0x10001U));
  }
  if (test_case.has_alpha) {
    mask = std::make_unique<GuardedVideoBuffer<T>>(test_case.width_samples, test_case.height,
                                                   test_case.mask_pitch, 64);
    fill_random(mask->view(), test_case.seed ^ 0x5A5A5A5AU);
  }
}

template <typename T>
void copy_layer_planarrgb_lighten_darken_planes(
    const LayerPlanarRgbLightenDarkenCase& test_case,
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

template <typename T>
void apply_layer_planarrgb_lighten_darken_reference(
    const LayerPlanarRgbLightenDarkenCase& test_case,
    const std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& overlay,
    const std::unique_ptr<GuardedVideoBuffer<T>>& mask,
    std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& destination) {
  constexpr int kCyb = 3736;
  constexpr int kCyg = 19234;
  constexpr int kCyr = 9798;
  const auto max_value = (std::uint64_t{1} << test_case.bits_per_pixel) - 1U;
  const auto half = max_value / 2U;
  const auto plane_count = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};

  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width_samples; ++x) {
      const auto alpha = test_case.has_alpha
                             ? (static_cast<std::uint64_t>(mask->view().row(y)[x]) *
                                    static_cast<std::uint64_t>(test_case.opacity) +
                                half) /
                                   max_value
                             : static_cast<std::uint64_t>(test_case.opacity);
      const auto destination_luma =
          (kCyb * static_cast<int>(destination[1]->view().row(y)[x]) +
           kCyg * static_cast<int>(destination[0]->view().row(y)[x]) +
           kCyr * static_cast<int>(destination[2]->view().row(y)[x])) >>
          15;
      const auto overlay_luma =
          (kCyb * static_cast<int>(overlay[1]->view().row(y)[x]) +
           kCyg * static_cast<int>(overlay[0]->view().row(y)[x]) +
           kCyr * static_cast<int>(overlay[2]->view().row(y)[x])) >>
          15;
      const bool selected = test_case.mode == LayerPlanarRgbLightenDarkenMode::Lighten
                                ? overlay_luma > destination_luma + test_case.threshold
                                : overlay_luma < destination_luma - test_case.threshold;
      const auto effective_alpha = selected ? alpha : std::uint64_t{0};
      const auto inverse_alpha = max_value - effective_alpha;
      for (std::size_t plane = 0; plane < plane_count; ++plane) {
        const auto destination_value =
            static_cast<std::uint64_t>(destination[plane]->view().row(y)[x]);
        const auto target = static_cast<std::uint64_t>(overlay[plane]->view().row(y)[x]);
        destination[plane]->view().row(y)[x] = static_cast<T>(
            (destination_value * inverse_alpha + target * effective_alpha + half) / max_value);
      }
    }
  }
}

template <typename T>
std::uint64_t hash_layer_planarrgb_lighten_darken_active(
    const LayerPlanarRgbLightenDarkenCase& test_case,
    const std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& planes) {
  XXH3_state_t state;
  if (XXH3_64bits_reset(&state) == XXH_ERROR) {
    throw std::runtime_error("XXH3 reset failed");
  }
  const auto plane_count = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};
  for (std::size_t plane = 0; plane < plane_count; ++plane) {
    const auto view = planes[plane]->view().as_const();
    for (std::size_t y = 0; y < view.height(); ++y) {
      if (XXH3_64bits_update(&state, view.row(y), view.active_row_bytes()) == XXH_ERROR) {
        throw std::runtime_error("XXH3 update failed");
      }
    }
  }
  return XXH3_64bits_digest(&state);
}

template <typename T>
void run_layer_planarrgb_lighten_darken_case(
    const LayerPlanarRgbLightenDarkenCase& test_case) {
  const auto destination_planes = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};
  const auto overlay_planes = test_case.has_alpha ? std::size_t{4} : std::size_t{3};
  std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4> destination;
  std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4> overlay;
  std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4> expected;
  std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4> actual;
  for (std::size_t plane = 0; plane < destination_planes; ++plane) {
    destination[plane] = std::make_unique<GuardedVideoBuffer<T>>(
        test_case.width_samples, test_case.height, test_case.destination_pitch, 64);
    expected[plane] = std::make_unique<GuardedVideoBuffer<T>>(
        test_case.width_samples, test_case.height, test_case.destination_pitch, 64);
    actual[plane] = std::make_unique<GuardedVideoBuffer<T>>(
        test_case.width_samples, test_case.height, test_case.destination_pitch, 64);
  }
  for (std::size_t plane = 0; plane < overlay_planes; ++plane) {
    overlay[plane] = std::make_unique<GuardedVideoBuffer<T>>(
        test_case.width_samples, test_case.height, test_case.overlay_pitch, 64);
  }
  std::unique_ptr<GuardedVideoBuffer<T>> mask;

  fill_layer_planarrgb_lighten_darken_inputs(test_case, destination, overlay, mask);
  copy_layer_planarrgb_lighten_darken_planes(test_case, destination, expected);
  copy_layer_planarrgb_lighten_darken_planes(test_case, destination, actual);
  apply_layer_planarrgb_lighten_darken_reference(test_case, overlay, mask, expected);

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
      << test_case.name << " AVX2 getter returned no integer Planar RGB lighten/darken function";
  test_case.variant.function(
      actual_ptrs.data(), overlay_ptrs.data(),
      mask ? reinterpret_cast<const BYTE*>(mask->view().data()) : nullptr,
      static_cast<int>(test_case.destination_pitch), static_cast<int>(test_case.overlay_pitch),
      static_cast<int>(test_case.mask_pitch), static_cast<int>(test_case.width_samples),
      static_cast<int>(test_case.height), test_case.opacity, test_case.threshold,
      test_case.bits_per_pixel);

  for (std::size_t plane = 0; plane < destination_planes; ++plane) {
    EXPECT_TRUE(compare_exact(expected[plane]->view().as_const(), actual[plane]->view().as_const()))
        << test_case.name << " reference mismatch for plane " << plane << " variant "
        << test_case.variant.name;
  }
  const auto actual_hash =
      format_hash(hash_layer_planarrgb_lighten_darken_active(test_case, actual));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;

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
