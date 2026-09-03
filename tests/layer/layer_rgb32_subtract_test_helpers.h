#pragma once

#include "filters/intel/layer_avx2.h"
#include "filters/intel/layer_sse.h"

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

using LayerRgb32SubtractFunction = void (*)(BYTE*, const BYTE*, int, int, int, int, int);

struct LayerRgb32SubtractCase {
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t destination_pitch{};
  std::size_t overlay_pitch{};
  int level{};
  std::string level_name;
  Variant<LayerRgb32SubtractFunction> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline std::string layer_rgb32_subtract_variant_name(
    const Variant<LayerRgb32SubtractFunction>& variant) {
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

inline std::string layer_rgb32_subtract_case_name(const LayerRgb32SubtractCase& test_case) {
  std::ostringstream stream;
  stream << "Rgb32_SubtractLuma_Width" << test_case.width_pixels << "_Height" << test_case.height
         << "_DstPitch" << test_case.destination_pitch << "_OverlayPitch" << test_case.overlay_pitch
         << "_Level" << test_case.level_name;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryAnchors_" : "_PatternFixedRandom_")
         << layer_rgb32_subtract_variant_name(test_case.variant);
  return stream.str();
}

inline LayerRgb32SubtractCase make_layer_rgb32_subtract_case(
    std::size_t width_pixels, std::size_t height, std::size_t destination_pitch,
    std::size_t overlay_pitch, int level, std::string level_name,
    Variant<LayerRgb32SubtractFunction> variant, std::string expected_hash = {},
    std::uint32_t seed = 0) {
  if (width_pixels == 0 || height == 0 || destination_pitch < width_pixels * 4 ||
      overlay_pitch < width_pixels * 4) {
    throw std::invalid_argument("Layer RGB32 subtract rows must contain active pixels");
  }
  if (level < 0 || level > 257) {
    throw std::invalid_argument("Layer RGB32 subtract level must be in 0..257");
  }
  LayerRgb32SubtractCase result{width_pixels,
                                height,
                                destination_pitch,
                                overlay_pitch,
                                level,
                                std::move(level_name),
                                std::move(variant),
                                std::move(expected_hash),
                                seed,
                                {}};
  result.name = layer_rgb32_subtract_case_name(result);
  return result;
}

inline void PrintTo(const LayerRgb32SubtractCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_layer_rgb32_subtract_inputs(PlaneView<std::uint8_t> destination,
                                             PlaneView<std::uint8_t> overlay,
                                             std::uint32_t seed = 0) {
  if (seed != 0) {
    fill_random(destination, seed);
    fill_random(overlay, seed ^ 0xA5A5A5A5U);
    return;
  }
  constexpr std::array<std::uint8_t, 12> anchors{0U,   1U,   2U,   17U,  63U,  64U,
                                                 127U, 128U, 191U, 192U, 254U, 255U};
  const auto width_pixels = destination.width() / 4;
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      auto* dst = destination.row(y) + x * 4;
      auto* ovr = overlay.row(y) + x * 4;
      for (std::size_t channel = 0; channel < 3; ++channel) {
        dst[channel] = anchors[(x * 3U + y * 5U + channel + 1U) % anchors.size()];
        ovr[channel] = anchors[(x * 5U + y * 7U + channel + 4U) % anchors.size()];
      }
      dst[3] = anchors[(x * 7U + y * 3U + 2U) % anchors.size()];
      ovr[3] = anchors[(x * 11U + y * 2U + 6U) % anchors.size()];
    }
  }
}

inline void apply_layer_rgb32_subtract_reference(const LayerRgb32SubtractCase& test_case,
                                                 PlaneView<const std::uint8_t> overlay,
                                                 PlaneView<std::uint8_t> destination) {
  constexpr int blue_coefficient = 3736;
  constexpr int green_coefficient = 19234;
  constexpr int red_coefficient = 9798;
  constexpr int round = 128;
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto* ovr = overlay.row(y) + x * 4;
      auto* dst = destination.row(y) + x * 4;
      const auto alpha = (static_cast<int>(ovr[3]) * test_case.level + 1) >> 8;
      const auto luma = (blue_coefficient * (255 - ovr[0]) + green_coefficient * (255 - ovr[1]) +
                         red_coefficient * (255 - ovr[2])) >>
                        15;
      for (std::size_t channel = 0; channel < 4; ++channel) {
        dst[channel] = static_cast<std::uint8_t>(
            dst[channel] + (((luma - static_cast<int>(dst[channel])) * alpha + round) >> 8));
      }
    }
  }
}

inline void run_layer_rgb32_subtract_case(const LayerRgb32SubtractCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> destination(test_case.width_pixels * 4, test_case.height,
                                               test_case.destination_pitch, 64, 0, 0xb4);
  GuardedVideoBuffer<std::uint8_t> overlay(test_case.width_pixels * 4, test_case.height,
                                           test_case.overlay_pitch, 64, 0, 0x4b);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_pixels * 4, test_case.height,
                                            test_case.destination_pitch, 64, 0, 0xb4);

  fill_layer_rgb32_subtract_inputs(destination.view(), overlay.view(), test_case.seed);
  for (std::size_t y = 0; y < destination.view().height(); ++y) {
    std::copy_n(destination.view().row(y), destination.view().width(), expected.view().row(y));
  }
  apply_layer_rgb32_subtract_reference(test_case, overlay.view().as_const(), expected.view());
  const auto overlay_snapshot = overlay.snapshot_active();

  test_case.variant.function(reinterpret_cast<BYTE*>(destination.view().data()),
                             reinterpret_cast<const BYTE*>(overlay.view().data()),
                             static_cast<int>(destination.view().pitch_bytes()),
                             static_cast<int>(overlay.view().pitch_bytes()),
                             static_cast<int>(test_case.width_pixels),
                             static_cast<int>(test_case.height), test_case.level);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), destination.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  const auto actual_hash = format_hash(hash_active(destination.view().as_const()));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(overlay.active_matches(overlay_snapshot))
      << test_case.name << " modified the overlay input";
  EXPECT_TRUE(destination.memory_intact())
      << test_case.name << " corrupted destination padding or guards";
  EXPECT_TRUE(overlay.memory_intact()) << test_case.name << " corrupted overlay padding or guards";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " corrupted reference padding or guards";
}

}  // namespace avsut::test
