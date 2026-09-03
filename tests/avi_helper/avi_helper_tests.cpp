#include "core/AviHelper.h"

#include "support/comparators.h"
#include "support/cpu_features.h"
#include "support/avisynth_environment.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace avsut::test {
namespace {

using ToY416Function = void (*)(uint8_t*, int, const uint8_t*, int, const uint8_t*, const uint8_t*,
                                int, const uint8_t*, int, int, int);
using FromY416Function = void (*)(uint8_t*, int, uint8_t*, uint8_t*, int, uint8_t*, int,
                                  const uint8_t*, int, int, int);
using BgraToArgbBeFunction = void (*)(uint8_t*, int, const uint8_t*, int, int, int);

struct ToY416Case {
  bool has_alpha{};
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<ToY416Function> variant;
  std::string expected_hash;
  std::string name;
};

struct BgraToArgbBeCase {
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<BgraToArgbBeFunction> variant;
  std::string expected_hash;
  std::string name;
};

template <typename T>
struct PlaneSet {
  GuardedVideoBuffer<T> y;
  GuardedVideoBuffer<T> u;
  GuardedVideoBuffer<T> v;
  GuardedVideoBuffer<T> a;

  PlaneSet(std::size_t width, std::size_t height, std::size_t pitch)
      : y(width, height, pitch, 64),
        u(width, height, pitch, 64),
        v(width, height, pitch, 64),
        a(width, height, pitch, 64) {}
};

template <typename Function>
std::string variant_name(const Variant<Function>& variant) {
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

inline std::string to_y416_case_name(const ToY416Case& test_case) {
  std::ostringstream stream;
  stream << "ToY416_" << (test_case.has_alpha ? "SourceAlpha" : "OpaqueAlpha") << "_Width"
         << test_case.width_pixels << "_Height" << test_case.height << "_SrcPitch"
         << test_case.source_pitch << "_DstPitch" << test_case.destination_pitch
         << "_PatternChannelAnchors_" << variant_name(test_case.variant);
  return stream.str();
}

inline std::string bgra_to_argb_be_case_name(const BgraToArgbBeCase& test_case) {
  std::ostringstream stream;
  stream << "BgraToArgbBE_Width" << test_case.width_pixels << "_Height" << test_case.height
         << "_SrcPitch" << test_case.source_pitch << "_DstPitch" << test_case.destination_pitch
         << "_PatternByteAnchors_" << variant_name(test_case.variant);
  return stream.str();
}

inline ToY416Case make_to_y416_case(bool has_alpha, Variant<ToY416Function> variant,
                                    std::string expected_hash) {
  ToY416Case result{has_alpha, 7, 3, 64, 80, std::move(variant), std::move(expected_hash), {}};
  result.name = to_y416_case_name(result);
  return result;
}

inline BgraToArgbBeCase make_bgra_to_argb_be_case(Variant<BgraToArgbBeFunction> variant,
                                                  std::string expected_hash) {
  BgraToArgbBeCase result{5, 3, 64, 64, std::move(variant), std::move(expected_hash), {}};
  result.name = bgra_to_argb_be_case_name(result);
  return result;
}

inline void PrintTo(const ToY416Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const BgraToArgbBeCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_yuv444p16(PlaneView<T> y_plane, PlaneView<T> u_plane, PlaneView<T> v_plane,
                    PlaneView<T> alpha_plane) {
  static_assert(std::is_same_v<T, std::uint16_t>);
  constexpr std::array<std::uint16_t, 12> anchors{0,   1,    2,    3,     255,   256,
                                                  512, 1023, 4095, 32767, 65534, 65535};
  for (std::size_t y = 0; y < y_plane.height(); ++y) {
    for (std::size_t x = 0; x < y_plane.width(); ++x) {
      const auto sample = [&](std::size_t channel) {
        const auto anchor = anchors[(x * 3U + y * 5U + channel * 7U) % anchors.size()];
        return static_cast<std::uint16_t>(anchor + x * 37U + y * 101U + channel * 257U);
      };
      y_plane.row(y)[x] = sample(0);
      u_plane.row(y)[x] = sample(1);
      v_plane.row(y)[x] = sample(2);
      alpha_plane.row(y)[x] = sample(3);
    }
  }
}

inline void run_to_y416_case(const ToY416Case& test_case) {
  GuardedVideoBuffer<std::uint16_t> source_y(test_case.width_pixels, test_case.height,
                                             test_case.source_pitch, 64);
  GuardedVideoBuffer<std::uint16_t> source_u(test_case.width_pixels, test_case.height,
                                             test_case.source_pitch, 64);
  GuardedVideoBuffer<std::uint16_t> source_v(test_case.width_pixels, test_case.height,
                                             test_case.source_pitch, 64);
  GuardedVideoBuffer<std::uint16_t> source_a(test_case.width_pixels, test_case.height,
                                             test_case.source_pitch, 64);
  GuardedVideoBuffer<std::uint16_t> expected(test_case.width_pixels * 4, test_case.height,
                                             test_case.destination_pitch, 64);
  GuardedVideoBuffer<std::uint16_t> actual(test_case.width_pixels * 4, test_case.height,
                                           test_case.destination_pitch, 64);

  fill_yuv444p16(source_y.view(), source_u.view(), source_v.view(), source_a.view());
  const auto y_snapshot = source_y.snapshot_active();
  const auto u_snapshot = source_u.snapshot_active();
  const auto v_snapshot = source_v.snapshot_active();
  const auto a_snapshot = source_a.snapshot_active();

  const ToY416Function reference = test_case.has_alpha ? ToY416_c<true> : ToY416_c<false>;
  reference(reinterpret_cast<uint8_t*>(expected.view().data()),
            static_cast<int>(expected.view().pitch_bytes()),
            reinterpret_cast<const uint8_t*>(source_y.view().data()),
            static_cast<int>(source_y.view().pitch_bytes()),
            reinterpret_cast<const uint8_t*>(source_u.view().data()),
            reinterpret_cast<const uint8_t*>(source_v.view().data()),
            static_cast<int>(source_u.view().pitch_bytes()),
            reinterpret_cast<const uint8_t*>(source_a.view().data()),
            static_cast<int>(source_a.view().pitch_bytes()),
            static_cast<int>(test_case.width_pixels), static_cast<int>(test_case.height));

  test_case.variant.function(reinterpret_cast<uint8_t*>(actual.view().data()),
                             static_cast<int>(actual.view().pitch_bytes()),
                             reinterpret_cast<const uint8_t*>(source_y.view().data()),
                             static_cast<int>(source_y.view().pitch_bytes()),
                             reinterpret_cast<const uint8_t*>(source_u.view().data()),
                             reinterpret_cast<const uint8_t*>(source_v.view().data()),
                             static_cast<int>(source_u.view().pitch_bytes()),
                             reinterpret_cast<const uint8_t*>(source_a.view().data()),
                             static_cast<int>(source_a.view().pitch_bytes()),
                             static_cast<int>(test_case.width_pixels),
                             static_cast<int>(test_case.height));

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch";
  EXPECT_TRUE(source_y.active_matches(y_snapshot)) << test_case.name;
  EXPECT_TRUE(source_u.active_matches(u_snapshot)) << test_case.name;
  EXPECT_TRUE(source_v.active_matches(v_snapshot)) << test_case.name;
  EXPECT_TRUE(source_a.active_matches(a_snapshot)) << test_case.name;
  EXPECT_TRUE(source_y.memory_intact()) << test_case.name;
  EXPECT_TRUE(source_u.memory_intact()) << test_case.name;
  EXPECT_TRUE(source_v.memory_intact()) << test_case.name;
  EXPECT_TRUE(source_a.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.memory_intact()) << test_case.name;
}

template <typename T>
void fill_yuv444p10(PlaneView<T> y_plane, PlaneView<T> u_plane, PlaneView<T> v_plane,
                    PlaneView<T> alpha_plane) {
  static_assert(std::is_same_v<T, std::uint16_t>);
  constexpr std::array<std::uint16_t, 10> anchors{0, 1, 2, 3, 64, 128, 511, 512, 1022, 1023};
  for (std::size_t y = 0; y < y_plane.height(); ++y) {
    for (std::size_t x = 0; x < y_plane.width(); ++x) {
      const auto sample = [&](std::size_t channel) {
        const auto anchor = anchors[(x * 3U + y * 5U + channel * 7U) % anchors.size()];
        return static_cast<std::uint16_t>((anchor + x * 17U + y * 31U + channel * 43U) & 0x3ffU);
      };
      y_plane.row(y)[x] = sample(0);
      u_plane.row(y)[x] = sample(1);
      v_plane.row(y)[x] = sample(2);
      alpha_plane.row(y)[x] =
          static_cast<std::uint16_t>((x * 0x1111U + y * 0x2222U + 0x1357U) & 0xffffU);
    }
  }
}

inline std::string from_y416_case_name(bool has_alpha) {
  return std::string("FromY416_") + (has_alpha ? "SourceAlpha" : "IgnoreAlpha") +
         "_Width7_Height3_SrcPitch80_DstPitch64_PatternChannelAnchors";
}

inline void run_from_y416_case(bool has_alpha, const char* expected_y_hash,
                               const char* expected_u_hash, const char* expected_v_hash,
                               const char* expected_a_hash) {
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height = 3;
  const auto name = from_y416_case_name(has_alpha);
  GuardedVideoBuffer<std::uint16_t> source(width_pixels * 4, height, 80, 64);
  PlaneSet<std::uint16_t> expected(width_pixels, height, 64);
  PlaneSet<std::uint16_t> actual(width_pixels, height, 64);

  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      source.view().row(y)[x * 4 + 0] = static_cast<std::uint16_t>(x * 37U + y * 101U + 3U);
      source.view().row(y)[x * 4 + 1] = static_cast<std::uint16_t>(x * 41U + y * 107U + 7U);
      source.view().row(y)[x * 4 + 2] = static_cast<std::uint16_t>(x * 43U + y * 109U + 11U);
      source.view().row(y)[x * 4 + 3] = static_cast<std::uint16_t>(x * 47U + y * 113U + 13U);
      expected.y.view().row(y)[x] = source.view().row(y)[x * 4 + 1];
      expected.u.view().row(y)[x] = source.view().row(y)[x * 4 + 0];
      expected.v.view().row(y)[x] = source.view().row(y)[x * 4 + 2];
      expected.a.view().row(y)[x] = has_alpha ? source.view().row(y)[x * 4 + 3] : 0xA55AU;
      actual.a.view().row(y)[x] = 0xA55AU;
    }
  }
  const auto source_snapshot = source.snapshot_active();
  const auto alpha_snapshot = actual.a.snapshot_active();
  const FromY416Function function = has_alpha ? FromY416_c<true> : FromY416_c<false>;

  function(reinterpret_cast<uint8_t*>(actual.y.view().data()),
           static_cast<int>(actual.y.view().pitch_bytes()),
           reinterpret_cast<uint8_t*>(actual.u.view().data()),
           reinterpret_cast<uint8_t*>(actual.v.view().data()),
           static_cast<int>(actual.u.view().pitch_bytes()),
           reinterpret_cast<uint8_t*>(actual.a.view().data()),
           static_cast<int>(actual.a.view().pitch_bytes()),
           reinterpret_cast<const uint8_t*>(source.view().data()),
           static_cast<int>(source.view().pitch_bytes()), static_cast<int>(width_pixels),
           static_cast<int>(height));

  EXPECT_TRUE(compare_exact(expected.y.view().as_const(), actual.y.view().as_const())) << name;
  EXPECT_TRUE(compare_exact(expected.u.view().as_const(), actual.u.view().as_const())) << name;
  EXPECT_TRUE(compare_exact(expected.v.view().as_const(), actual.v.view().as_const())) << name;
  EXPECT_TRUE(compare_exact(expected.a.view().as_const(), actual.a.view().as_const())) << name;
  EXPECT_EQ(format_hash(hash_active(expected.y.view().as_const())), expected_y_hash) << name;
  EXPECT_EQ(format_hash(hash_active(expected.u.view().as_const())), expected_u_hash) << name;
  EXPECT_EQ(format_hash(hash_active(expected.v.view().as_const())), expected_v_hash) << name;
  EXPECT_EQ(format_hash(hash_active(expected.a.view().as_const())), expected_a_hash) << name;
  EXPECT_TRUE(source.active_matches(source_snapshot)) << name;
  if (!has_alpha) {
    EXPECT_TRUE(actual.a.active_matches(alpha_snapshot)) << name << " modified ignored alpha";
  }
  EXPECT_TRUE(source.memory_intact()) << name;
  EXPECT_TRUE(expected.y.memory_intact()) << name;
  EXPECT_TRUE(expected.u.memory_intact()) << name;
  EXPECT_TRUE(expected.v.memory_intact()) << name;
  EXPECT_TRUE(expected.a.memory_intact()) << name;
  EXPECT_TRUE(actual.y.memory_intact()) << name;
  EXPECT_TRUE(actual.u.memory_intact()) << name;
  EXPECT_TRUE(actual.v.memory_intact()) << name;
  EXPECT_TRUE(actual.a.memory_intact()) << name;
}

inline std::string to_y410_case_name(bool has_alpha) {
  return std::string("ToY410_") + (has_alpha ? "SourceAlpha" : "OpaqueAlpha") +
         "_Width7_Height3_SrcPitch64_DstPitch64_PatternChannelAnchors";
}

inline void run_to_y410_case(bool has_alpha, const char* expected_hash) {
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height = 3;
  const auto name = to_y410_case_name(has_alpha);
  PlaneSet<std::uint16_t> source(width_pixels, height, 64);
  GuardedVideoBuffer<std::uint32_t> expected(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint32_t> actual(width_pixels, height, 64, 64);
  fill_yuv444p10(source.y.view(), source.u.view(), source.v.view(), source.a.view());
  const auto y_snapshot = source.y.snapshot_active();
  const auto u_snapshot = source.u.snapshot_active();
  const auto v_snapshot = source.v.snapshot_active();
  const auto a_snapshot = source.a.snapshot_active();

  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto alpha = has_alpha ? (source.a.view().row(y)[x] >> 8) : 3U;
      expected.view().row(y)[x] = static_cast<std::uint32_t>(source.u.view().row(y)[x]) |
                                  (static_cast<std::uint32_t>(source.y.view().row(y)[x]) << 10) |
                                  (static_cast<std::uint32_t>(source.v.view().row(y)[x]) << 20) |
                                  (alpha << 30);
    }
  }
  const auto function = has_alpha ? ToY410_c<true> : ToY410_c<false>;
  function(reinterpret_cast<uint8_t*>(actual.view().data()),
           static_cast<int>(actual.view().pitch_bytes()),
           reinterpret_cast<const uint8_t*>(source.y.view().data()),
           static_cast<int>(source.y.view().pitch_bytes()),
           reinterpret_cast<const uint8_t*>(source.u.view().data()),
           reinterpret_cast<const uint8_t*>(source.v.view().data()),
           static_cast<int>(source.u.view().pitch_bytes()),
           reinterpret_cast<const uint8_t*>(source.a.view().data()),
           static_cast<int>(source.a.view().pitch_bytes()), static_cast<int>(width_pixels),
           static_cast<int>(height));

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const())) << name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), expected_hash) << name;
  EXPECT_TRUE(source.y.active_matches(y_snapshot)) << name;
  EXPECT_TRUE(source.u.active_matches(u_snapshot)) << name;
  EXPECT_TRUE(source.v.active_matches(v_snapshot)) << name;
  EXPECT_TRUE(source.a.active_matches(a_snapshot)) << name;
  EXPECT_TRUE(source.y.memory_intact()) << name;
  EXPECT_TRUE(source.u.memory_intact()) << name;
  EXPECT_TRUE(source.v.memory_intact()) << name;
  EXPECT_TRUE(source.a.memory_intact()) << name;
  EXPECT_TRUE(expected.memory_intact()) << name;
  EXPECT_TRUE(actual.memory_intact()) << name;
}

inline std::string from_y410_case_name(bool has_alpha) {
  return std::string("FromY410_") + (has_alpha ? "SourceAlpha" : "IgnoreAlpha") +
         "_Width7_Height3_SrcPitch64_DstPitch64_PatternBitAnchors";
}

inline void run_from_y410_case(bool has_alpha, const char* expected_y_hash,
                               const char* expected_u_hash, const char* expected_v_hash,
                               const char* expected_a_hash) {
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height = 3;
  const auto name = from_y410_case_name(has_alpha);
  GuardedVideoBuffer<std::uint32_t> source(width_pixels, height, 64, 64);
  PlaneSet<std::uint16_t> expected(width_pixels, height, 64);
  PlaneSet<std::uint16_t> actual(width_pixels, height, 64);
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto u = static_cast<std::uint32_t>((x * 53U + y * 71U + 1U) & 0x3ffU);
      const auto luma = static_cast<std::uint32_t>((x * 59U + y * 73U + 2U) & 0x3ffU);
      const auto v = static_cast<std::uint32_t>((x * 61U + y * 79U + 3U) & 0x3ffU);
      const auto alpha = static_cast<std::uint32_t>((x + y * 2U) & 0x3U);
      source.view().row(y)[x] = u | (luma << 10) | (v << 20) | (alpha << 30);
      expected.u.view().row(y)[x] = static_cast<std::uint16_t>(u);
      expected.y.view().row(y)[x] = static_cast<std::uint16_t>(luma);
      expected.v.view().row(y)[x] = static_cast<std::uint16_t>(v);
      expected.a.view().row(y)[x] = has_alpha ? (alpha == 3 ? 0x3ff : alpha << 8) : 0xA55A;
      actual.a.view().row(y)[x] = 0xA55A;
    }
  }
  const auto source_snapshot = source.snapshot_active();
  const auto alpha_snapshot = actual.a.snapshot_active();
  const auto function = has_alpha ? FromY410_c<true> : FromY410_c<false>;
  function(reinterpret_cast<uint8_t*>(actual.y.view().data()),
           static_cast<int>(actual.y.view().pitch_bytes()),
           reinterpret_cast<uint8_t*>(actual.u.view().data()),
           reinterpret_cast<uint8_t*>(actual.v.view().data()),
           static_cast<int>(actual.u.view().pitch_bytes()),
           reinterpret_cast<uint8_t*>(actual.a.view().data()),
           static_cast<int>(actual.a.view().pitch_bytes()),
           reinterpret_cast<const uint8_t*>(source.view().data()),
           static_cast<int>(source.view().pitch_bytes()), static_cast<int>(width_pixels),
           static_cast<int>(height));

  EXPECT_TRUE(compare_exact(expected.y.view().as_const(), actual.y.view().as_const())) << name;
  EXPECT_TRUE(compare_exact(expected.u.view().as_const(), actual.u.view().as_const())) << name;
  EXPECT_TRUE(compare_exact(expected.v.view().as_const(), actual.v.view().as_const())) << name;
  EXPECT_TRUE(compare_exact(expected.a.view().as_const(), actual.a.view().as_const())) << name;
  EXPECT_EQ(format_hash(hash_active(expected.y.view().as_const())), expected_y_hash) << name;
  EXPECT_EQ(format_hash(hash_active(expected.u.view().as_const())), expected_u_hash) << name;
  EXPECT_EQ(format_hash(hash_active(expected.v.view().as_const())), expected_v_hash) << name;
  EXPECT_EQ(format_hash(hash_active(expected.a.view().as_const())), expected_a_hash) << name;
  EXPECT_TRUE(source.active_matches(source_snapshot)) << name;
  if (!has_alpha) {
    EXPECT_TRUE(actual.a.active_matches(alpha_snapshot)) << name << " modified ignored alpha";
  }
  EXPECT_TRUE(source.memory_intact()) << name;
  EXPECT_TRUE(expected.y.memory_intact()) << name;
  EXPECT_TRUE(expected.u.memory_intact()) << name;
  EXPECT_TRUE(expected.v.memory_intact()) << name;
  EXPECT_TRUE(expected.a.memory_intact()) << name;
  EXPECT_TRUE(actual.y.memory_intact()) << name;
  EXPECT_TRUE(actual.u.memory_intact()) << name;
  EXPECT_TRUE(actual.v.memory_intact()) << name;
  EXPECT_TRUE(actual.a.memory_intact()) << name;
}

inline void fill_bgra_bytes(PlaneView<std::uint8_t> view, std::size_t width_pixels) {
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      for (std::size_t component = 0; component < 8; ++component) {
        view.row(y)[x * 8 + component] = static_cast<std::uint8_t>(
            3U + x * 29U + y * 47U + component * 61U + ((x + y + component) % 5U) * 17U);
      }
    }
  }
}

inline void run_bgra_to_argb_be_case(const BgraToArgbBeCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source(test_case.width_pixels * 8, test_case.height,
                                          test_case.source_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_pixels * 8, test_case.height,
                                            test_case.destination_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> actual(test_case.width_pixels * 8, test_case.height,
                                          test_case.destination_pitch, 64);
  fill_bgra_bytes(source.view(), test_case.width_pixels);
  const auto source_snapshot = source.snapshot_active();

  bgra_to_argbBE_c(reinterpret_cast<uint8_t*>(expected.view().data()),
                   static_cast<int>(expected.view().pitch_bytes()),
                   reinterpret_cast<const uint8_t*>(source.view().data()),
                   static_cast<int>(source.view().pitch_bytes()),
                   static_cast<int>(test_case.width_pixels), static_cast<int>(test_case.height));
  test_case.variant.function(reinterpret_cast<uint8_t*>(actual.view().data()),
                             static_cast<int>(actual.view().pitch_bytes()),
                             reinterpret_cast<const uint8_t*>(source.view().data()),
                             static_cast<int>(source.view().pitch_bytes()),
                             static_cast<int>(test_case.width_pixels),
                             static_cast<int>(test_case.height));

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch";
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name;
  EXPECT_TRUE(source.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.memory_intact()) << test_case.name;
}

inline std::uint16_t swap16(std::uint16_t value) {
  return static_cast<std::uint16_t>((value << 8) | (value >> 8));
}

inline std::uint32_t swap32(std::uint32_t value) {
  return ((value & 0x000000ffU) << 24) | ((value & 0x0000ff00U) << 8) |
         ((value & 0x00ff0000U) >> 8) | ((value & 0xff000000U) >> 24);
}

inline void run_from_packed_rgb10_case(bool r210, const char* expected_r_hash,
                                       const char* expected_g_hash, const char* expected_b_hash) {
  constexpr std::size_t width_pixels = 5;
  constexpr std::size_t height = 3;
  const auto name = std::string(r210 ? "FromR210" : "FromR10k") +
                    "_Width5_Height3_SrcPitch64_DstPitch64_PatternBitAnchors";
  GuardedVideoBuffer<std::uint32_t> source(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> expected_r(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> expected_g(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> expected_b(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> actual_r(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> actual_g(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> actual_b(width_pixels, height, 64, 64);

  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto r = static_cast<std::uint32_t>((x * 97U + y * 113U + 1U) & 0x3ffU);
      const auto g = static_cast<std::uint32_t>((x * 101U + y * 127U + 2U) & 0x3ffU);
      const auto b = static_cast<std::uint32_t>((x * 103U + y * 131U + 3U) & 0x3ffU);
      const auto packed = r210 ? ((r << 20) | (g << 10) | b) : ((r << 22) | (g << 12) | (b << 2));
      source.view().row(y)[x] = swap32(packed);
      expected_r.view().row(y)[x] = static_cast<std::uint16_t>(r);
      expected_g.view().row(y)[x] = static_cast<std::uint16_t>(g);
      expected_b.view().row(y)[x] = static_cast<std::uint16_t>(b);
    }
  }
  const auto source_snapshot = source.snapshot_active();

  if (r210) {
    From_r210_c(reinterpret_cast<uint8_t*>(actual_r.view().data()),
                reinterpret_cast<uint8_t*>(actual_g.view().data()),
                reinterpret_cast<uint8_t*>(actual_b.view().data()),
                static_cast<int>(actual_r.view().pitch_bytes()),
                reinterpret_cast<uint8_t*>(source.view().data()),
                static_cast<int>(source.view().pitch_bytes()), static_cast<int>(width_pixels),
                static_cast<int>(height));
  } else {
    From_R10k_c(reinterpret_cast<uint8_t*>(actual_r.view().data()),
                reinterpret_cast<uint8_t*>(actual_g.view().data()),
                reinterpret_cast<uint8_t*>(actual_b.view().data()),
                static_cast<int>(actual_r.view().pitch_bytes()),
                reinterpret_cast<uint8_t*>(source.view().data()),
                static_cast<int>(source.view().pitch_bytes()), static_cast<int>(width_pixels),
                static_cast<int>(height));
  }

  EXPECT_TRUE(compare_exact(expected_r.view().as_const(), actual_r.view().as_const())) << name;
  EXPECT_TRUE(compare_exact(expected_g.view().as_const(), actual_g.view().as_const())) << name;
  EXPECT_TRUE(compare_exact(expected_b.view().as_const(), actual_b.view().as_const())) << name;
  EXPECT_EQ(format_hash(hash_active(expected_r.view().as_const())), expected_r_hash) << name;
  EXPECT_EQ(format_hash(hash_active(expected_g.view().as_const())), expected_g_hash) << name;
  EXPECT_EQ(format_hash(hash_active(expected_b.view().as_const())), expected_b_hash) << name;
  EXPECT_TRUE(source.active_matches(source_snapshot)) << name;
  EXPECT_TRUE(source.memory_intact()) << name;
  EXPECT_TRUE(expected_r.memory_intact()) << name;
  EXPECT_TRUE(expected_g.memory_intact()) << name;
  EXPECT_TRUE(expected_b.memory_intact()) << name;
  EXPECT_TRUE(actual_r.memory_intact()) << name;
  EXPECT_TRUE(actual_g.memory_intact()) << name;
  EXPECT_TRUE(actual_b.memory_intact()) << name;
}

TEST(BgrToRgbBe, ReordersAndByteSwapsPacked16BitPixels) {
  constexpr std::size_t width_pixels = 5;
  constexpr std::size_t height = 3;
  GuardedVideoBuffer<std::uint16_t> source(width_pixels * 3, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> expected(width_pixels * 3, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> actual(width_pixels * 3, height, 64, 64);

  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width_pixels * 3; ++x) {
      source.view().row(y)[x] = static_cast<std::uint16_t>(0x1000U + x * 0x31U + y * 0x207U);
    }
    for (std::size_t x = 0; x < width_pixels; ++x) {
      expected.view().row(y)[x * 3 + 0] = swap16(source.view().row(y)[x * 3 + 2]);
      expected.view().row(y)[x * 3 + 1] = swap16(source.view().row(y)[x * 3 + 1]);
      expected.view().row(y)[x * 3 + 2] = swap16(source.view().row(y)[x * 3 + 0]);
    }
  }
  const auto source_snapshot = source.snapshot_active();

  bgr_to_rgbBE_c(reinterpret_cast<uint8_t*>(actual.view().data()),
                 static_cast<int>(actual.view().pitch_bytes()),
                 reinterpret_cast<const uint8_t*>(source.view().data()),
                 static_cast<int>(source.view().pitch_bytes()), static_cast<int>(width_pixels),
                 static_cast<int>(height));

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()));
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), "9f64ded7b9728cc6");
  EXPECT_TRUE(source.active_matches(source_snapshot));
  EXPECT_TRUE(source.memory_intact());
  EXPECT_TRUE(expected.memory_intact());
  EXPECT_TRUE(actual.memory_intact());
}

TEST(FromY416, ExtractsChannelsAndLeavesIgnoredAlphaUntouched) {
  run_from_y416_case(false, "e3d4e9352341576d", "9259e877473733cc", "8ac97661bb952cb1",
                     "3238983f1e799a61");
  run_from_y416_case(true, "e3d4e9352341576d", "9259e877473733cc", "8ac97661bb952cb1",
                     "b4482fe3776327cc");
}

TEST(ToY410, PacksTenBitChannelsAndAlphaModes) {
  run_to_y410_case(false, "ded819d861ea3b15");
  run_to_y410_case(true, "837e45f9bebba043");
}

TEST(FromY410, ExtractsTenBitChannelsAndLeavesIgnoredAlphaUntouched) {
  run_from_y410_case(false, "5cdec5e89160ae43", "7860e260704152d1", "6621c94fa3e5bf10",
                     "3238983f1e799a61");
  run_from_y410_case(true, "5cdec5e89160ae43", "7860e260704152d1", "6621c94fa3e5bf10",
                     "6031275cea930c5d");
}

TEST(FromR210, ExtractsBigEndianTenBitRgb) {
  run_from_packed_rgb10_case(true, "9950d5147e5f4240", "eb4956e869eba190", "01ccc44f33d1a66e");
}

TEST(FromR10k, ExtractsBigEndianTenBitRgb) {
  run_from_packed_rgb10_case(false, "9950d5147e5f4240", "eb4956e869eba190", "01ccc44f33d1a66e");
}

TEST(V308ToYuv444p8, SplitsPackedVYUSamples) {
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height = 3;
  GuardedVideoBuffer<std::uint8_t> source(width_pixels * 3, height, width_pixels * 3, 64);
  GuardedVideoBuffer<std::uint8_t> expected_y(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> expected_u(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> expected_v(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> actual_y(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> actual_u(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> actual_v(width_pixels, height, 64, 64);

  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto v = static_cast<std::uint8_t>(3U + x * 17U + y * 29U);
      const auto luma = static_cast<std::uint8_t>(7U + x * 19U + y * 31U);
      const auto u = static_cast<std::uint8_t>(11U + x * 23U + y * 37U);
      source.view().row(y)[x * 3 + 0] = v;
      source.view().row(y)[x * 3 + 1] = luma;
      source.view().row(y)[x * 3 + 2] = u;
      expected_y.view().row(y)[x] = luma;
      expected_u.view().row(y)[x] = u;
      expected_v.view().row(y)[x] = v;
    }
  }
  const auto source_snapshot = source.snapshot_active();
  v308_to_yuv444p8(reinterpret_cast<BYTE*>(actual_y.view().data()),
                   static_cast<int>(actual_y.view().pitch_bytes()),
                   reinterpret_cast<BYTE*>(actual_u.view().data()),
                   reinterpret_cast<BYTE*>(actual_v.view().data()),
                   static_cast<int>(actual_u.view().pitch_bytes()),
                   reinterpret_cast<const BYTE*>(source.view().data()),
                   static_cast<int>(width_pixels), static_cast<int>(height));

  EXPECT_TRUE(compare_exact(expected_y.view().as_const(), actual_y.view().as_const()));
  EXPECT_TRUE(compare_exact(expected_u.view().as_const(), actual_u.view().as_const()));
  EXPECT_TRUE(compare_exact(expected_v.view().as_const(), actual_v.view().as_const()));
  EXPECT_EQ(format_hash(hash_active(expected_y.view().as_const())), "c28a41c622e9a481");
  EXPECT_EQ(format_hash(hash_active(expected_u.view().as_const())), "556c33bb2701bf32");
  EXPECT_EQ(format_hash(hash_active(expected_v.view().as_const())), "d8f027b1a5917757");
  EXPECT_TRUE(source.active_matches(source_snapshot));
  EXPECT_TRUE(source.memory_intact());
  EXPECT_TRUE(expected_y.memory_intact());
  EXPECT_TRUE(expected_u.memory_intact());
  EXPECT_TRUE(expected_v.memory_intact());
  EXPECT_TRUE(actual_y.memory_intact());
  EXPECT_TRUE(actual_u.memory_intact());
  EXPECT_TRUE(actual_v.memory_intact());
}

TEST(V408ToYuva444p8, SplitsPackedVYUABytes) {
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height = 3;
  GuardedVideoBuffer<std::uint8_t> source(width_pixels * 4, height, width_pixels * 4, 64);
  GuardedVideoBuffer<std::uint8_t> expected_y(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> expected_u(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> expected_v(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> expected_a(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> actual_y(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> actual_u(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> actual_v(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint8_t> actual_a(width_pixels, height, 64, 64);

  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto u = static_cast<std::uint8_t>(5U + x * 13U + y * 17U);
      const auto luma = static_cast<std::uint8_t>(9U + x * 19U + y * 23U);
      const auto v = static_cast<std::uint8_t>(15U + x * 29U + y * 31U);
      const auto alpha = static_cast<std::uint8_t>(21U + x * 37U + y * 41U);
      auto* pixel = source.view().row(y) + x * 4;
      pixel[0] = u;
      pixel[1] = luma;
      pixel[2] = v;
      pixel[3] = alpha;
      expected_y.view().row(y)[x] = luma;
      expected_u.view().row(y)[x] = u;
      expected_v.view().row(y)[x] = v;
      expected_a.view().row(y)[x] = alpha;
    }
  }
  const auto source_snapshot = source.snapshot_active();
  v408_to_yuva444p8(reinterpret_cast<BYTE*>(actual_y.view().data()),
                    static_cast<int>(actual_y.view().pitch_bytes()),
                    reinterpret_cast<BYTE*>(actual_u.view().data()),
                    reinterpret_cast<BYTE*>(actual_v.view().data()),
                    reinterpret_cast<BYTE*>(actual_a.view().data()),
                    static_cast<int>(actual_u.view().pitch_bytes()),
                    static_cast<int>(actual_a.view().pitch_bytes()),
                    reinterpret_cast<const BYTE*>(source.view().data()),
                    static_cast<int>(width_pixels), static_cast<int>(height));

  EXPECT_TRUE(compare_exact(expected_y.view().as_const(), actual_y.view().as_const()));
  EXPECT_TRUE(compare_exact(expected_u.view().as_const(), actual_u.view().as_const()));
  EXPECT_TRUE(compare_exact(expected_v.view().as_const(), actual_v.view().as_const()));
  EXPECT_TRUE(compare_exact(expected_a.view().as_const(), actual_a.view().as_const()));
  EXPECT_EQ(format_hash(hash_active(expected_y.view().as_const())), "ce099906ad31faba");
  EXPECT_EQ(format_hash(hash_active(expected_u.view().as_const())), "17b8d7edd9fb4ceb");
  EXPECT_EQ(format_hash(hash_active(expected_v.view().as_const())), "24c4712b0bdc501e");
  EXPECT_EQ(format_hash(hash_active(expected_a.view().as_const())), "b89cdeb721f56803");
  EXPECT_TRUE(source.active_matches(source_snapshot));
  EXPECT_TRUE(source.memory_intact());
  EXPECT_TRUE(expected_y.memory_intact());
  EXPECT_TRUE(expected_u.memory_intact());
  EXPECT_TRUE(expected_v.memory_intact());
  EXPECT_TRUE(expected_a.memory_intact());
  EXPECT_TRUE(actual_y.memory_intact());
  EXPECT_TRUE(actual_u.memory_intact());
  EXPECT_TRUE(actual_v.memory_intact());
  EXPECT_TRUE(actual_a.memory_intact());
}

TEST(V410ToYuv444p10, SplitsPackedTenBitVYUSamples) {
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height = 3;
  GuardedVideoBuffer<std::uint32_t> source(width_pixels, height, width_pixels * 4, 64);
  GuardedVideoBuffer<std::uint16_t> expected_y(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> expected_u(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> expected_v(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> actual_y(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> actual_u(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> actual_v(width_pixels, height, 64, 64);

  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto u = static_cast<std::uint32_t>((x * 43U + y * 59U + 1U) & 0x3ffU);
      const auto luma = static_cast<std::uint32_t>((x * 47U + y * 61U + 2U) & 0x3ffU);
      const auto v = static_cast<std::uint32_t>((x * 53U + y * 67U + 3U) & 0x3ffU);
      source.view().row(y)[x] = (u << 2) | (luma << 12) | (v << 22);
      expected_y.view().row(y)[x] = static_cast<std::uint16_t>(luma);
      expected_u.view().row(y)[x] = static_cast<std::uint16_t>(u);
      expected_v.view().row(y)[x] = static_cast<std::uint16_t>(v);
    }
  }
  const auto source_snapshot = source.snapshot_active();
  v410_to_yuv444p10(reinterpret_cast<BYTE*>(actual_y.view().data()),
                    static_cast<int>(actual_y.view().pitch_bytes()),
                    reinterpret_cast<BYTE*>(actual_u.view().data()),
                    reinterpret_cast<BYTE*>(actual_v.view().data()),
                    static_cast<int>(actual_u.view().pitch_bytes()),
                    reinterpret_cast<const BYTE*>(source.view().data()),
                    static_cast<int>(width_pixels), static_cast<int>(height));

  EXPECT_TRUE(compare_exact(expected_y.view().as_const(), actual_y.view().as_const()));
  EXPECT_TRUE(compare_exact(expected_u.view().as_const(), actual_u.view().as_const()));
  EXPECT_TRUE(compare_exact(expected_v.view().as_const(), actual_v.view().as_const()));
  EXPECT_EQ(format_hash(hash_active(expected_y.view().as_const())), "1bdca42a4b0dd5e0");
  EXPECT_EQ(format_hash(hash_active(expected_u.view().as_const())), "e601fd689383acb6");
  EXPECT_EQ(format_hash(hash_active(expected_v.view().as_const())), "bfa0b87fd25b8cc0");
  EXPECT_TRUE(source.active_matches(source_snapshot));
  EXPECT_TRUE(source.memory_intact());
  EXPECT_TRUE(expected_y.memory_intact());
  EXPECT_TRUE(expected_u.memory_intact());
  EXPECT_TRUE(expected_v.memory_intact());
  EXPECT_TRUE(actual_y.memory_intact());
  EXPECT_TRUE(actual_u.memory_intact());
  EXPECT_TRUE(actual_v.memory_intact());
}

inline void fill_v210_source(PlaneView<std::uint16_t> y_plane, PlaneView<std::uint16_t> u_plane,
                             PlaneView<std::uint16_t> v_plane) {
  constexpr std::array<std::uint16_t, 10> anchors{0, 1, 2, 3, 31, 128, 511, 512, 1022, 1023};
  for (std::size_t y = 0; y < y_plane.height(); ++y) {
    for (std::size_t x = 0; x < y_plane.width(); ++x) {
      y_plane.row(y)[x] = static_cast<std::uint16_t>(
          (anchors[(x * 3U + y * 5U) % anchors.size()] + x * 17U + y * 19U) & 0x3ffU);
    }
    for (std::size_t x = 0; x < u_plane.width(); ++x) {
      u_plane.row(y)[x] = static_cast<std::uint16_t>(
          (anchors[(x * 7U + y * 11U + 1U) % anchors.size()] + x * 23U + y * 29U) & 0x3ffU);
      v_plane.row(y)[x] = static_cast<std::uint16_t>(
          (anchors[(x * 13U + y * 17U + 2U) % anchors.size()] + x * 31U + y * 37U) & 0x3ffU);
    }
  }
}

inline void pack_v210_reference_rows(PlaneView<const std::uint16_t> y_plane,
                                     PlaneView<const std::uint16_t> u_plane,
                                     PlaneView<const std::uint16_t> v_plane,
                                     PlaneView<std::uint32_t> packed) {
  const auto groups = packed.width() / 4;
  for (std::size_t y = 0; y < y_plane.height(); ++y) {
    for (std::size_t group = 0; group < groups; ++group) {
      const auto y_offset = group * 6;
      const auto uv_offset = group * 3;
      auto* output = packed.row(y) + group * 4;
      output[0] = u_plane.row(y)[uv_offset + 0] | (y_plane.row(y)[y_offset + 0] << 10) |
                  (v_plane.row(y)[uv_offset + 0] << 20);
      output[1] = y_plane.row(y)[y_offset + 1] | (u_plane.row(y)[uv_offset + 1] << 10) |
                  (y_plane.row(y)[y_offset + 2] << 20);
      output[2] = v_plane.row(y)[uv_offset + 1] | (y_plane.row(y)[y_offset + 3] << 10) |
                  (u_plane.row(y)[uv_offset + 2] << 20);
      output[3] = y_plane.row(y)[y_offset + 4] | (v_plane.row(y)[uv_offset + 2] << 10) |
                  (y_plane.row(y)[y_offset + 5] << 20);
    }
  }
}

TEST(Yuv422p10ToV210, PacksFullGroupAndTwoPixelTail) {
  constexpr std::size_t width_pixels = 8;
  constexpr std::size_t height = 3;
  constexpr std::size_t encoded_groups = (width_pixels + 5) / 6;
  constexpr std::size_t source_groups = (width_pixels + 10) / 6;
  constexpr std::size_t source_width = source_groups * 6;
  constexpr std::size_t source_chroma_width = source_width / 2;
  // The upstream packer emits one complete extra group for this two-pixel tail.
  constexpr std::size_t output_tail_end = source_groups * 16;
  GuardedVideoBuffer<std::uint16_t> source_y(source_width, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> source_u(source_chroma_width, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> source_v(source_chroma_width, height, 64, 64);
  GuardedVideoBuffer<std::uint32_t> expected(encoded_groups * 4, height, 128, 64);
  GuardedVideoBuffer<std::uint32_t> actual(encoded_groups * 4, height, 128, 64);
  fill_v210_source(source_y.view(), source_u.view(), source_v.view());
  const auto y_snapshot = source_y.snapshot_active();
  const auto u_snapshot = source_u.snapshot_active();
  const auto v_snapshot = source_v.snapshot_active();
  pack_v210_reference_rows(source_y.view().as_const(), source_u.view().as_const(),
                           source_v.view().as_const(), expected.view());

  yuv422p10_to_v210(reinterpret_cast<BYTE*>(actual.view().data()),
                    reinterpret_cast<const BYTE*>(source_y.view().data()),
                    static_cast<int>(source_y.view().pitch_bytes()),
                    reinterpret_cast<const BYTE*>(source_u.view().data()),
                    reinterpret_cast<const BYTE*>(source_v.view().data()),
                    static_cast<int>(source_u.view().pitch_bytes()), static_cast<int>(width_pixels),
                    static_cast<int>(height));

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()));
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), "b9d3f89892ac9625");
  EXPECT_TRUE(source_y.active_matches(y_snapshot));
  EXPECT_TRUE(source_u.active_matches(u_snapshot));
  EXPECT_TRUE(source_v.active_matches(v_snapshot));
  EXPECT_TRUE(source_y.memory_intact());
  EXPECT_TRUE(source_u.memory_intact());
  EXPECT_TRUE(source_v.memory_intact());
  EXPECT_TRUE(expected.memory_intact());
  EXPECT_TRUE(actual.guards_intact());
  const bool permitted_tail_write =
      !actual.padding_intact() && actual.padding_intact_from(output_tail_end);
  if (permitted_tail_write) {
    std::cout << "[INFO] Yuv422p10ToV210 permitted full-group tail write through byte "
              << output_tail_end << "\n";
  }
  EXPECT_TRUE(actual.padding_intact_from(output_tail_end));
}

TEST(V210ToYuv422p10, UnpacksFullGroupAndTwoPixelTail) {
  constexpr std::size_t width_pixels = 8;
  constexpr std::size_t height = 3;
  constexpr std::size_t encoded_groups = (width_pixels + 5) / 6;
  constexpr std::size_t padded_width = encoded_groups * 6;
  constexpr std::size_t chroma_width = width_pixels / 2;
  GuardedVideoBuffer<std::uint16_t> source_y(padded_width, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> source_u(padded_width / 2, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> source_v(padded_width / 2, height, 64, 64);
  GuardedVideoBuffer<std::uint32_t> source(encoded_groups * 4, height, 128, 64);
  GuardedVideoBuffer<std::uint16_t> expected_y(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> expected_u(chroma_width, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> expected_v(chroma_width, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> actual_y(width_pixels, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> actual_u(chroma_width, height, 64, 64);
  GuardedVideoBuffer<std::uint16_t> actual_v(chroma_width, height, 64, 64);
  fill_v210_source(source_y.view(), source_u.view(), source_v.view());
  pack_v210_reference_rows(source_y.view().as_const(), source_u.view().as_const(),
                           source_v.view().as_const(), source.view());
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      expected_y.view().row(y)[x] = source_y.view().row(y)[x];
    }
    for (std::size_t x = 0; x < chroma_width; ++x) {
      expected_u.view().row(y)[x] = source_u.view().row(y)[x];
      expected_v.view().row(y)[x] = source_v.view().row(y)[x];
    }
  }
  const auto source_snapshot = source.snapshot_active();
  v210_to_yuv422p10(reinterpret_cast<BYTE*>(actual_y.view().data()),
                    static_cast<int>(actual_y.view().pitch_bytes()),
                    reinterpret_cast<BYTE*>(actual_u.view().data()),
                    reinterpret_cast<BYTE*>(actual_v.view().data()),
                    static_cast<int>(actual_u.view().pitch_bytes()),
                    reinterpret_cast<const BYTE*>(source.view().data()),
                    static_cast<int>(width_pixels), static_cast<int>(height));

  EXPECT_TRUE(compare_exact(expected_y.view().as_const(), actual_y.view().as_const()));
  EXPECT_TRUE(compare_exact(expected_u.view().as_const(), actual_u.view().as_const()));
  EXPECT_TRUE(compare_exact(expected_v.view().as_const(), actual_v.view().as_const()));
  EXPECT_EQ(format_hash(hash_active(expected_y.view().as_const())), "79ce25c85acea312");
  EXPECT_EQ(format_hash(hash_active(expected_u.view().as_const())), "3ad98637e0f98b65");
  EXPECT_EQ(format_hash(hash_active(expected_v.view().as_const())), "eac89d5ce8709eb9");
  EXPECT_TRUE(source.active_matches(source_snapshot));
  EXPECT_TRUE(source.memory_intact());
  EXPECT_TRUE(expected_y.memory_intact());
  EXPECT_TRUE(expected_u.memory_intact());
  EXPECT_TRUE(expected_v.memory_intact());
  EXPECT_TRUE(actual_y.memory_intact());
  EXPECT_TRUE(actual_u.memory_intact());
  EXPECT_TRUE(actual_v.memory_intact());
}

struct Px10ConversionCase {
  bool semi_packed_p16{};
  std::string name;
  std::string packed_hash;
  std::string y_hash;
  std::string u_hash;
  std::string v_hash;
};

std::vector<Px10ConversionCase> px10_conversion_cases() {
  return {{false, "FormatP210", "1f6d2419e7f98ed3", "a2a31cf6c56d226c", "6d4f5bd45212d632",
           "27d4288bf328167a"},
          {true, "FormatP216", "a4c76d65c6f47d79", "8a123c262867f5d1", "4511aa36dbc3946e",
           "0a63986139e846ee"}};
}

inline void PrintTo(const Px10ConversionCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline std::uint16_t px10_source_value(std::size_t x, std::size_t y, std::size_t channel,
                                       bool semi_packed_p16) {
  constexpr std::array<std::uint32_t, 12> anchors{0,   1,   2,    3,    31,    128,
                                                  511, 512, 1022, 1023, 32767, 65535};
  const auto anchor = anchors[(x * 5U + y * 7U + channel * 11U) % anchors.size()];
  const auto value = anchor + x * 257U + y * 4099U + channel * 109U;
  return static_cast<std::uint16_t>(value & (semi_packed_p16 ? 0xffffU : 0x3ffU));
}

inline void fill_px10_source(PlaneView<std::uint16_t> y_plane, PlaneView<std::uint16_t> u_plane,
                             PlaneView<std::uint16_t> v_plane, bool semi_packed_p16) {
  for (std::size_t y = 0; y < y_plane.height(); ++y) {
    for (std::size_t x = 0; x < y_plane.width(); ++x) {
      y_plane.row(y)[x] = px10_source_value(x, y, 0, semi_packed_p16);
    }
    for (std::size_t x = 0; x < u_plane.width(); ++x) {
      u_plane.row(y)[x] = px10_source_value(x, y, 1, semi_packed_p16);
      v_plane.row(y)[x] = px10_source_value(x, y, 2, semi_packed_p16);
    }
  }
}

inline std::uint16_t px10_packed_value(std::uint16_t value, bool semi_packed_p16) {
  return semi_packed_p16 ? value : static_cast<std::uint16_t>(value << 6);
}

inline void make_px10_packed_reference(PlaneView<const std::uint16_t> y_plane,
                                       PlaneView<const std::uint16_t> u_plane,
                                       PlaneView<const std::uint16_t> v_plane,
                                       PlaneView<std::uint16_t> packed, bool semi_packed_p16) {
  for (std::size_t y = 0; y < y_plane.height(); ++y) {
    for (std::size_t x = 0; x < y_plane.width(); ++x) {
      packed.row(y)[x] = px10_packed_value(y_plane.row(y)[x], semi_packed_p16);
    }
  }
  for (std::size_t y = 0; y < u_plane.height(); ++y) {
    for (std::size_t x = 0; x < u_plane.width(); ++x) {
      packed.row(y + y_plane.height())[x * 2] =
          px10_packed_value(u_plane.row(y)[x], semi_packed_p16);
      packed.row(y + y_plane.height())[x * 2 + 1] =
          px10_packed_value(v_plane.row(y)[x], semi_packed_p16);
    }
  }
}

class Px10ConversionKernels : public ::testing::TestWithParam<Px10ConversionCase> {};

TEST_P(Px10ConversionKernels, PacksAndUnpacksPitched422Rows) {
  constexpr std::size_t width_pixels = 18;
  constexpr std::size_t height = 3;
  constexpr std::size_t chroma_width = width_pixels / 2;
  constexpr std::size_t pitch_bytes = 64;
  const auto& test_case = GetParam();

  GuardedVideoBuffer<std::uint16_t> source_y(width_pixels, height, pitch_bytes, 64);
  GuardedVideoBuffer<std::uint16_t> source_u(chroma_width, height, pitch_bytes, 64);
  GuardedVideoBuffer<std::uint16_t> source_v(chroma_width, height, pitch_bytes, 64);
  GuardedVideoBuffer<std::uint16_t> expected_packed(width_pixels, height * 2, pitch_bytes, 64);
  GuardedVideoBuffer<std::uint16_t> actual_packed(width_pixels, height * 2, pitch_bytes, 64);
  fill_px10_source(source_y.view(), source_u.view(), source_v.view(), test_case.semi_packed_p16);
  const auto y_snapshot = source_y.snapshot_active();
  const auto u_snapshot = source_u.snapshot_active();
  const auto v_snapshot = source_v.snapshot_active();
  make_px10_packed_reference(source_y.view().as_const(), source_u.view().as_const(),
                             source_v.view().as_const(), expected_packed.view(),
                             test_case.semi_packed_p16);

  AviSynthEnvironment environment;
  yuv42xp10_16_to_Px10_16(reinterpret_cast<BYTE*>(actual_packed.view().data()),
                          static_cast<int>(actual_packed.view().pitch_bytes()),
                          reinterpret_cast<const BYTE*>(source_y.view().data()),
                          static_cast<int>(source_y.view().pitch_bytes()),
                          reinterpret_cast<const BYTE*>(source_u.view().data()),
                          reinterpret_cast<const BYTE*>(source_v.view().data()),
                          static_cast<int>(source_u.view().pitch_bytes()),
                          static_cast<int>(width_pixels), static_cast<int>(height),
                          static_cast<int>(height), test_case.semi_packed_p16, environment.get());

  EXPECT_TRUE(compare_exact(expected_packed.view().as_const(), actual_packed.view().as_const()))
      << test_case.name << " packed output mismatch";
  EXPECT_EQ(format_hash(hash_active(expected_packed.view().as_const())), test_case.packed_hash)
      << test_case.name << " packed output hash mismatch";
  EXPECT_TRUE(source_y.active_matches(y_snapshot)) << test_case.name << " modified Y input";
  EXPECT_TRUE(source_u.active_matches(u_snapshot)) << test_case.name << " modified U input";
  EXPECT_TRUE(source_v.active_matches(v_snapshot)) << test_case.name << " modified V input";
  EXPECT_TRUE(source_y.memory_intact()) << test_case.name << " corrupted Y input memory";
  EXPECT_TRUE(source_u.memory_intact()) << test_case.name << " corrupted U input memory";
  EXPECT_TRUE(source_v.memory_intact()) << test_case.name << " corrupted V input memory";
  EXPECT_TRUE(expected_packed.memory_intact()) << test_case.name << " corrupted reference memory";
  EXPECT_TRUE(actual_packed.memory_intact()) << test_case.name << " corrupted packed memory";

  GuardedVideoBuffer<std::uint16_t> expected_y(width_pixels, height, pitch_bytes, 64);
  GuardedVideoBuffer<std::uint16_t> expected_u(chroma_width, height, pitch_bytes, 64);
  GuardedVideoBuffer<std::uint16_t> expected_v(chroma_width, height, pitch_bytes, 64);
  GuardedVideoBuffer<std::uint16_t> actual_y(width_pixels, height, pitch_bytes, 64);
  GuardedVideoBuffer<std::uint16_t> actual_u(chroma_width, height, pitch_bytes, 64);
  GuardedVideoBuffer<std::uint16_t> actual_v(chroma_width, height, pitch_bytes, 64);
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      expected_y.view().row(y)[x] = source_y.view().row(y)[x];
    }
    for (std::size_t x = 0; x < chroma_width; ++x) {
      expected_u.view().row(y)[x] = source_u.view().row(y)[x];
      expected_v.view().row(y)[x] = source_v.view().row(y)[x];
    }
  }
  const auto packed_snapshot = actual_packed.snapshot_active();
  Px10_16_to_yuv42xp10_16(reinterpret_cast<BYTE*>(actual_y.view().data()),
                          static_cast<int>(actual_y.view().pitch_bytes()),
                          reinterpret_cast<BYTE*>(actual_u.view().data()),
                          reinterpret_cast<BYTE*>(actual_v.view().data()),
                          static_cast<int>(actual_u.view().pitch_bytes()),
                          reinterpret_cast<const BYTE*>(actual_packed.view().data()),
                          static_cast<int>(actual_packed.view().pitch_bytes()),
                          static_cast<int>(width_pixels), static_cast<int>(height),
                          static_cast<int>(height), test_case.semi_packed_p16, environment.get());

  EXPECT_TRUE(compare_exact(expected_y.view().as_const(), actual_y.view().as_const()))
      << test_case.name << " unpacked Y mismatch";
  EXPECT_TRUE(compare_exact(expected_u.view().as_const(), actual_u.view().as_const()))
      << test_case.name << " unpacked U mismatch";
  EXPECT_TRUE(compare_exact(expected_v.view().as_const(), actual_v.view().as_const()))
      << test_case.name << " unpacked V mismatch";
  EXPECT_EQ(format_hash(hash_active(expected_y.view().as_const())), test_case.y_hash)
      << test_case.name << " unpacked Y hash mismatch";
  EXPECT_EQ(format_hash(hash_active(expected_u.view().as_const())), test_case.u_hash)
      << test_case.name << " unpacked U hash mismatch";
  EXPECT_EQ(format_hash(hash_active(expected_v.view().as_const())), test_case.v_hash)
      << test_case.name << " unpacked V hash mismatch";
  EXPECT_TRUE(actual_packed.active_matches(packed_snapshot))
      << test_case.name << " modified packed input";
  EXPECT_TRUE(actual_packed.memory_intact()) << test_case.name << " corrupted packed input memory";
  EXPECT_TRUE(expected_y.memory_intact()) << test_case.name << " corrupted Y reference memory";
  EXPECT_TRUE(expected_u.memory_intact()) << test_case.name << " corrupted U reference memory";
  EXPECT_TRUE(expected_v.memory_intact()) << test_case.name << " corrupted V reference memory";
  EXPECT_TRUE(actual_y.memory_intact()) << test_case.name << " corrupted Y output memory";
  EXPECT_TRUE(actual_u.memory_intact()) << test_case.name << " corrupted U output memory";
  EXPECT_TRUE(actual_v.memory_intact()) << test_case.name << " corrupted V output memory";
}

INSTANTIATE_TEST_SUITE_P(Packed422, Px10ConversionKernels,
                         ::testing::ValuesIn(px10_conversion_cases()),
                         [](const ::testing::TestParamInfo<Px10ConversionCase>& info) {
                           return info.param.name;
                         });

std::vector<ToY416Case> to_y416_cases() {
  const auto c_false = Variant<ToY416Function>{"c", ToY416_c<false>, IsaRequirement::Scalar};
  const auto c_true = Variant<ToY416Function>{"c", ToY416_c<true>, IsaRequirement::Scalar};
  const auto sse_false = Variant<ToY416Function>{"sse2", ToY416_sse2<false>, IsaRequirement::Sse2};
  const auto sse_true = Variant<ToY416Function>{"sse2", ToY416_sse2<true>, IsaRequirement::Sse2};
  return {make_to_y416_case(false, c_false, "c835b250e5c3f569"),
          make_to_y416_case(false, sse_false, "c835b250e5c3f569"),
          make_to_y416_case(true, c_true, "ad8e099a3bd0d8cf"),
          make_to_y416_case(true, sse_true, "ad8e099a3bd0d8cf")};
}

std::vector<BgraToArgbBeCase> bgra_to_argb_be_cases() {
  const auto hash = "f0a88e7431f4f42d";
  return {make_bgra_to_argb_be_case({"c", bgra_to_argbBE_c, IsaRequirement::Scalar}, hash),
          make_bgra_to_argb_be_case({"sse2", bgra_to_argbBE_sse2, IsaRequirement::Sse2}, hash),
          make_bgra_to_argb_be_case({"ssse3", bgra_to_argbBE_ssse3, IsaRequirement::Ssse3}, hash)};
}

class ToY416Kernels : public ::testing::TestWithParam<ToY416Case> {};

TEST_P(ToY416Kernels, MatchesScalarPackingAndPreservesInputs) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_to_y416_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, ToY416Kernels, ::testing::ValuesIn(to_y416_cases()),
                         [](const ::testing::TestParamInfo<ToY416Case>& info) {
                           return info.param.name;
                         });

class BgraToArgbBeKernels : public ::testing::TestWithParam<BgraToArgbBeCase> {};

TEST_P(BgraToArgbBeKernels, MatchesScalarByteSwapAndPreservesInputs) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_bgra_to_argb_be_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, BgraToArgbBeKernels, ::testing::ValuesIn(bgra_to_argb_be_cases()),
                         [](const ::testing::TestParamInfo<BgraToArgbBeCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
