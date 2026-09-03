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
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace avsut::test {

using LayerPackedBlendFuncPtr = layer_packedrgb_blend_c_t*;

struct LayerPackedBlendCase {
  bool has_separate_mask{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t destination_pitch{};
  std::size_t overlay_pitch{};
  std::size_t mask_pitch{};
  int opacity{};
  std::string opacity_name;
  Variant<LayerPackedBlendFuncPtr> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline std::string layer_packed_blend_variant_name(
    const Variant<LayerPackedBlendFuncPtr>& variant) {
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

inline std::string layer_packed_blend_case_name(const LayerPackedBlendCase& test_case) {
  std::ostringstream stream;
  stream << "Rgb32_" << (test_case.has_separate_mask ? "SeparateMask" : "OverlayAlpha") << "_Width"
         << test_case.width_pixels << "_Height" << test_case.height_pixels << "_DstPitch"
         << test_case.destination_pitch << "_OverlayPitch" << test_case.overlay_pitch
         << "_MaskPitch" << test_case.mask_pitch << "_Opacity" << test_case.opacity_name;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryAnchors_" : "_PatternFixedRandom_")
         << layer_packed_blend_variant_name(test_case.variant);
  return stream.str();
}

inline LayerPackedBlendCase make_layer_packed_blend_case(
    bool has_separate_mask, std::size_t width_pixels, std::size_t height_pixels,
    std::size_t destination_pitch, std::size_t overlay_pitch, std::size_t mask_pitch, int opacity,
    std::string opacity_name, Variant<LayerPackedBlendFuncPtr> variant,
    std::string expected_hash = {}, std::uint32_t seed = 0) {
  if (destination_pitch < width_pixels * 4 || overlay_pitch < width_pixels * 4 ||
      mask_pitch < width_pixels) {
    throw std::invalid_argument("Layer packed blend pitches must contain the active row");
  }
  if (opacity < 0 || opacity > 255) {
    throw std::invalid_argument("Layer packed blend opacity must be in 0..255");
  }
  LayerPackedBlendCase result{has_separate_mask,
                              width_pixels,
                              height_pixels,
                              destination_pitch,
                              overlay_pitch,
                              mask_pitch,
                              opacity,
                              std::move(opacity_name),
                              std::move(variant),
                              std::move(expected_hash),
                              seed,
                              {}};
  result.name = layer_packed_blend_case_name(result);
  return result;
}

inline void PrintTo(const LayerPackedBlendCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_layer_packed_blend_inputs(PlaneView<std::uint8_t> destination,
                                           PlaneView<std::uint8_t> overlay,
                                           PlaneView<std::uint8_t> mask,
                                           std::uint32_t seed = 0) {
  if (seed != 0) {
    fill_random(destination, seed);
    fill_random(overlay, seed ^ 0xA5A5A5A5U);
    fill_random(mask, seed ^ 0x5A5A5A5AU);
    return;
  }
  constexpr std::array<std::uint8_t, 10> channels{0, 1, 2, 63, 64, 127, 128, 192, 254, 255};
  constexpr std::array<std::uint8_t, 8> alpha{0, 1, 17, 64, 127, 128, 239, 255};
  const auto width_pixels = destination.width() / 4;
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto pixel = y * width_pixels + x;
      for (std::size_t channel = 0; channel < 4; ++channel) {
        destination.row(y)[x * 4 + channel] = channels[(pixel * 4 + channel) % channels.size()];
        overlay.row(y)[x * 4 + channel] = channels[(pixel * 4 + channel + 3) % channels.size()];
      }
      overlay.row(y)[x * 4 + 3] = alpha[(pixel + 2) % alpha.size()];
      mask.row(y)[x] = alpha[(pixel + 5) % alpha.size()];
    }
  }
}

inline void apply_layer_packed_blend_reference(const LayerPackedBlendCase& test_case,
                                               PlaneView<const std::uint8_t> overlay,
                                               PlaneView<const std::uint8_t> mask,
                                               PlaneView<std::uint8_t> destination) {
  constexpr std::uint32_t max_value = 255;
  constexpr std::uint32_t half = 127;
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto alpha_source = test_case.has_separate_mask
                                    ? static_cast<std::uint32_t>(mask.row(y)[x])
                                    : static_cast<std::uint32_t>(overlay.row(y)[x * 4 + 3]);
      const auto alpha_effective =
          (alpha_source * static_cast<std::uint32_t>(test_case.opacity) + half) / max_value;
      const auto inverse_alpha = max_value - alpha_effective;
      for (std::size_t channel = 0; channel < 4; ++channel) {
        const auto destination_value =
            static_cast<std::uint32_t>(destination.row(y)[x * 4 + channel]);
        const auto overlay_value = static_cast<std::uint32_t>(overlay.row(y)[x * 4 + channel]);
        destination.row(y)[x * 4 + channel] = static_cast<std::uint8_t>(
            (destination_value * inverse_alpha + overlay_value * alpha_effective + half) /
            max_value);
      }
    }
  }
}

inline void run_layer_packed_blend_case(const LayerPackedBlendCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> destination(test_case.width_pixels * 4, test_case.height_pixels,
                                               test_case.destination_pitch, 32);
  GuardedVideoBuffer<std::uint8_t> overlay(test_case.width_pixels * 4, test_case.height_pixels,
                                           test_case.overlay_pitch, 32);
  GuardedVideoBuffer<std::uint8_t> mask(test_case.width_pixels, test_case.height_pixels,
                                        test_case.mask_pitch, 32);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_pixels * 4, test_case.height_pixels,
                                            test_case.destination_pitch, 32);
  GuardedVideoBuffer<std::uint8_t> actual(test_case.width_pixels * 4, test_case.height_pixels,
                                          test_case.destination_pitch, 32);

  fill_layer_packed_blend_inputs(destination.view(), overlay.view(), mask.view(), test_case.seed);
  for (std::size_t y = 0; y < destination.view().height(); ++y) {
    std::copy_n(destination.view().row(y), destination.view().width(), expected.view().row(y));
    std::copy_n(destination.view().row(y), destination.view().width(), actual.view().row(y));
  }
  apply_layer_packed_blend_reference(test_case, overlay.view().as_const(), mask.view().as_const(),
                                     expected.view());
  const auto overlay_snapshot = overlay.snapshot_active();
  const auto mask_snapshot = mask.snapshot_active();

  test_case.variant.function(
      reinterpret_cast<BYTE*>(actual.view().data()),
      reinterpret_cast<const BYTE*>(overlay.view().data()),
      test_case.has_separate_mask ? reinterpret_cast<const BYTE*>(mask.view().data()) : nullptr,
      static_cast<int>(actual.view().pitch_bytes()), static_cast<int>(overlay.view().pitch_bytes()),
      test_case.has_separate_mask ? static_cast<int>(mask.view().pitch_bytes()) : 0,
      static_cast<int>(test_case.width_pixels), static_cast<int>(test_case.height_pixels),
      test_case.opacity);

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
