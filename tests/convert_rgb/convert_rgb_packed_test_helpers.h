#pragma once

#include "convert_rgb_test_helpers.h"

#include "convert/intel/convert_rgb_avx2.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace avsut::test {

using PlanarToPackedRgbFunction = void (*)(const BYTE* (&)[4], BYTE*, int (&)[4], int, int, int);

struct PlanarToPackedRgbCase {
  std::string operation;
  std::size_t bytes_per_component{};
  bool source_has_alpha{};
  std::size_t width_pixels{};
  std::size_t height{};
  std::array<std::size_t, 4> source_pitches{};
  std::size_t destination_pitch{};
  Variant<PlanarToPackedRgbFunction> variant;
  std::string expected_hash;
  std::string name;
};

inline std::string planar_to_packed_rgb_case_name(const PlanarToPackedRgbCase& test_case) {
  std::ostringstream stream;
  stream << test_case.operation << "_Width" << test_case.width_pixels << "_Height"
         << test_case.height << "_SrcPitches" << test_case.source_pitches[0] << '_'
         << test_case.source_pitches[1] << '_' << test_case.source_pitches[2] << '_'
         << test_case.source_pitches[3] << "_DstPitch" << test_case.destination_pitch
         << "_PatternChannelAnchors_" << convert_rgb_variant_name(test_case.variant);
  return stream.str();
}

inline PlanarToPackedRgbCase make_planar_to_packed_rgb_case(
    std::string operation, std::size_t bytes_per_component, bool source_has_alpha,
    std::size_t width_pixels, std::size_t height, std::array<std::size_t, 4> source_pitches,
    std::size_t destination_pitch, Variant<PlanarToPackedRgbFunction> variant,
    std::string expected_hash) {
  PlanarToPackedRgbCase result{std::move(operation),
                               bytes_per_component,
                               source_has_alpha,
                               width_pixels,
                               height,
                               std::move(source_pitches),
                               destination_pitch,
                               std::move(variant),
                               std::move(expected_hash),
                               {}};
  result.name = planar_to_packed_rgb_case_name(result);
  return result;
}

inline void PrintTo(const PlanarToPackedRgbCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_planar_rgb_input(PlaneView<T> green, PlaneView<T> blue, PlaneView<T> red,
                           PlaneView<T> alpha) {
  static_assert(std::is_integral_v<T>);
  constexpr std::array<std::uint32_t, 16> anchors{0U,      1U,      2U,      15U,    16U,  127U,
                                                  128U,    191U,    254U,    255U,   256U, 0x7fffU,
                                                  0x8000U, 0xff00U, 0xfffeU, 0xffffU};
  const auto max_value = static_cast<std::uint32_t>(std::numeric_limits<T>::max());
  for (std::size_t y = 0; y < green.height(); ++y) {
    for (std::size_t x = 0; x < green.width(); ++x) {
      const auto sample = [&](std::size_t channel) {
        const auto anchor = anchors[(x * 3U + y * 5U + channel * 7U) % anchors.size()];
        const auto perturbation = static_cast<std::uint32_t>(x * 509U + y * 2053U + channel * 149U);
        return static_cast<T>((anchor + perturbation) & max_value);
      };
      green.row(y)[x] = sample(0);
      blue.row(y)[x] = sample(1);
      red.row(y)[x] = sample(2);
      alpha.row(y)[x] = sample(3);
    }
  }
}

template <typename T>
void make_planar_to_packed_rgb_reference(std::array<PlaneView<const T>, 4> source,
                                         PlaneView<T> destination,
                                         const PlanarToPackedRgbCase& test_case) {
  const auto opaque_alpha = std::numeric_limits<T>::max();
  for (std::size_t y = 0; y < test_case.height; ++y) {
    // Packed RGB storage is bottom-up while planar input is top-down.
    auto* destination_row = destination.row(test_case.height - 1 - y);
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      destination_row[x * 4 + 0] = source[1].row(y)[x];
      destination_row[x * 4 + 1] = source[0].row(y)[x];
      destination_row[x * 4 + 2] = source[2].row(y)[x];
      destination_row[x * 4 + 3] = test_case.source_has_alpha ? source[3].row(y)[x] : opaque_alpha;
    }
  }
}

template <typename T>
void run_planar_to_packed_rgb_case(const PlanarToPackedRgbCase& test_case) {
  static_assert(std::is_integral_v<T>);
  GuardedVideoBuffer<T> source_g(test_case.width_pixels, test_case.height,
                                 test_case.source_pitches[0], 64);
  GuardedVideoBuffer<T> source_b(test_case.width_pixels, test_case.height,
                                 test_case.source_pitches[1], 64);
  GuardedVideoBuffer<T> source_r(test_case.width_pixels, test_case.height,
                                 test_case.source_pitches[2], 64);
  GuardedVideoBuffer<T> source_a(test_case.width_pixels, test_case.height,
                                 test_case.source_pitches[3], 64);
  GuardedVideoBuffer<T> expected(test_case.width_pixels * 4, test_case.height,
                                 test_case.destination_pitch, 64);
  GuardedVideoBuffer<T> actual(test_case.width_pixels * 4, test_case.height,
                               test_case.destination_pitch, 64);

  fill_planar_rgb_input(source_g.view(), source_b.view(), source_r.view(), source_a.view());
  const auto source_g_snapshot = source_g.snapshot_active();
  const auto source_b_snapshot = source_b.snapshot_active();
  const auto source_r_snapshot = source_r.snapshot_active();
  const auto source_a_snapshot = source_a.snapshot_active();
  make_planar_to_packed_rgb_reference({source_g.view().as_const(), source_b.view().as_const(),
                                       source_r.view().as_const(), source_a.view().as_const()},
                                      expected.view(), test_case);

  const BYTE* source[4]{reinterpret_cast<const BYTE*>(source_g.view().data()),
                        reinterpret_cast<const BYTE*>(source_b.view().data()),
                        reinterpret_cast<const BYTE*>(source_r.view().data()),
                        reinterpret_cast<const BYTE*>(source_a.view().data())};
  int source_pitches[4]{static_cast<int>(source_g.view().pitch_bytes()),
                        static_cast<int>(source_b.view().pitch_bytes()),
                        static_cast<int>(source_r.view().pitch_bytes()),
                        static_cast<int>(source_a.view().pitch_bytes())};
  auto* destination_bottom_row = reinterpret_cast<BYTE*>(actual.view().row(test_case.height - 1));
  test_case.variant.function(
      source, destination_bottom_row, source_pitches, static_cast<int>(actual.view().pitch_bytes()),
      static_cast<int>(test_case.width_pixels), static_cast<int>(test_case.height));

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch";
  EXPECT_TRUE(source_g.active_matches(source_g_snapshot)) << test_case.name;
  EXPECT_TRUE(source_b.active_matches(source_b_snapshot)) << test_case.name;
  EXPECT_TRUE(source_r.active_matches(source_r_snapshot)) << test_case.name;
  EXPECT_TRUE(source_a.active_matches(source_a_snapshot)) << test_case.name;
  EXPECT_TRUE(source_g.memory_intact()) << test_case.name;
  EXPECT_TRUE(source_b.memory_intact()) << test_case.name;
  EXPECT_TRUE(source_r.memory_intact()) << test_case.name;
  EXPECT_TRUE(source_a.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.memory_intact()) << test_case.name;
}

}  // namespace avsut::test
