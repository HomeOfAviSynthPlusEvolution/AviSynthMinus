#pragma once

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
#include <stdexcept>
#include <string>
#include <utility>

namespace avsut::test {

using OverlayDarkLightenFunction = void (*)(BYTE*, BYTE*, BYTE*, const BYTE*, const BYTE*,
                                            const BYTE*, int, int, int, int);

enum class OverlayDarkLightenOperation { Darken, Lighten };

struct OverlayDarkLightenCase {
  OverlayDarkLightenOperation operation{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t destination_pitch{};
  std::size_t overlay_pitch{};
  std::size_t destination_alignment_offset{};
  std::size_t overlay_alignment_offset{};
  Variant<OverlayDarkLightenFunction> variant;
  std::array<std::string, 3> expected_hashes;
  std::uint32_t seed{};
  std::string name;
};

inline const char* overlay_darklighten_operation_name(OverlayDarkLightenOperation operation) {
  return operation == OverlayDarkLightenOperation::Darken ? "Darken" : "Lighten";
}

inline std::string overlay_darklighten_variant_name(
    const Variant<OverlayDarkLightenFunction>& variant) {
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

inline std::string overlay_darklighten_case_name(const OverlayDarkLightenCase& test_case) {
  std::ostringstream stream;
  stream << overlay_darklighten_operation_name(test_case.operation) << "_Width"
         << test_case.width_pixels << "_Height" << test_case.height_pixels << "_DstPitch"
         << test_case.destination_pitch << "_OverlayPitch" << test_case.overlay_pitch
         << "_DstOffset" << test_case.destination_alignment_offset << "_OverlayOffset"
         << test_case.overlay_alignment_offset;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryAnchors_" : "_PatternFixedRandom_")
         << overlay_darklighten_variant_name(test_case.variant);
  return stream.str();
}

inline OverlayDarkLightenCase make_overlay_darklighten_case(
    OverlayDarkLightenOperation operation, std::size_t width_pixels, std::size_t height_pixels,
    std::size_t destination_pitch, std::size_t overlay_pitch,
    std::size_t destination_alignment_offset, std::size_t overlay_alignment_offset,
    Variant<OverlayDarkLightenFunction> variant, std::array<std::string, 3> expected_hashes = {},
    std::uint32_t seed = 0) {
  if (width_pixels == 0 || height_pixels == 0 || destination_pitch < width_pixels ||
      overlay_pitch < width_pixels || destination_alignment_offset >= 32 ||
      overlay_alignment_offset >= 32) {
    throw std::invalid_argument("invalid Overlay lighten/darken dimensions or parameters");
  }
  OverlayDarkLightenCase result{operation,
                                width_pixels,
                                height_pixels,
                                destination_pitch,
                                overlay_pitch,
                                destination_alignment_offset,
                                overlay_alignment_offset,
                                std::move(variant),
                                std::move(expected_hashes),
                                seed,
                                {}};
  result.name = overlay_darklighten_case_name(result);
  return result;
}

inline void PrintTo(const OverlayDarkLightenCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_overlay_darklighten_inputs(PlaneView<std::uint8_t> destination_y,
                                            PlaneView<std::uint8_t> destination_u,
                                            PlaneView<std::uint8_t> destination_v,
                                            PlaneView<std::uint8_t> overlay_y,
                                            PlaneView<std::uint8_t> overlay_u,
                                            PlaneView<std::uint8_t> overlay_v) {
  constexpr std::array<std::uint8_t, 12> anchors{0U,   1U,   2U,   63U,  64U,  127U,
                                                 128U, 191U, 192U, 254U, 255U, 127U};
  for (std::size_t y = 0; y < destination_y.height(); ++y) {
    for (std::size_t x = 0; x < destination_y.width(); ++x) {
      const auto base_index = (x + 3 * y) % anchors.size();
      const auto overlay_index = (5 * x + 7 * y + 2) % anchors.size();
      destination_y.row(y)[x] = anchors[base_index];
      destination_u.row(y)[x] = anchors[(base_index + 4) % anchors.size()];
      destination_v.row(y)[x] = anchors[(base_index + 8) % anchors.size()];
      overlay_y.row(y)[x] = anchors[overlay_index];
      overlay_u.row(y)[x] = anchors[(overlay_index + 3) % anchors.size()];
      overlay_v.row(y)[x] = anchors[(overlay_index + 7) % anchors.size()];
    }
  }
}

inline void copy_overlay_darklighten_active(PlaneView<const std::uint8_t> source,
                                            PlaneView<std::uint8_t> destination) {
  ASSERT_EQ(source.width(), destination.width());
  ASSERT_EQ(source.height(), destination.height());
  for (std::size_t y = 0; y < source.height(); ++y) {
    std::copy_n(source.row(y), source.width(), destination.row(y));
  }
}

inline void apply_overlay_darklighten_reference(const OverlayDarkLightenCase& test_case,
                                                PlaneView<std::uint8_t> destination_y,
                                                PlaneView<std::uint8_t> destination_u,
                                                PlaneView<std::uint8_t> destination_v,
                                                PlaneView<const std::uint8_t> overlay_y,
                                                PlaneView<const std::uint8_t> overlay_u,
                                                PlaneView<const std::uint8_t> overlay_v) {
  for (std::size_t y = 0; y < test_case.height_pixels; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto first = destination_y.row(y)[x];
      const auto second = overlay_y.row(y)[x];
      const bool choose_overlay = test_case.operation == OverlayDarkLightenOperation::Darken
                                      ? second <= first
                                      : second >= first;
      if (choose_overlay) {
        destination_y.row(y)[x] = second;
        destination_u.row(y)[x] = overlay_u.row(y)[x];
        destination_v.row(y)[x] = overlay_v.row(y)[x];
      }
    }
  }
}

inline void run_overlay_darklighten_case(const OverlayDarkLightenCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> destination_y(test_case.width_pixels, test_case.height_pixels,
                                                 test_case.destination_pitch, 32,
                                                 test_case.destination_alignment_offset);
  GuardedVideoBuffer<std::uint8_t> destination_u(test_case.width_pixels, test_case.height_pixels,
                                                 test_case.destination_pitch, 32,
                                                 test_case.destination_alignment_offset);
  GuardedVideoBuffer<std::uint8_t> destination_v(test_case.width_pixels, test_case.height_pixels,
                                                 test_case.destination_pitch, 32,
                                                 test_case.destination_alignment_offset);
  GuardedVideoBuffer<std::uint8_t> overlay_y(test_case.width_pixels, test_case.height_pixels,
                                             test_case.overlay_pitch, 32,
                                             test_case.overlay_alignment_offset);
  GuardedVideoBuffer<std::uint8_t> overlay_u(test_case.width_pixels, test_case.height_pixels,
                                             test_case.overlay_pitch, 32,
                                             test_case.overlay_alignment_offset);
  GuardedVideoBuffer<std::uint8_t> overlay_v(test_case.width_pixels, test_case.height_pixels,
                                             test_case.overlay_pitch, 32,
                                             test_case.overlay_alignment_offset);
  GuardedVideoBuffer<std::uint8_t> expected_y(test_case.width_pixels, test_case.height_pixels,
                                              test_case.destination_pitch, 32,
                                              test_case.destination_alignment_offset);
  GuardedVideoBuffer<std::uint8_t> expected_u(test_case.width_pixels, test_case.height_pixels,
                                              test_case.destination_pitch, 32,
                                              test_case.destination_alignment_offset);
  GuardedVideoBuffer<std::uint8_t> expected_v(test_case.width_pixels, test_case.height_pixels,
                                              test_case.destination_pitch, 32,
                                              test_case.destination_alignment_offset);

  if (test_case.seed != 0) {
    fill_random(destination_y.view(), test_case.seed ^ 0x01010101U);
    fill_random(destination_u.view(), test_case.seed ^ 0x02020202U);
    fill_random(destination_v.view(), test_case.seed ^ 0x03030303U);
    fill_random(overlay_y.view(), test_case.seed ^ 0xA5A5A5A5U);
    fill_random(overlay_u.view(), test_case.seed ^ 0xB6B6B6B6U);
    fill_random(overlay_v.view(), test_case.seed ^ 0xC7C7C7C7U);
  } else {
    fill_overlay_darklighten_inputs(destination_y.view(), destination_u.view(), destination_v.view(),
                                    overlay_y.view(), overlay_u.view(), overlay_v.view());
  }
  copy_overlay_darklighten_active(destination_y.view().as_const(), expected_y.view());
  copy_overlay_darklighten_active(destination_u.view().as_const(), expected_u.view());
  copy_overlay_darklighten_active(destination_v.view().as_const(), expected_v.view());
  apply_overlay_darklighten_reference(test_case, expected_y.view(), expected_u.view(),
                                      expected_v.view(), overlay_y.view().as_const(),
                                      overlay_u.view().as_const(), overlay_v.view().as_const());

  const auto overlay_y_snapshot = overlay_y.snapshot_active();
  const auto overlay_u_snapshot = overlay_u.snapshot_active();
  const auto overlay_v_snapshot = overlay_v.snapshot_active();
  test_case.variant.function(
      reinterpret_cast<BYTE*>(destination_y.view().data()),
      reinterpret_cast<BYTE*>(destination_u.view().data()),
      reinterpret_cast<BYTE*>(destination_v.view().data()),
      reinterpret_cast<const BYTE*>(overlay_y.view().data()),
      reinterpret_cast<const BYTE*>(overlay_u.view().data()),
      reinterpret_cast<const BYTE*>(overlay_v.view().data()),
      static_cast<int>(test_case.destination_pitch), static_cast<int>(test_case.overlay_pitch),
      static_cast<int>(test_case.width_pixels), static_cast<int>(test_case.height_pixels));

  EXPECT_TRUE(compare_exact(expected_y.view().as_const(), destination_y.view().as_const()))
      << test_case.name << " Y reference mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(compare_exact(expected_u.view().as_const(), destination_u.view().as_const()))
      << test_case.name << " U reference mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(compare_exact(expected_v.view().as_const(), destination_v.view().as_const()))
      << test_case.name << " V reference mismatch for variant " << test_case.variant.name;

  const std::array<std::string, 3> actual_hashes{
      format_hash(hash_active(destination_y.view().as_const())),
      format_hash(hash_active(destination_u.view().as_const())),
      format_hash(hash_active(destination_v.view().as_const()))};
  for (std::size_t plane = 0; plane < actual_hashes.size(); ++plane) {
    if (!test_case.expected_hashes[plane].empty()) {
      EXPECT_EQ(actual_hashes[plane], test_case.expected_hashes[plane])
          << test_case.name << " stable hash mismatch for plane " << plane
          << "; actual=" << actual_hashes[plane];
    }
  }

  EXPECT_TRUE(overlay_y.active_matches(overlay_y_snapshot))
      << test_case.name << " modified overlay Y";
  EXPECT_TRUE(overlay_u.active_matches(overlay_u_snapshot))
      << test_case.name << " modified overlay U";
  EXPECT_TRUE(overlay_v.active_matches(overlay_v_snapshot))
      << test_case.name << " modified overlay V";
  EXPECT_TRUE(destination_y.memory_intact()) << test_case.name << " corrupted destination Y";
  EXPECT_TRUE(destination_u.memory_intact()) << test_case.name << " corrupted destination U";
  EXPECT_TRUE(destination_v.memory_intact()) << test_case.name << " corrupted destination V";
  EXPECT_TRUE(overlay_y.memory_intact()) << test_case.name << " corrupted overlay Y";
  EXPECT_TRUE(overlay_u.memory_intact()) << test_case.name << " corrupted overlay U";
  EXPECT_TRUE(overlay_v.memory_intact()) << test_case.name << " corrupted overlay V";
  EXPECT_TRUE(expected_y.memory_intact()) << test_case.name << " corrupted expected Y";
  EXPECT_TRUE(expected_u.memory_intact()) << test_case.name << " corrupted expected U";
  EXPECT_TRUE(expected_v.memory_intact()) << test_case.name << " corrupted expected V";
}

}  // namespace avsut::test
