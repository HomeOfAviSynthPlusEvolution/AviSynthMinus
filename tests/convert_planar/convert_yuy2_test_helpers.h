#pragma once

#include "convert/intel/convert_planar_sse.h"

#include "support/comparators.h"
#include "support/cpu_features.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace avsut::test {

struct Yuy2ConversionCase {
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t packed_pitch{};
  std::size_t y_pitch{};
  std::size_t uv_pitch{};
  std::string y_hash;
  std::string u_hash;
  std::string v_hash;
  std::string packed_hash;
  std::string name;
};

inline void PrintTo(const Yuy2ConversionCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline Yuy2ConversionCase make_yuy2_conversion_case(
    const char* operation, std::size_t width_pixels, std::size_t height, std::size_t packed_pitch,
    std::size_t y_pitch, std::size_t uv_pitch, std::string y_hash = {}, std::string u_hash = {},
    std::string v_hash = {}, std::string packed_hash = {}) {
  Yuy2ConversionCase result{width_pixels,
                            height,
                            packed_pitch,
                            y_pitch,
                            uv_pitch,
                            std::move(y_hash),
                            std::move(u_hash),
                            std::move(v_hash),
                            std::move(packed_hash),
                            {}};
  std::ostringstream stream;
  stream << operation << "_Width" << width_pixels << "_Height" << height << "_PackedPitch"
         << packed_pitch << "_YPitch" << y_pitch << "_UvPitch" << uv_pitch
         << "_PatternChannelRamp_VariantSse2";
  result.name = stream.str();
  return result;
}

inline void fill_packed_yuy2(PlaneView<std::uint8_t> packed, std::size_t width_pixels) {
  for (std::size_t y = 0; y < packed.height(); ++y) {
    for (std::size_t pair = 0; pair < width_pixels / 2; ++pair) {
      auto* pixel = packed.row(y) + pair * 4;
      pixel[0] = static_cast<std::uint8_t>(7 + pair * 11 + y * 17);
      pixel[1] = static_cast<std::uint8_t>(31 + pair * 13 + y * 19);
      pixel[2] = static_cast<std::uint8_t>(53 + pair * 23 + y * 29);
      pixel[3] = static_cast<std::uint8_t>(79 + pair * 31 + y * 37);
    }
  }
}

inline void unpack_yuy2_reference(PlaneView<const std::uint8_t> packed,
                                  PlaneView<std::uint8_t> y_plane, PlaneView<std::uint8_t> u_plane,
                                  PlaneView<std::uint8_t> v_plane, std::size_t width_pixels) {
  for (std::size_t y = 0; y < packed.height(); ++y) {
    for (std::size_t pair = 0; pair < width_pixels / 2; ++pair) {
      const auto* pixel = packed.row(y) + pair * 4;
      y_plane.row(y)[pair * 2] = pixel[0];
      u_plane.row(y)[pair] = pixel[1];
      y_plane.row(y)[pair * 2 + 1] = pixel[2];
      v_plane.row(y)[pair] = pixel[3];
    }
  }
}

inline void fill_planar_yv16(PlaneView<std::uint8_t> y_plane, PlaneView<std::uint8_t> u_plane,
                             PlaneView<std::uint8_t> v_plane) {
  for (std::size_t y = 0; y < y_plane.height(); ++y) {
    for (std::size_t x = 0; x < y_plane.width(); ++x) {
      y_plane.row(y)[x] = static_cast<std::uint8_t>(5 + x * 17 + y * 23);
    }
    for (std::size_t x = 0; x < u_plane.width(); ++x) {
      u_plane.row(y)[x] = static_cast<std::uint8_t>(41 + x * 19 + y * 29);
      v_plane.row(y)[x] = static_cast<std::uint8_t>(83 + x * 31 + y * 37);
    }
  }
}

inline void pack_yuy2_reference(PlaneView<const std::uint8_t> y_plane,
                                PlaneView<const std::uint8_t> u_plane,
                                PlaneView<const std::uint8_t> v_plane,
                                PlaneView<std::uint8_t> packed) {
  for (std::size_t y = 0; y < packed.height(); ++y) {
    for (std::size_t pair = 0; pair < u_plane.width(); ++pair) {
      auto* pixel = packed.row(y) + pair * 4;
      pixel[0] = y_plane.row(y)[pair * 2];
      pixel[1] = u_plane.row(y)[pair];
      pixel[2] = y_plane.row(y)[pair * 2 + 1];
      pixel[3] = v_plane.row(y)[pair];
    }
  }
}

inline void run_yuy2_to_y8_case(const Yuy2ConversionCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source(test_case.width_pixels * 2, test_case.height,
                                          test_case.packed_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_pixels, test_case.height,
                                            test_case.y_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual(test_case.width_pixels, test_case.height,
                                          test_case.y_pitch, 64);
  fill_packed_yuy2(source.view(), test_case.width_pixels);
  const auto source_snapshot = source.snapshot_active();
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      expected.view().row(y)[x] = source.view().row(y)[x * 2];
    }
  }
  convert_yuy2_to_y8_sse2(reinterpret_cast<const BYTE*>(source.view().data()),
                          reinterpret_cast<BYTE*>(actual.view().data()), test_case.packed_pitch,
                          test_case.y_pitch, test_case.width_pixels, test_case.height);
  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name;
  EXPECT_EQ(format_hash(hash_active(actual.view().as_const())), test_case.y_hash) << test_case.name;
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name;
  EXPECT_TRUE(source.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.memory_intact()) << test_case.name;
}

inline void run_yuy2_to_yv16_case(const Yuy2ConversionCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source(test_case.width_pixels * 2, test_case.height,
                                          test_case.packed_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected_y(test_case.width_pixels, test_case.height,
                                              test_case.y_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected_u(test_case.width_pixels / 2, test_case.height,
                                              test_case.uv_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected_v(test_case.width_pixels / 2, test_case.height,
                                              test_case.uv_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual_y(test_case.width_pixels, test_case.height,
                                            test_case.y_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual_u(test_case.width_pixels / 2, test_case.height,
                                            test_case.uv_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual_v(test_case.width_pixels / 2, test_case.height,
                                            test_case.uv_pitch, 64);
  fill_packed_yuy2(source.view(), test_case.width_pixels);
  const auto source_snapshot = source.snapshot_active();
  unpack_yuy2_reference(source.view().as_const(), expected_y.view(), expected_u.view(),
                        expected_v.view(), test_case.width_pixels);
  convert_yuy2_to_yv16_sse2(reinterpret_cast<const BYTE*>(source.view().data()),
                            reinterpret_cast<BYTE*>(actual_y.view().data()),
                            reinterpret_cast<BYTE*>(actual_u.view().data()),
                            reinterpret_cast<BYTE*>(actual_v.view().data()), test_case.packed_pitch,
                            test_case.y_pitch, test_case.uv_pitch, test_case.width_pixels,
                            test_case.height);
  EXPECT_TRUE(compare_exact(expected_y.view().as_const(), actual_y.view().as_const()))
      << test_case.name << " Y plane";
  EXPECT_TRUE(compare_exact(expected_u.view().as_const(), actual_u.view().as_const()))
      << test_case.name << " U plane";
  EXPECT_TRUE(compare_exact(expected_v.view().as_const(), actual_v.view().as_const()))
      << test_case.name << " V plane";
  EXPECT_EQ(format_hash(hash_active(actual_y.view().as_const())), test_case.y_hash);
  EXPECT_EQ(format_hash(hash_active(actual_u.view().as_const())), test_case.u_hash);
  EXPECT_EQ(format_hash(hash_active(actual_v.view().as_const())), test_case.v_hash);
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name;
  EXPECT_TRUE(source.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected_y.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected_u.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected_v.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual_y.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual_u.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual_v.memory_intact()) << test_case.name;
}

inline void run_yv16_to_yuy2_case(const Yuy2ConversionCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source_y(test_case.width_pixels, test_case.height,
                                            test_case.y_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> source_u(test_case.width_pixels / 2, test_case.height,
                                            test_case.uv_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> source_v(test_case.width_pixels / 2, test_case.height,
                                            test_case.uv_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_pixels * 2, test_case.height,
                                            test_case.packed_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual(test_case.width_pixels * 2, test_case.height,
                                          test_case.packed_pitch, 64);
  fill_planar_yv16(source_y.view(), source_u.view(), source_v.view());
  const auto y_snapshot = source_y.snapshot_active();
  const auto u_snapshot = source_u.snapshot_active();
  const auto v_snapshot = source_v.snapshot_active();
  pack_yuy2_reference(source_y.view().as_const(), source_u.view().as_const(),
                      source_v.view().as_const(), expected.view());
  convert_yv16_to_yuy2_sse2(reinterpret_cast<const BYTE*>(source_y.view().data()),
                            reinterpret_cast<const BYTE*>(source_u.view().data()),
                            reinterpret_cast<const BYTE*>(source_v.view().data()),
                            reinterpret_cast<BYTE*>(actual.view().data()), test_case.y_pitch,
                            test_case.uv_pitch, test_case.packed_pitch, test_case.width_pixels,
                            test_case.height);
  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name;
  EXPECT_EQ(format_hash(hash_active(actual.view().as_const())), test_case.packed_hash)
      << test_case.name;
  EXPECT_TRUE(source_y.active_matches(y_snapshot)) << test_case.name;
  EXPECT_TRUE(source_u.active_matches(u_snapshot)) << test_case.name;
  EXPECT_TRUE(source_v.active_matches(v_snapshot)) << test_case.name;
  EXPECT_TRUE(source_y.memory_intact()) << test_case.name;
  EXPECT_TRUE(source_u.memory_intact()) << test_case.name;
  EXPECT_TRUE(source_v.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.memory_intact()) << test_case.name;
}

}  // namespace avsut::test
