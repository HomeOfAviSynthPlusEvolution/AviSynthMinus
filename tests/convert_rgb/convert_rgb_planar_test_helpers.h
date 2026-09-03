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

using PackedToPlanarRgbFunction = void (*)(const BYTE*, BYTE* (&)[4], int, int (&)[4], int, int);

template <typename T, bool TargetHasAlpha>
inline void convert_rgb_to_rgbp_ssse3_adapter(const BYTE* srcp, BYTE* (&dstp)[4], int src_pitch, int (&dst_pitch)[4], int width, int height) {
  convert_rgb_to_rgbp_ssse3<T, TargetHasAlpha>(srcp, dstp, src_pitch, dst_pitch, width, height, sizeof(T) * 8);
}

template <typename T, bool TargetHasAlpha>
inline void convert_rgb_to_rgbp_avx2_adapter(const BYTE* srcp, BYTE* (&dstp)[4], int src_pitch, int (&dst_pitch)[4], int width, int height) {
  convert_rgb_to_rgbp_avx2<T, TargetHasAlpha>(srcp, dstp, src_pitch, dst_pitch, width, height, sizeof(T) * 8);
}

struct PackedToPlanarRgbCase {
  std::string operation;
  std::size_t bytes_per_component{};
  std::size_t source_components{};
  bool target_has_alpha{};
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::array<std::size_t, 4> destination_pitches{};
  Variant<PackedToPlanarRgbFunction> variant;
  std::array<std::string, 4> expected_hashes;
  std::string name;
};

inline std::string packed_to_planar_rgb_case_name(const PackedToPlanarRgbCase& test_case) {
  std::ostringstream stream;
  stream << test_case.operation << "_Width" << test_case.width_pixels << "_Height"
         << test_case.height << "_SrcPitch" << test_case.source_pitch << "_DstPitches"
         << test_case.destination_pitches[0] << '_' << test_case.destination_pitches[1] << '_'
         << test_case.destination_pitches[2] << '_' << test_case.destination_pitches[3]
         << "_PatternChannelAnchors_" << convert_rgb_variant_name(test_case.variant);
  return stream.str();
}

inline PackedToPlanarRgbCase make_packed_to_planar_rgb_case(
    std::string operation, std::size_t bytes_per_component, std::size_t source_components,
    bool target_has_alpha, std::size_t width_pixels, std::size_t height, std::size_t source_pitch,
    std::array<std::size_t, 4> destination_pitches, Variant<PackedToPlanarRgbFunction> variant,
    std::array<std::string, 4> expected_hashes) {
  PackedToPlanarRgbCase result{std::move(operation),
                               bytes_per_component,
                               source_components,
                               target_has_alpha,
                               width_pixels,
                               height,
                               source_pitch,
                               std::move(destination_pitches),
                               std::move(variant),
                               std::move(expected_hashes),
                               {}};
  result.name = packed_to_planar_rgb_case_name(result);
  return result;
}

inline void PrintTo(const PackedToPlanarRgbCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void make_packed_to_planar_rgb_reference(PlaneView<const T> source,
                                         std::array<PlaneView<T>, 4> destination,
                                         const PackedToPlanarRgbCase& test_case) {
  const auto opaque_alpha = std::numeric_limits<T>::max();
  for (std::size_t y = 0; y < test_case.height; ++y) {
    // Packed RGB storage is bottom-up while planar output is top-down.
    const auto* source_row = source.row(test_case.height - 1 - y);
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      const auto* source_pixel = source_row + x * test_case.source_components;
      destination[0].row(y)[x] = source_pixel[1];
      destination[1].row(y)[x] = source_pixel[0];
      destination[2].row(y)[x] = source_pixel[2];
      if (test_case.target_has_alpha) {
        destination[3].row(y)[x] =
            test_case.source_components == 4 ? source_pixel[3] : opaque_alpha;
      }
    }
  }
}

template <typename T>
void run_packed_to_planar_rgb_case(const PackedToPlanarRgbCase& test_case) {
  static_assert(std::is_integral_v<T>);
  const auto source_width = test_case.width_pixels * test_case.source_components;
  GuardedVideoBuffer<T> source(source_width, test_case.height, test_case.source_pitch, 64);
  GuardedVideoBuffer<T> expected_g(test_case.width_pixels, test_case.height,
                                   test_case.destination_pitches[0], 64);
  GuardedVideoBuffer<T> expected_b(test_case.width_pixels, test_case.height,
                                   test_case.destination_pitches[1], 64);
  GuardedVideoBuffer<T> expected_r(test_case.width_pixels, test_case.height,
                                   test_case.destination_pitches[2], 64);
  GuardedVideoBuffer<T> expected_a(test_case.width_pixels, test_case.height,
                                   test_case.destination_pitches[3], 64);
  GuardedVideoBuffer<T> actual_g(test_case.width_pixels, test_case.height,
                                 test_case.destination_pitches[0], 64);
  GuardedVideoBuffer<T> actual_b(test_case.width_pixels, test_case.height,
                                 test_case.destination_pitches[1], 64);
  GuardedVideoBuffer<T> actual_r(test_case.width_pixels, test_case.height,
                                 test_case.destination_pitches[2], 64);
  GuardedVideoBuffer<T> actual_a(test_case.width_pixels, test_case.height,
                                 test_case.destination_pitches[3], 64);

  fill_packed_rgb_input(source.view(), test_case.width_pixels, test_case.source_components);
  const auto source_snapshot = source.snapshot_active();
  const auto alpha_snapshot = actual_a.snapshot_active();
  make_packed_to_planar_rgb_reference(
      source.view().as_const(),
      {expected_g.view(), expected_b.view(), expected_r.view(), expected_a.view()}, test_case);

  BYTE* destination[4]{reinterpret_cast<BYTE*>(actual_g.view().data()),
                       reinterpret_cast<BYTE*>(actual_b.view().data()),
                       reinterpret_cast<BYTE*>(actual_r.view().data()),
                       reinterpret_cast<BYTE*>(actual_a.view().data())};
  int destination_pitches[4]{static_cast<int>(actual_g.view().pitch_bytes()),
                             static_cast<int>(actual_b.view().pitch_bytes()),
                             static_cast<int>(actual_r.view().pitch_bytes()),
                             static_cast<int>(actual_a.view().pitch_bytes())};
  const auto* source_bottom_row =
      reinterpret_cast<const BYTE*>(source.view().row(test_case.height - 1));
  test_case.variant.function(source_bottom_row, destination,
                             static_cast<int>(source.view().pitch_bytes()), destination_pitches,
                             static_cast<int>(test_case.width_pixels),
                             static_cast<int>(test_case.height));

  const std::array<PlaneView<const T>, 4> expected{
      expected_g.view().as_const(), expected_b.view().as_const(), expected_r.view().as_const(),
      expected_a.view().as_const()};
  const std::array<PlaneView<const T>, 4> actual{
      actual_g.view().as_const(), actual_b.view().as_const(), actual_r.view().as_const(),
      actual_a.view().as_const()};
  constexpr std::array<const char*, 4> kPlaneNames{"G", "B", "R", "A"};
  const auto plane_count = test_case.target_has_alpha ? 4U : 3U;
  for (std::size_t plane = 0; plane < plane_count; ++plane) {
    EXPECT_TRUE(compare_exact(expected[plane], actual[plane]))
        << test_case.name << " reference mismatch for variant " << test_case.variant.name
        << " plane " << kPlaneNames[plane];
    EXPECT_EQ(format_hash(hash_active(expected[plane])), test_case.expected_hashes[plane])
        << test_case.name << " stable output hash mismatch for plane " << kPlaneNames[plane];
  }
  if (!test_case.target_has_alpha) {
    EXPECT_TRUE(actual_a.active_matches(alpha_snapshot))
        << test_case.name << " modified the unused alpha plane";
  }

  EXPECT_TRUE(source.active_matches(source_snapshot))
      << test_case.name << " modified the source input";
  EXPECT_TRUE(source.memory_intact())
      << test_case.name << " source padding or guards were corrupted";
  EXPECT_TRUE(expected_g.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected_b.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected_r.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected_a.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual_g.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual_b.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual_r.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual_a.memory_intact()) << test_case.name;
}

}  // namespace avsut::test
