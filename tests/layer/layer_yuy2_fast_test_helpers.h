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
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace avsut::test {

using LayerYuy2FastFunction = void (*)(BYTE*, const BYTE*, int, int, int, int, int);

struct LayerYuy2FastCase {
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t destination_pitch{};
  std::size_t overlay_pitch{};
  std::size_t destination_alignment_offset{};
  std::size_t overlay_alignment_offset{};
  Variant<LayerYuy2FastFunction> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline std::string layer_yuy2_fast_variant_name(const Variant<LayerYuy2FastFunction>& variant) {
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

inline std::string layer_yuy2_fast_case_name(const LayerYuy2FastCase& test_case) {
  std::ostringstream stream;
  stream << "Yuy2_Width" << test_case.width_pixels << "_Height" << test_case.height << "_DstPitch"
         << test_case.destination_pitch << "_OverlayPitch" << test_case.overlay_pitch
         << "_DstOffset" << test_case.destination_alignment_offset << "_OverlayOffset"
         << test_case.overlay_alignment_offset;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryAnchors_" : "_PatternFixedRandom_")
         << layer_yuy2_fast_variant_name(test_case.variant);
  return stream.str();
}

inline LayerYuy2FastCase make_layer_yuy2_fast_case(std::size_t width_pixels, std::size_t height,
                                                   std::size_t destination_pitch,
                                                   std::size_t overlay_pitch,
                                                   std::size_t destination_alignment_offset,
                                                   std::size_t overlay_alignment_offset,
                                                   Variant<LayerYuy2FastFunction> variant,
                                                   std::string expected_hash = {},
                                                   std::uint32_t seed = 0) {
  const auto row_bytes = width_pixels * 2;
  if (width_pixels == 0 || height == 0 || destination_pitch < row_bytes ||
      overlay_pitch < row_bytes || destination_alignment_offset >= 64 ||
      overlay_alignment_offset >= 64) {
    throw std::invalid_argument("Layer YUY2 fast rows must contain active pixels");
  }
  LayerYuy2FastCase result{width_pixels,
                           height,
                           destination_pitch,
                           overlay_pitch,
                           destination_alignment_offset,
                           overlay_alignment_offset,
                           std::move(variant),
                           std::move(expected_hash),
                           seed,
                           {}};
  result.name = layer_yuy2_fast_case_name(result);
  return result;
}

inline void PrintTo(const LayerYuy2FastCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_layer_yuy2_fast_inputs(PlaneView<std::uint8_t> destination,
                                        PlaneView<std::uint8_t> overlay, std::uint32_t seed = 0) {
  if (seed != 0) {
    fill_random(destination, seed);
    fill_random(overlay, seed ^ 0xA5A5A5A5U);
    return;
  }
  constexpr std::array<std::uint8_t, 12> anchors{0U,   1U,   2U,   3U,   63U,  64U,
                                                 127U, 128U, 191U, 192U, 254U, 255U};
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < destination.width(); ++x) {
      destination.row(y)[x] = anchors[(x + y * 5U) % anchors.size()];
      overlay.row(y)[x] = anchors[(x * 3U + y * 7U + 4U) % anchors.size()];
    }
  }
}

inline void apply_layer_yuy2_fast_reference(PlaneView<const std::uint8_t> overlay,
                                            PlaneView<std::uint8_t> destination) {
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < destination.width(); ++x) {
      destination.row(y)[x] = static_cast<std::uint8_t>(
          (static_cast<unsigned int>(destination.row(y)[x]) + overlay.row(y)[x] + 1U) / 2U);
    }
  }
}

inline void run_layer_yuy2_fast_case(const LayerYuy2FastCase& test_case) {
  const auto row_bytes = test_case.width_pixels * 2;
  GuardedVideoBuffer<std::uint8_t> destination(row_bytes, test_case.height,
                                               test_case.destination_pitch, 64,
                                               test_case.destination_alignment_offset, 0xe1);
  GuardedVideoBuffer<std::uint8_t> overlay(row_bytes, test_case.height, test_case.overlay_pitch, 64,
                                           test_case.overlay_alignment_offset, 0x3c);
  GuardedVideoBuffer<std::uint8_t> expected(row_bytes, test_case.height,
                                            test_case.destination_pitch, 64,
                                            test_case.destination_alignment_offset, 0xe1);

  fill_layer_yuy2_fast_inputs(destination.view(), overlay.view(), test_case.seed);
  for (std::size_t y = 0; y < destination.view().height(); ++y) {
    std::copy_n(destination.view().row(y), destination.view().width(), expected.view().row(y));
  }
  apply_layer_yuy2_fast_reference(overlay.view().as_const(), expected.view());
  const auto overlay_snapshot = overlay.snapshot_active();

  test_case.variant.function(reinterpret_cast<BYTE*>(destination.view().data()),
                             reinterpret_cast<const BYTE*>(overlay.view().data()),
                             static_cast<int>(destination.view().pitch_bytes()),
                             static_cast<int>(overlay.view().pitch_bytes()),
                             static_cast<int>(test_case.width_pixels),
                             static_cast<int>(test_case.height), 173);

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
