#pragma once

#include "filters/intel/layer_avx2.h"
#include "filters/intel/layer_sse.h"

#include "support/comparators.h"
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

using LayerColorKeyMaskFunction = void (*)(BYTE*, int, int, int, int, int, int, int);

struct LayerColorKeyMaskCase {
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t pitch{};
  int color{};
  int tolerance_b{};
  int tolerance_g{};
  int tolerance_r{};
  Variant<LayerColorKeyMaskFunction> variant;
  std::string expected_hash;
  std::string name;
};

inline std::string layer_colorkey_variant_name(const Variant<LayerColorKeyMaskFunction>& variant) {
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

inline std::string layer_colorkey_case_name(const LayerColorKeyMaskCase& test_case) {
  std::ostringstream stream;
  stream << "Rgb32_Width" << test_case.width_pixels << "_Height" << test_case.height << "_Pitch"
         << test_case.pitch << "_Color" << std::hex << std::uppercase << test_case.color << std::dec
         << "_TolB" << test_case.tolerance_b << "_TolG" << test_case.tolerance_g << "_TolR"
         << test_case.tolerance_r << "_PatternBoundaryAnchors_"
         << layer_colorkey_variant_name(test_case.variant);
  return stream.str();
}

inline LayerColorKeyMaskCase make_layer_colorkey_case(std::size_t width_pixels, std::size_t height,
                                                      std::size_t pitch, int color, int tolerance_b,
                                                      int tolerance_g, int tolerance_r,
                                                      Variant<LayerColorKeyMaskFunction> variant,
                                                      std::string expected_hash = {}) {
  if (width_pixels == 0 || height == 0 || pitch < width_pixels * 4 || (pitch % 16) != 0) {
    throw std::invalid_argument("Layer color-key rows must have a non-zero 16-byte pitch");
  }
  if (tolerance_b < 0 || tolerance_b > 255 || tolerance_g < 0 || tolerance_g > 255 ||
      tolerance_r < 0 || tolerance_r > 255) {
    throw std::invalid_argument("Layer color-key tolerances must be in 0..255");
  }
  LayerColorKeyMaskCase result{width_pixels,
                               height,
                               pitch,
                               color,
                               tolerance_b,
                               tolerance_g,
                               tolerance_r,
                               std::move(variant),
                               std::move(expected_hash),
                               {}};
  result.name = layer_colorkey_case_name(result);
  return result;
}

inline void PrintTo(const LayerColorKeyMaskCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

struct LayerColorKeyPixel {
  std::uint8_t blue;
  std::uint8_t green;
  std::uint8_t red;
  std::uint8_t alpha;
};

inline constexpr std::array<LayerColorKeyPixel, 13> layer_colorkey_pixels() {
  return {{{40, 120, 200, 17},
           {37, 115, 193, 31},
           {43, 125, 207, 45},
           {44, 120, 200, 59},
           {40, 126, 200, 73},
           {40, 120, 208, 87},
           {42, 116, 204, 101},
           {0, 0, 0, 119},
           {255, 255, 255, 137},
           {40, 120, 200, 255},
           {37, 120, 200, 151},
           {40, 125, 200, 165},
           {40, 120, 193, 179}}};
}

inline void fill_layer_colorkey_input(PlaneView<std::uint8_t> frame) {
  const auto pixels = layer_colorkey_pixels();
  const auto width_pixels = frame.width() / 4;
  for (std::size_t y = 0; y < frame.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto& pixel = pixels[(x + y * 3) % pixels.size()];
      auto* destination = frame.row(y) + x * 4;
      destination[0] = pixel.blue;
      destination[1] = pixel.green;
      destination[2] = pixel.red;
      destination[3] = pixel.alpha;
    }
  }
}

inline bool layer_colorkey_channel_matches(std::uint8_t value, int target, int tolerance) {
  const auto difference = static_cast<int>(value) - target;
  return difference >= -tolerance && difference <= tolerance;
}

inline void apply_layer_colorkey_reference(const LayerColorKeyMaskCase& test_case,
                                           PlaneView<std::uint8_t> frame) {
  const auto blue = test_case.color & 0xff;
  const auto green = (test_case.color >> 8) & 0xff;
  const auto red = (test_case.color >> 16) & 0xff;
  const auto width_pixels = test_case.width_pixels;
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      auto* pixel = frame.row(y) + x * 4;
      if (layer_colorkey_channel_matches(pixel[0], blue, test_case.tolerance_b) &&
          layer_colorkey_channel_matches(pixel[1], green, test_case.tolerance_g) &&
          layer_colorkey_channel_matches(pixel[2], red, test_case.tolerance_r)) {
        pixel[3] = 0;
      }
    }
  }
}

inline void run_layer_colorkey_case(const LayerColorKeyMaskCase& test_case) {
  constexpr std::uint8_t padding_sentinel = 0xd7;
  GuardedVideoBuffer<std::uint8_t> frame(test_case.width_pixels * 4, test_case.height,
                                         test_case.pitch, 64, 0, padding_sentinel);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_pixels * 4, test_case.height,
                                            test_case.pitch, 64, 0, padding_sentinel);

  fill_layer_colorkey_input(frame.view());
  for (std::size_t y = 0; y < frame.view().height(); ++y) {
    std::copy_n(frame.view().row(y), frame.view().width(), expected.view().row(y));
  }
  apply_layer_colorkey_reference(test_case, expected.view());

  test_case.variant.function(reinterpret_cast<BYTE*>(frame.view().data()),
                             static_cast<int>(frame.view().pitch_bytes()), test_case.color,
                             static_cast<int>(test_case.height),
                             static_cast<int>(test_case.width_pixels * 4), test_case.tolerance_b,
                             test_case.tolerance_g, test_case.tolerance_r);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), frame.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  const auto actual_hash = format_hash(hash_active(frame.view().as_const()));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(frame.memory_intact()) << test_case.name << " corrupted row padding or guards";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " corrupted reference padding or guards";
}

}  // namespace avsut::test
