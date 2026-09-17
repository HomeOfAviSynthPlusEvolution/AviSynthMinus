#pragma once

#include <avisynth.h>
#include <gtest/gtest.h>
#include <planes/kernel.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "support/comparators.h"
#include "support/deterministic_data.h"
#include "support/guarded_video_buffer.h"
#include "support/kernel_cpu_profiles.h"
#include "support/stable_hash.h"

namespace avsut::test {

struct Yuy2SwapCase {
  std::size_t width_bytes{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  KernelCpuProfile variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

struct Yuy2UvToYCase {
  std::string operation;
  bool packed_output{};
  std::size_t destination_width{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  int position{};
  KernelCpuProfile variant;
  std::string expected_hash;
  std::string name;
};

struct Yuy2ToUvCase {
  bool has_clip_y{};
  std::size_t destination_row_bytes{};
  std::size_t height{};
  std::size_t y_pitch{};
  std::size_t uv_pitch{};
  std::size_t destination_pitch{};
  KernelCpuProfile variant;
  std::string expected_hash;
  std::string name;
};

inline std::string planeswap_variant_name(const KernelCpuProfile& variant) {
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

inline std::string yuy2_case_name(std::size_t width_bytes, std::size_t height, std::size_t source_pitch,
                                  std::size_t destination_pitch, const KernelCpuProfile& variant,
                                  std::uint32_t seed) {
  std::ostringstream stream;
  stream << "Yuy2Swap_Width" << width_bytes << "_Height" << height << "_SrcPitch" << source_pitch
         << "_DstPitch" << destination_pitch;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternChannelRamp_" : "_PatternFixedRandom_") << planeswap_variant_name(variant);
  return stream.str();
}

inline std::string yuy2_uv_to_y_case_name(const Yuy2UvToYCase& test_case) {
  std::ostringstream stream;
  stream << test_case.operation << "_Width" << test_case.destination_width << "_Height" << test_case.height
         << "_SrcPitch" << test_case.source_pitch << "_DstPitch" << test_case.destination_pitch
         << "_ChromaOffset" << test_case.position << "_PatternChannelRamp_"
         << planeswap_variant_name(test_case.variant);
  return stream.str();
}

inline std::string yuy2_to_uv_case_name(const Yuy2ToUvCase& test_case) {
  std::ostringstream stream;
  stream << "Yuy2ToUv_" << (test_case.has_clip_y ? "WithY" : "NeutralY") << "_WidthBytes"
         << test_case.destination_row_bytes << "_Height" << test_case.height << "_YPitch" << test_case.y_pitch
         << "_UvPitch" << test_case.uv_pitch << "_DstPitch" << test_case.destination_pitch
         << "_PatternChannelRamp_" << planeswap_variant_name(test_case.variant);
  return stream.str();
}

inline Yuy2SwapCase make_yuy2_case(std::size_t width_bytes, std::size_t height, std::size_t source_pitch,
                                   std::size_t destination_pitch, KernelCpuProfile variant,
                                   std::string expected_hash, std::uint32_t seed = 0) {
  Yuy2SwapCase result{
      width_bytes, height, source_pitch, destination_pitch, std::move(variant), std::move(expected_hash),
      seed,        {}};
  result.name = yuy2_case_name(result.width_bytes, result.height, result.source_pitch,
                               result.destination_pitch, result.variant, result.seed);
  return result;
}

inline Yuy2UvToYCase make_yuy2_uv_to_y_case(std::string operation, bool packed_output,
                                            std::size_t destination_width, std::size_t height,
                                            std::size_t source_pitch, std::size_t destination_pitch,
                                            int position, KernelCpuProfile variant,
                                            std::string expected_hash) {
  Yuy2UvToYCase result{std::move(operation),
                       packed_output,
                       destination_width,
                       height,
                       source_pitch,
                       destination_pitch,
                       position,
                       std::move(variant),
                       std::move(expected_hash),
                       {}};
  result.name = yuy2_uv_to_y_case_name(result);
  return result;
}

inline Yuy2ToUvCase make_yuy2_to_uv_case(bool has_clip_y, std::size_t destination_row_bytes,
                                         std::size_t height, std::size_t y_pitch, std::size_t uv_pitch,
                                         std::size_t destination_pitch, KernelCpuProfile variant,
                                         std::string expected_hash) {
  Yuy2ToUvCase result{
      has_clip_y,         destination_row_bytes,    height, y_pitch, uv_pitch, destination_pitch,
      std::move(variant), std::move(expected_hash), {}};
  result.name = yuy2_to_uv_case_name(result);
  return result;
}

inline void PrintTo(const Yuy2SwapCase& test_case, std::ostream* stream) { *stream << test_case.name; }

inline void PrintTo(const Yuy2UvToYCase& test_case, std::ostream* stream) { *stream << test_case.name; }

inline void PrintTo(const Yuy2ToUvCase& test_case, std::ostream* stream) { *stream << test_case.name; }

inline void fill_yuy2_input(PlaneView<std::uint8_t> view) {
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); x += 4) {
      view.row(y)[x + 0] = static_cast<std::uint8_t>(17 + y * 23 + x * 5);
      view.row(y)[x + 1] = static_cast<std::uint8_t>(31 + y * 29 + x * 7);
      view.row(y)[x + 2] = static_cast<std::uint8_t>(43 + y * 31 + x * 11);
      view.row(y)[x + 3] = static_cast<std::uint8_t>(59 + y * 37 + x * 13);
    }
  }
}

inline void apply_yuy2_reference(PlaneView<const std::uint8_t> source, PlaneView<std::uint8_t> destination) {
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < source.width(); x += 4) {
      destination.row(y)[x + 0] = source.row(y)[x + 0];
      destination.row(y)[x + 1] = source.row(y)[x + 3];
      destination.row(y)[x + 2] = source.row(y)[x + 2];
      destination.row(y)[x + 3] = source.row(y)[x + 1];
    }
  }
}

inline void apply_yuy2_uv_to_y_reference(PlaneView<const std::uint8_t> source,
                                         PlaneView<std::uint8_t> destination, int position,
                                         bool preserve_neutral_chroma) {
  for (std::size_t y = 0; y < source.height(); ++y) {
    if (preserve_neutral_chroma) {
      for (std::size_t x = 0; x < destination.width(); x += 2) {
        const auto selected = source.row(y)[x * 2 + static_cast<std::size_t>(position)];
        destination.row(y)[x] = selected;
        destination.row(y)[x + 1] = 0x80;
      }
    } else {
      for (std::size_t x = 0; x < destination.width(); ++x) {
        const auto selected = source.row(y)[x * 4 + static_cast<std::size_t>(position)];
        destination.row(y)[x] = selected;
      }
    }
  }
}

inline void fill_yuy2_component_input(PlaneView<std::uint8_t> y_plane, PlaneView<std::uint8_t> u_plane,
                                      PlaneView<std::uint8_t> v_plane) {
  for (std::size_t y = 0; y < y_plane.height(); ++y) {
    for (std::size_t x = 0; x < y_plane.width(); ++x) {
      y_plane.row(y)[x] = static_cast<std::uint8_t>(13 + x * 17 + y * 29);
    }
    for (std::size_t x = 0; x < u_plane.width(); ++x) {
      u_plane.row(y)[x] = static_cast<std::uint8_t>(47 + x * 23 + y * 31);
      v_plane.row(y)[x] = static_cast<std::uint8_t>(89 + x * 37 + y * 41);
    }
  }
}

inline void apply_yuy2_to_uv_reference(const Yuy2ToUvCase& test_case, PlaneView<const std::uint8_t> y_plane,
                                       PlaneView<const std::uint8_t> u_plane,
                                       PlaneView<const std::uint8_t> v_plane,
                                       PlaneView<std::uint8_t> destination) {
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < destination.width(); x += 4) {
      destination.row(y)[x + 0] = test_case.has_clip_y ? y_plane.row(y)[x] : 0x7e;
      destination.row(y)[x + 1] = u_plane.row(y)[x / 2];
      destination.row(y)[x + 2] = test_case.has_clip_y ? y_plane.row(y)[x + 2] : 0x7e;
      destination.row(y)[x + 3] = v_plane.row(y)[x / 2];
    }
  }
}

inline void run_yuy2_case(const Yuy2SwapCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source(test_case.width_bytes, test_case.height, test_case.source_pitch,
                                          32);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_bytes, test_case.height,
                                            test_case.destination_pitch, 32);
  GuardedVideoBuffer<std::uint8_t> actual(test_case.width_bytes, test_case.height,
                                          test_case.destination_pitch, 32);

  if (test_case.seed == 0) {
    fill_yuy2_input(source.view());
  } else {
    fill_random(source.view(), test_case.seed);
  }
  const auto source_snapshot = source.snapshot_active();
  apply_yuy2_reference(source.view().as_const(), expected.view());

  ASSERT_EQ(aif_planes_swap(source.view().data(), static_cast<int>(test_case.source_pitch),
                            actual.view().data(), static_cast<int>(test_case.destination_pitch),
                            static_cast<int>(test_case.width_bytes) / 2, static_cast<int>(test_case.height),
                            test_case.variant.cpu),
            0);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name << " modified the source input";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " source padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact()) << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(actual.memory_intact()) << test_case.name << " output padding or guards were corrupted";
}

inline void run_yuy2_uv_to_y_case(const Yuy2UvToYCase& test_case) {
  const auto source_row_bytes =
      test_case.packed_output ? test_case.destination_width * 2 : test_case.destination_width * 4;
  GuardedVideoBuffer<std::uint8_t> source(source_row_bytes, test_case.height, test_case.source_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.destination_width, test_case.height,
                                            test_case.destination_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual(test_case.destination_width, test_case.height,
                                          test_case.destination_pitch, 64);

  fill_yuy2_input(source.view());
  const auto source_snapshot = source.snapshot_active();
  apply_yuy2_uv_to_y_reference(source.view().as_const(), expected.view(), test_case.position,
                               test_case.packed_output);

  ASSERT_EQ(
      aif_planes_extract_uv(source.view().data(), static_cast<int>(test_case.source_pitch),
                            actual.view().data(), static_cast<int>(test_case.destination_pitch),
                            static_cast<int>(test_case.destination_width / (test_case.packed_output ? 2 : 1)),
                            static_cast<int>(test_case.height), test_case.position == 3,
                            test_case.packed_output, test_case.variant.cpu),
      0);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name << " modified the source input";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " source padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact()) << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(actual.memory_intact()) << test_case.name << " output padding or guards were corrupted";
}

inline void run_yuy2_to_uv_case(const Yuy2ToUvCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source_y(test_case.destination_row_bytes, test_case.height,
                                            test_case.y_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> source_u(test_case.destination_row_bytes / 2, test_case.height,
                                            test_case.uv_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> source_v(test_case.destination_row_bytes / 2, test_case.height,
                                            test_case.uv_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.destination_row_bytes, test_case.height,
                                            test_case.destination_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual(test_case.destination_row_bytes, test_case.height,
                                          test_case.destination_pitch, 64);

  fill_yuy2_component_input(source_y.view(), source_u.view(), source_v.view());
  const auto y_snapshot = source_y.snapshot_active();
  const auto u_snapshot = source_u.snapshot_active();
  const auto v_snapshot = source_v.snapshot_active();
  apply_yuy2_to_uv_reference(test_case, source_y.view().as_const(), source_u.view().as_const(),
                             source_v.view().as_const(), expected.view());

  ASSERT_EQ(aif_planes_assemble(test_case.has_clip_y ? source_y.view().data() : nullptr,
                                static_cast<int>(test_case.y_pitch), source_u.view().data(),
                                static_cast<int>(test_case.uv_pitch), source_v.view().data(),
                                static_cast<int>(test_case.uv_pitch), actual.view().data(),
                                static_cast<int>(test_case.destination_pitch),
                                static_cast<int>(test_case.destination_row_bytes / 2),
                                static_cast<int>(test_case.height), test_case.variant.cpu),
            0);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  }
  EXPECT_TRUE(source_y.active_matches(y_snapshot)) << test_case.name << " modified the Y source input";
  EXPECT_TRUE(source_u.active_matches(u_snapshot)) << test_case.name << " modified the U source input";
  EXPECT_TRUE(source_v.active_matches(v_snapshot)) << test_case.name << " modified the V source input";
  EXPECT_TRUE(source_y.memory_intact()) << test_case.name << " Y source padding or guards were corrupted";
  EXPECT_TRUE(source_u.memory_intact()) << test_case.name << " U source padding or guards were corrupted";
  EXPECT_TRUE(source_v.memory_intact()) << test_case.name << " V source padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact()) << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(actual.memory_intact()) << test_case.name << " output padding or guards were corrupted";
}

}  // namespace avsut::test
