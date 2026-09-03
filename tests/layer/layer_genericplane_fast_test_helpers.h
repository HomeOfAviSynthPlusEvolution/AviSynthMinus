#pragma once

#include "core/internal.h"

#ifndef AVS_UNUSED
#define AVS_UNUSED(value) (void)(value)
#define AVSUT_LAYER_GENERIC_DEFINED_AVS_UNUSED
#endif

#include "filters/intel/layer_avx2.h"
#include "filters/intel/layer_sse.h"

#ifdef AVSUT_LAYER_GENERIC_DEFINED_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_LAYER_GENERIC_DEFINED_AVS_UNUSED
#endif

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
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace avsut::test {

using LayerGenericPlaneFastFunction = void (*)(BYTE*, const BYTE*, int, int, int, int, int);

struct LayerGenericPlaneFastCase {
  int bits_per_sample{};
  std::size_t width_samples{};
  std::size_t height{};
  std::size_t destination_pitch{};
  std::size_t overlay_pitch{};
  std::size_t destination_alignment_offset{};
  std::size_t overlay_alignment_offset{};
  Variant<LayerGenericPlaneFastFunction> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline std::string layer_genericplane_fast_variant_name(
    const Variant<LayerGenericPlaneFastFunction>& variant) {
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

inline std::string layer_genericplane_fast_case_name(const LayerGenericPlaneFastCase& test_case) {
  std::ostringstream stream;
  stream << "Plane_Bps" << test_case.bits_per_sample << "_Width" << test_case.width_samples
         << "_Height" << test_case.height << "_DstPitch" << test_case.destination_pitch
         << "_OverlayPitch" << test_case.overlay_pitch << "_DstOffset"
         << test_case.destination_alignment_offset << "_OverlayOffset"
         << test_case.overlay_alignment_offset;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryAnchors_" : "_PatternFixedRandom_")
         << layer_genericplane_fast_variant_name(test_case.variant);
  return stream.str();
}

inline LayerGenericPlaneFastCase make_layer_genericplane_fast_case(
    int bits_per_sample, std::size_t width_samples, std::size_t height,
    std::size_t destination_pitch, std::size_t overlay_pitch,
    std::size_t destination_alignment_offset, std::size_t overlay_alignment_offset,
    Variant<LayerGenericPlaneFastFunction> variant, std::string expected_hash = {},
    std::uint32_t seed = 0) {
  const auto bytes_per_sample = static_cast<std::size_t>(bits_per_sample / 8);
  if ((bits_per_sample != 8 && bits_per_sample != 16) || width_samples == 0 || height == 0 ||
      destination_pitch < width_samples * bytes_per_sample ||
      overlay_pitch < width_samples * bytes_per_sample || destination_alignment_offset >= 64 ||
      overlay_alignment_offset >= 64) {
    throw std::invalid_argument("invalid Layer generic-plane dimensions or parameters");
  }
  LayerGenericPlaneFastCase result{bits_per_sample,
                                   width_samples,
                                   height,
                                   destination_pitch,
                                   overlay_pitch,
                                   destination_alignment_offset,
                                   overlay_alignment_offset,
                                   std::move(variant),
                                   std::move(expected_hash),
                                   seed,
                                   {}};
  result.name = layer_genericplane_fast_case_name(result);
  return result;
}

inline void PrintTo(const LayerGenericPlaneFastCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
inline T layer_genericplane_fast_anchor(std::uint32_t anchor) {
  const auto max_value = static_cast<std::uint64_t>(std::numeric_limits<T>::max());
  return static_cast<T>((anchor * max_value + 127U) / 255U);
}

template <typename T>
inline void fill_layer_genericplane_fast_inputs(PlaneView<T> destination, PlaneView<T> overlay,
                                                std::uint32_t seed = 0) {
  if (seed != 0) {
    fill_random(destination, seed);
    fill_random(overlay, seed ^ 0xA5A5A5A5U);
    return;
  }
  constexpr std::array<std::uint32_t, 12> anchors{0U,   1U,   2U,   17U,  63U,  64U,
                                                  127U, 128U, 191U, 223U, 254U, 255U};
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < destination.width(); ++x) {
      destination.row(y)[x] =
          layer_genericplane_fast_anchor<T>(anchors[(x + 5 * y + 1) % anchors.size()]);
      overlay.row(y)[x] =
          layer_genericplane_fast_anchor<T>(anchors[(3 * x + 7 * y + 4) % anchors.size()]);
    }
  }
}

template <typename T>
inline void apply_layer_genericplane_fast_reference(PlaneView<T> destination,
                                                    PlaneView<const T> overlay) {
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < destination.width(); ++x) {
      destination.row(y)[x] = static_cast<T>(
          (static_cast<std::uint64_t>(destination.row(y)[x]) + overlay.row(y)[x] + 1U) / 2U);
    }
  }
}

template <typename T>
inline void run_layer_genericplane_fast_case(const LayerGenericPlaneFastCase& test_case) {
  GuardedVideoBuffer<T> destination(test_case.width_samples, test_case.height,
                                    test_case.destination_pitch, 64,
                                    test_case.destination_alignment_offset, 0xe1);
  GuardedVideoBuffer<T> overlay(test_case.width_samples, test_case.height, test_case.overlay_pitch,
                                64, test_case.overlay_alignment_offset, 0x3c);
  GuardedVideoBuffer<T> expected(test_case.width_samples, test_case.height,
                                 test_case.destination_pitch, 64,
                                 test_case.destination_alignment_offset, 0xe1);

  fill_layer_genericplane_fast_inputs(destination.view(), overlay.view(), test_case.seed);
  for (std::size_t y = 0; y < destination.view().height(); ++y) {
    std::copy_n(destination.view().row(y), destination.view().width(), expected.view().row(y));
  }
  apply_layer_genericplane_fast_reference(expected.view(), overlay.view().as_const());
  const auto overlay_snapshot = overlay.snapshot_active();

  test_case.variant.function(reinterpret_cast<BYTE*>(destination.view().data()),
                             reinterpret_cast<const BYTE*>(overlay.view().data()),
                             static_cast<int>(destination.view().pitch_bytes()),
                             static_cast<int>(overlay.view().pitch_bytes()),
                             static_cast<int>(test_case.width_samples),
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
