#pragma once

#include "filters/intel/layer_avx2.h"
#include "filters/intel/layer_sse41.h"

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

using LayerPlanarRgbAddFuncPtr = layer_planarrgb_add_c_t*;

struct LayerPlanarRgbAddCase {
  bool blend_alpha{};
  int bits_per_pixel{};
  std::size_t width_samples{};
  std::size_t height{};
  std::size_t destination_pitch{};
  std::size_t overlay_pitch{};
  std::size_t mask_pitch{};
  int opacity{};
  std::string opacity_name;
  Variant<LayerPlanarRgbAddFuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline std::string layer_planarrgb_add_variant_name(
    const Variant<LayerPlanarRgbAddFuncPtr>& variant) {
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

inline std::string layer_planarrgb_add_case_name(const LayerPlanarRgbAddCase& test_case) {
  std::ostringstream stream;
  stream << (test_case.blend_alpha ? "PlanarRgba" : "PlanarRgb") << "_Bpp"
         << test_case.bits_per_pixel << "_Width" << test_case.width_samples << "_Height"
         << test_case.height << "_DstPitch" << test_case.destination_pitch << "_OverlayPitch"
         << test_case.overlay_pitch << "_MaskPitch" << test_case.mask_pitch << "_Opacity"
         << test_case.opacity_name;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryAnchors_" : "_PatternFixedRandom_")
         << layer_planarrgb_add_variant_name(test_case.variant);
  return stream.str();
}

inline LayerPlanarRgbAddFuncPtr layer_planarrgb_add_function(bool avx2, bool blend_alpha,
                                                             int bits_per_pixel) {
  LayerPlanarRgbAddFuncPtr function = nullptr;
  layer_planarrgb_add_f_c_t* float_function = nullptr;
  if (avx2) {
    get_layer_planarrgb_add_functions_avx2(true, true, blend_alpha, bits_per_pixel, &function,
                                           &float_function);
  } else {
    get_layer_planarrgb_add_functions_sse41(true, true, blend_alpha, bits_per_pixel, &function,
                                            &float_function);
  }
  return function;
}

inline LayerPlanarRgbAddCase make_layer_planarrgb_add_case(
    bool blend_alpha, int bits_per_pixel, std::size_t width_samples, std::size_t height,
    std::size_t destination_pitch, std::size_t overlay_pitch, std::size_t mask_pitch, int opacity,
    std::string opacity_name, std::string variant_name, IsaRequirement requirement, bool avx2,
    std::string expected_hash = {}, std::uint32_t seed = 0) {
  LayerPlanarRgbAddCase result{
      blend_alpha,
      bits_per_pixel,
      width_samples,
      height,
      destination_pitch,
      overlay_pitch,
      mask_pitch,
      opacity,
      std::move(opacity_name),
      Variant<LayerPlanarRgbAddFuncPtr>{
          std::move(variant_name), layer_planarrgb_add_function(avx2, blend_alpha, bits_per_pixel),
          requirement},
      std::move(expected_hash),
      seed,
      {}};
  result.name = layer_planarrgb_add_case_name(result);
  return result;
}

inline void PrintTo(const LayerPlanarRgbAddCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline std::uint32_t layer_planarrgb_add_scale_anchor(std::uint32_t anchor,
                                                      std::uint32_t max_value) {
  return (anchor * max_value + 127U) / 255U;
}

template <typename T>
void fill_layer_planarrgb_add_inputs(
    const LayerPlanarRgbAddCase& test_case,
    std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& destination,
    std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& overlay, GuardedVideoBuffer<T>& mask) {
  const auto plane_count = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  if (test_case.seed != 0) {
    for (std::size_t plane = 0; plane < plane_count; ++plane) {
      const auto plane_seed =
          test_case.seed ^ (static_cast<std::uint32_t>(plane + 1) * 0x10001U);
      fill_random(destination[plane]->view(), plane_seed);
      fill_random(overlay[plane]->view(),
                  test_case.seed ^ 0xA5A5A5A5U ^
                      (static_cast<std::uint32_t>(plane + 1) * 0x10001U));
    }
    fill_random(mask.view(), test_case.seed ^ 0x5A5A5A5AU);
    return;
  }
  constexpr std::array<std::uint32_t, 10> anchors{0U,   1U,   17U,  63U,  127U,
                                                  128U, 191U, 254U, 255U, 42U};
  for (std::size_t plane = 0; plane < plane_count; ++plane) {
    for (std::size_t y = 0; y < test_case.height; ++y) {
      for (std::size_t x = 0; x < test_case.width_samples; ++x) {
        const auto index = y * test_case.width_samples + x;
        destination[plane]->view().row(y)[x] = static_cast<T>(layer_planarrgb_add_scale_anchor(
            anchors[(index + plane * 2U) % anchors.size()], max_value));
        overlay[plane]->view().row(y)[x] = static_cast<T>(layer_planarrgb_add_scale_anchor(
            anchors[(index + plane * 3U + 4U) % anchors.size()], max_value));
      }
    }
  }
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width_samples; ++x) {
      const auto index = y * test_case.width_samples + x;
      mask.view().row(y)[x] = static_cast<T>(layer_planarrgb_add_scale_anchor(
          anchors[(index * 3U + y + 1U) % anchors.size()], max_value));
    }
  }
}

template <typename T>
void copy_layer_planarrgb_add_planes(
    const LayerPlanarRgbAddCase& test_case,
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
void apply_layer_planarrgb_add_reference(
    const LayerPlanarRgbAddCase& test_case,
    const std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& overlay,
    const GuardedVideoBuffer<T>& mask,
    std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4>& destination) {
  const auto plane_count = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};
  const auto max_value = (std::uint32_t{1} << test_case.bits_per_pixel) - 1U;
  const auto half = max_value / 2U;
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width_samples; ++x) {
      const auto alpha = (static_cast<std::uint32_t>(mask.view().row(y)[x]) *
                              static_cast<std::uint32_t>(test_case.opacity) +
                          half) /
                         max_value;
      const auto inverse_alpha = max_value - alpha;
      for (std::size_t plane = 0; plane < plane_count; ++plane) {
        const auto dst = static_cast<std::uint32_t>(destination[plane]->view().row(y)[x]);
        const auto source = static_cast<std::uint32_t>(overlay[plane]->view().row(y)[x]);
        destination[plane]->view().row(y)[x] =
            static_cast<T>((dst * inverse_alpha + source * alpha + half) / max_value);
      }
    }
  }
}

template <typename T>
std::uint64_t hash_layer_planarrgb_add_active(
    const LayerPlanarRgbAddCase& test_case,
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
void run_layer_planarrgb_add_case(const LayerPlanarRgbAddCase& test_case) {
  const auto plane_count = test_case.blend_alpha ? std::size_t{4} : std::size_t{3};
  std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4> destination;
  std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4> overlay;
  std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4> expected;
  std::array<std::unique_ptr<GuardedVideoBuffer<T>>, 4> actual;
  for (std::size_t plane = 0; plane < plane_count; ++plane) {
    destination[plane] = std::make_unique<GuardedVideoBuffer<T>>(
        test_case.width_samples, test_case.height, test_case.destination_pitch, 64);
    overlay[plane] = std::make_unique<GuardedVideoBuffer<T>>(
        test_case.width_samples, test_case.height, test_case.overlay_pitch, 64);
    expected[plane] = std::make_unique<GuardedVideoBuffer<T>>(
        test_case.width_samples, test_case.height, test_case.destination_pitch, 64);
    actual[plane] = std::make_unique<GuardedVideoBuffer<T>>(
        test_case.width_samples, test_case.height, test_case.destination_pitch, 64);
  }
  GuardedVideoBuffer<T> mask(test_case.width_samples, test_case.height, test_case.mask_pitch, 64);

  fill_layer_planarrgb_add_inputs(test_case, destination, overlay, mask);
  copy_layer_planarrgb_add_planes(test_case, destination, expected);
  copy_layer_planarrgb_add_planes(test_case, destination, actual);
  apply_layer_planarrgb_add_reference(test_case, overlay, mask, expected);

  std::array<BYTE*, 4> actual_ptrs{};
  std::array<const BYTE*, 4> overlay_ptrs{};
  for (std::size_t plane = 0; plane < plane_count; ++plane) {
    actual_ptrs[plane] = reinterpret_cast<BYTE*>(actual[plane]->view().data());
    overlay_ptrs[plane] = reinterpret_cast<const BYTE*>(overlay[plane]->view().data());
  }
  const auto overlay_snapshots = [&]() {
    std::array<std::vector<std::uint8_t>, 4> snapshots;
    for (std::size_t plane = 0; plane < plane_count; ++plane) {
      snapshots[plane] = overlay[plane]->snapshot_active();
    }
    return snapshots;
  }();
  const auto mask_snapshot = mask.snapshot_active();

  test_case.variant.function(
      actual_ptrs.data(), overlay_ptrs.data(), reinterpret_cast<const BYTE*>(mask.view().data()),
      static_cast<int>(test_case.destination_pitch), static_cast<int>(test_case.overlay_pitch),
      static_cast<int>(test_case.mask_pitch), static_cast<int>(test_case.width_samples),
      static_cast<int>(test_case.height), test_case.opacity, test_case.bits_per_pixel);

  for (std::size_t plane = 0; plane < plane_count; ++plane) {
    EXPECT_TRUE(compare_exact(expected[plane]->view().as_const(), actual[plane]->view().as_const()))
        << test_case.name << " reference mismatch for plane " << plane << " variant "
        << test_case.variant.name;
  }
  const auto actual_hash = format_hash(hash_layer_planarrgb_add_active(test_case, actual));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;
  for (std::size_t plane = 0; plane < plane_count; ++plane) {
    EXPECT_TRUE(overlay[plane]->active_matches(overlay_snapshots[plane]))
        << test_case.name << " modified overlay plane " << plane;
    EXPECT_TRUE(overlay[plane]->memory_intact())
        << test_case.name << " corrupted overlay plane " << plane;
    EXPECT_TRUE(expected[plane]->memory_intact())
        << test_case.name << " corrupted reference plane " << plane;
    EXPECT_TRUE(actual[plane]->memory_intact())
        << test_case.name << " corrupted output plane " << plane;
  }
  EXPECT_TRUE(mask.active_matches(mask_snapshot)) << test_case.name << " modified mask input";
  EXPECT_TRUE(mask.memory_intact()) << test_case.name << " corrupted mask padding or guards";
}

}  // namespace avsut::test
