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
#include <string>
#include <utility>

namespace avsut::test {

using LayerMaskFunction = void (*)(BYTE*, const BYTE*, int, int, std::size_t, std::size_t);

struct LayerMaskCase {
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t alpha_pitch{};
  Variant<LayerMaskFunction> variant;
  std::string expected_hash;
  std::string name;
};

inline std::string layer_mask_variant_name(const Variant<LayerMaskFunction>& variant) {
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

inline std::string layer_mask_case_name(const LayerMaskCase& test_case) {
  std::ostringstream stream;
  stream << "Rgb32_Width" << test_case.width_pixels << "_Height" << test_case.height << "_SrcPitch"
         << test_case.source_pitch << "_AlphaPitch" << test_case.alpha_pitch
         << "_PatternBoundaryAnchors_" << layer_mask_variant_name(test_case.variant);
  return stream.str();
}

inline LayerMaskCase make_layer_mask_case(std::size_t width_pixels, std::size_t height,
                                          std::size_t source_pitch, std::size_t alpha_pitch,
                                          Variant<LayerMaskFunction> variant,
                                          std::string expected_hash = {}) {
  LayerMaskCase result{width_pixels,
                       height,
                       source_pitch,
                       alpha_pitch,
                       std::move(variant),
                       std::move(expected_hash),
                       {}};
  result.name = layer_mask_case_name(result);
  return result;
}

inline void PrintTo(const LayerMaskCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_layer_mask_inputs(PlaneView<std::uint8_t> source, PlaneView<std::uint8_t> alpha) {
  constexpr std::array<std::uint8_t, 10> anchors{0U,   1U,   17U,  63U,  127U,
                                                 128U, 191U, 254U, 255U, 42U};
  const auto width_pixels = source.width() / 4;
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto index = y * width_pixels + x;
      for (std::size_t channel = 0; channel < 4; ++channel) {
        source.row(y)[x * 4 + channel] = anchors[(index * 3U + channel + 1U) % anchors.size()];
        alpha.row(y)[x * 4 + channel] = anchors[(index * 5U + channel * 2U + 4U) % anchors.size()];
      }
    }
  }
}

inline void apply_layer_mask_reference(PlaneView<std::uint8_t> expected,
                                       PlaneView<const std::uint8_t> alpha) {
  constexpr int blue_coefficient = 3736;
  constexpr int green_coefficient = 19234;
  constexpr int red_coefficient = 9798;
  constexpr int round = 16384;
  const auto width_pixels = expected.width() / 4;
  for (std::size_t y = 0; y < expected.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto* alpha_pixel = alpha.row(y) + x * 4;
      const auto luma = (blue_coefficient * alpha_pixel[0] + green_coefficient * alpha_pixel[1] +
                         red_coefficient * alpha_pixel[2] + round) >>
                        15;
      expected.row(y)[x * 4 + 3] = static_cast<std::uint8_t>(luma);
    }
  }
}

inline void run_layer_mask_case(const LayerMaskCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source(test_case.width_pixels * 4, test_case.height,
                                          test_case.source_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> alpha(test_case.width_pixels * 4, test_case.height,
                                         test_case.alpha_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_pixels * 4, test_case.height,
                                            test_case.source_pitch, 64);
  fill_layer_mask_inputs(source.view(), alpha.view());
  for (std::size_t y = 0; y < source.view().height(); ++y) {
    std::copy_n(source.view().row(y), source.view().width(), expected.view().row(y));
  }
  apply_layer_mask_reference(expected.view(), alpha.view().as_const());
  const auto alpha_snapshot = alpha.snapshot_active();

  test_case.variant.function(reinterpret_cast<BYTE*>(source.view().data()),
                             reinterpret_cast<const BYTE*>(alpha.view().data()),
                             static_cast<int>(source.view().pitch_bytes()),
                             static_cast<int>(alpha.view().pitch_bytes()), test_case.width_pixels,
                             test_case.height);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), source.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  const auto actual_hash = format_hash(hash_active(source.view().as_const()));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(alpha.active_matches(alpha_snapshot)) << test_case.name << " modified alpha input";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " corrupted output padding or guards";
  EXPECT_TRUE(alpha.memory_intact()) << test_case.name << " corrupted alpha padding or guards";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " corrupted reference padding or guards";
}

}  // namespace avsut::test
