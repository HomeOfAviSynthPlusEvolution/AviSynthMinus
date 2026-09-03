#pragma once

#include "convert_bits_integer_test_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace avsut::test {

enum class FloatConversion { FloatToUInt8, FloatToUInt16, UInt8ToFloat, UInt16ToFloat };

struct ConvertBitsFloatCase {
  FloatConversion conversion{};
  bool chroma{};
  bool source_full{};
  bool target_full{};
  int source_bit_depth{};
  int target_bit_depth{};
  std::size_t width{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t target_pitch{};
  Variant<ConvertBitsIntegerFunc> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline void PrintTo(const ConvertBitsFloatCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline const char* float_conversion_name(FloatConversion conversion) {
  switch (conversion) {
    case FloatConversion::FloatToUInt8:
      return "FloatToUInt8";
    case FloatConversion::FloatToUInt16:
      return "FloatToUInt16";
    case FloatConversion::UInt8ToFloat:
      return "UInt8ToFloat";
    case FloatConversion::UInt16ToFloat:
      return "UInt16ToFloat";
  }
  return "Unknown";
}

inline ConvertBitsFloatCase make_convert_bits_float_case(
    FloatConversion conversion, bool chroma, bool source_full, bool target_full,
    int source_bit_depth, int target_bit_depth, std::size_t width, std::size_t height,
    std::size_t source_pitch, std::size_t target_pitch, Variant<ConvertBitsIntegerFunc> variant,
    std::string expected_hash = {}, std::uint32_t seed = 0) {
  ConvertBitsFloatCase result{
      conversion, chroma, source_full,  target_full,  source_bit_depth,   target_bit_depth,
      width,      height, source_pitch, target_pitch, std::move(variant), std::move(expected_hash),
      seed, {}};
  std::ostringstream stream;
  stream << (chroma ? "Chroma" : "Luma") << "_" << float_conversion_name(conversion) << "_Src"
         << source_bit_depth << (source_full ? "Full" : "Limited") << "_Dst" << target_bit_depth
         << (target_full ? "Full" : "Limited") << "_Width" << width << "_Height" << height
         << "_SrcPitch" << source_pitch << "_DstPitch" << target_pitch;
  if (result.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << result.seed;
  }
  stream << "_PatternFiniteAnchors_" << integer_variant_name(result.variant.name);
  result.name = stream.str();
  return result;
}

struct NumericRange {
  float offset{};
  float span{};
};

inline NumericRange numeric_range(bool chroma, bool full, int bit_depth) {
  if (bit_depth == 32) {
    if (chroma) {
      return {0.0F, full ? 0.5F : 112.0F / 255.0F};
    }
    return {full ? 0.0F : 16.0F / 255.0F, full ? 1.0F : 219.0F / 255.0F};
  }
  const auto range = integer_range(chroma, full, bit_depth);
  return {static_cast<float>(range.offset), static_cast<float>(range.span)};
}

inline void fill_float_source(PlaneView<float> source, bool chroma, std::uint32_t seed = 0) {
  constexpr std::array<float, 16> luma_anchors{
      -0.25F,          0.0F, 1.0F / 255.0F, 15.0F / 255.0F,  16.0F / 255.0F,  17.0F / 255.0F,
      0.25F,           0.5F, 0.75F,         234.0F / 255.0F, 235.0F / 255.0F, 236.0F / 255.0F,
      254.0F / 255.0F, 1.0F, 1.25F,         0.125F};
  constexpr std::array<float, 16> chroma_anchors{
      -0.75F,        -0.5F,           -112.0F / 255.0F, -0.25F, -1.0F / 255.0F, 0.0F,
      1.0F / 255.0F, 0.25F,           112.0F / 255.0F,  0.5F,   0.75F,          -0.125F,
      0.125F,        -64.0F / 255.0F, 64.0F / 255.0F,   0.375F};
  const auto& anchors = chroma ? chroma_anchors : luma_anchors;
  const auto anchor_offset = static_cast<std::size_t>(seed % anchors.size());
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < source.width(); ++x) {
      source.row(y)[x] = anchors[(x + 3 * y + anchor_offset) % anchors.size()];
    }
  }
}

inline std::uint32_t float_to_integer_reference(float source,
                                                const ConvertBitsFloatCase& test_case) {
  const auto source_range =
      numeric_range(test_case.chroma, test_case.source_full, test_case.source_bit_depth);
  const auto target_range =
      numeric_range(test_case.chroma, test_case.target_full, test_case.target_bit_depth);
  const float mapped = (source - source_range.offset) * (target_range.span / source_range.span) +
                       target_range.offset + 0.5F;
  const float target_maximum = static_cast<float>(bit_depth_max(test_case.target_bit_depth));
  return static_cast<std::uint32_t>(std::clamp(mapped, 0.0F, target_maximum));
}

inline float integer_to_float_reference(std::uint32_t source,
                                        const ConvertBitsFloatCase& test_case) {
  const auto source_range =
      numeric_range(test_case.chroma, test_case.source_full, test_case.source_bit_depth);
  const auto target_range =
      numeric_range(test_case.chroma, test_case.target_full, test_case.target_bit_depth);
  return static_cast<float>(
      (static_cast<double>(source) - static_cast<double>(source_range.offset)) *
          (static_cast<double>(target_range.span) / static_cast<double>(source_range.span)) +
      static_cast<double>(target_range.offset));
}

inline std::uint64_t convert_bits_ulp_distance(float lhs, float rhs) noexcept {
  if (lhs == rhs) {
    return 0;
  }
  std::uint32_t lhs_bits{};
  std::uint32_t rhs_bits{};
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  constexpr std::uint32_t sign_bit = 0x80000000U;
  if ((lhs_bits & sign_bit) != (rhs_bits & sign_bit)) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  lhs_bits &= ~sign_bit;
  rhs_bits &= ~sign_bit;
  return lhs_bits >= rhs_bits ? lhs_bits - rhs_bits : rhs_bits - lhs_bits;
}

inline ::testing::AssertionResult compare_convert_bits_float(PlaneView<const float> expected,
                                                             PlaneView<const float> actual,
                                                             std::uint64_t maximum_ulps = 4,
                                                             float absolute_floor = 1.0e-4F) {
  if (expected.width() != actual.width() || expected.height() != actual.height()) {
    return ::testing::AssertionFailure() << "dimension mismatch";
  }
  for (std::size_t y = 0; y < expected.height(); ++y) {
    for (std::size_t x = 0; x < expected.width(); ++x) {
      const float lhs = expected.row(y)[x];
      const float rhs = actual.row(y)[x];
      if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
        return ::testing::AssertionFailure() << "row=" << y << " col=" << x << " expected=" << lhs
                                             << " actual=" << rhs << " non-finite output";
      }
      if (lhs == rhs) {
        continue;
      }
      const float absolute_error = std::abs(lhs - rhs);
      const auto ulps = convert_bits_ulp_distance(lhs, rhs);
      if (absolute_error > absolute_floor && ulps > maximum_ulps) {
        return ::testing::AssertionFailure()
               << "row=" << y << " col=" << x << " expected=" << lhs << " actual=" << rhs
               << " absolute_error=" << absolute_error << " ulps=" << ulps
               << " allowed_ulps=" << maximum_ulps << " absolute_floor=" << absolute_floor;
      }
    }
  }
  return ::testing::AssertionSuccess();
}

template <typename Target>
void run_float_to_integer_case(const ConvertBitsFloatCase& test_case) {
  GuardedVideoBuffer<float> source(test_case.width, test_case.height, test_case.source_pitch, 64);
  GuardedVideoBuffer<Target> expected(test_case.width, test_case.height, test_case.target_pitch,
                                      64);
  GuardedVideoBuffer<Target> actual(test_case.width, test_case.height, test_case.target_pitch, 64);
  fill_float_source(source.view(), test_case.chroma, test_case.seed);
  const auto source_snapshot = source.snapshot_active();
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width; ++x) {
      expected.view().row(y)[x] =
          static_cast<Target>(float_to_integer_reference(source.view().row(y)[x], test_case));
    }
  }
  test_case.variant.function(
      reinterpret_cast<const BYTE*>(source.view().data()),
      reinterpret_cast<BYTE*>(actual.view().data()),
      static_cast<int>(test_case.width * sizeof(float)), static_cast<int>(test_case.height),
      static_cast<int>(test_case.source_pitch), static_cast<int>(test_case.target_pitch),
      test_case.source_bit_depth, test_case.target_bit_depth, test_case.target_bit_depth);
  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name;
  EXPECT_EQ(format_hash(hash_active(actual.view().as_const())), test_case.expected_hash)
      << test_case.name;
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name;
  EXPECT_TRUE(source.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.guards_intact()) << test_case.name << " output guards";
  constexpr std::size_t kSimdTailBytes = sizeof(Target) == 1 ? 32 : 64;
  const auto active_row_bytes = test_case.width * sizeof(Target);
  const auto allowed_end = std::min(
      test_case.target_pitch,
      ((active_row_bytes + kSimdTailBytes - 1) / kSimdTailBytes) * kSimdTailBytes);
  const bool permitted_tail_write =
      !actual.padding_intact() && actual.padding_intact_from(allowed_end);
  if (permitted_tail_write) {
    std::cout << "[INFO] " << test_case.name
              << " modified output padding within the permitted " << kSimdTailBytes
              << "-byte SIMD tail"
              << " (active_row_bytes=" << active_row_bytes << ", protected_from_byte="
              << allowed_end << ")\n";
  }
  EXPECT_TRUE(actual.padding_intact_from(allowed_end))
      << test_case.name << " output padding after permitted " << kSimdTailBytes
      << "-byte SIMD tail boundary " << allowed_end;
}

template <typename Source>
void run_integer_to_float_case(const ConvertBitsFloatCase& test_case) {
  GuardedVideoBuffer<Source> source(test_case.width, test_case.height, test_case.source_pitch, 64);
  GuardedVideoBuffer<float> expected(test_case.width, test_case.height, test_case.target_pitch, 64);
  GuardedVideoBuffer<float> actual(test_case.width, test_case.height, test_case.target_pitch, 64);
  ConvertBitsIntegerCase input_case{std::is_same_v<Source, std::uint8_t>
                                        ? IntegerStorage::UInt8ToUInt8
                                        : IntegerStorage::UInt16ToUInt16,
                                    test_case.chroma,
                                    test_case.source_full,
                                    test_case.target_full,
                                    test_case.source_bit_depth,
                                    test_case.source_bit_depth,
                                    test_case.width,
                                    test_case.height,
                                    test_case.source_pitch,
                                    test_case.source_pitch,
                                    {},
                                    {},
                                    test_case.seed,
                                    {}};
  fill_convert_bits_integer_source(source.view(), input_case);
  const auto source_snapshot = source.snapshot_active();
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width; ++x) {
      expected.view().row(y)[x] = integer_to_float_reference(source.view().row(y)[x], test_case);
    }
  }
  test_case.variant.function(
      reinterpret_cast<const BYTE*>(source.view().data()),
      reinterpret_cast<BYTE*>(actual.view().data()),
      static_cast<int>(test_case.width * sizeof(Source)), static_cast<int>(test_case.height),
      static_cast<int>(test_case.source_pitch), static_cast<int>(test_case.target_pitch),
      test_case.source_bit_depth, test_case.target_bit_depth, test_case.target_bit_depth);
  EXPECT_TRUE(compare_convert_bits_float(expected.view().as_const(), actual.view().as_const()))
      << test_case.name;
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name;
  EXPECT_TRUE(source.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.guards_intact()) << test_case.name << " output guards";
  constexpr std::size_t kSimdTailBytes = 64;
  const auto active_row_bytes = test_case.width * sizeof(float);
  const auto allowed_end = std::min(
      test_case.target_pitch,
      ((active_row_bytes + kSimdTailBytes - 1) / kSimdTailBytes) * kSimdTailBytes);
  const bool permitted_tail_write =
      !actual.padding_intact() && actual.padding_intact_from(allowed_end);
  if (permitted_tail_write) {
    std::cout << "[INFO] " << test_case.name
              << " modified output padding within the permitted 64-byte SIMD tail"
              << " (active_row_bytes=" << active_row_bytes << ", protected_from_byte="
              << allowed_end << ")\n";
  }
  EXPECT_TRUE(actual.padding_intact_from(allowed_end))
      << test_case.name << " output padding after permitted SIMD tail boundary " << allowed_end;
}

inline void run_convert_bits_float_case(const ConvertBitsFloatCase& test_case) {
  switch (test_case.conversion) {
    case FloatConversion::FloatToUInt8:
      run_float_to_integer_case<std::uint8_t>(test_case);
      break;
    case FloatConversion::FloatToUInt16:
      run_float_to_integer_case<std::uint16_t>(test_case);
      break;
    case FloatConversion::UInt8ToFloat:
      run_integer_to_float_case<std::uint8_t>(test_case);
      break;
    case FloatConversion::UInt16ToFloat:
      run_integer_to_float_case<std::uint16_t>(test_case);
      break;
  }
}

template <typename Target, bool Chroma, bool SourceFull, bool TargetFull>
void add_float_to_integer_variants(std::vector<ConvertBitsFloatCase>& cases,
                                   FloatConversion conversion, int target_bit_depth,
                                   const char* expected_hash) {
  constexpr std::size_t width = 16;
  constexpr std::size_t height = 5;
  constexpr std::size_t source_pitch = 96;
  constexpr std::size_t target_pitch = 64;
  cases.push_back(make_convert_bits_float_case(
      conversion, Chroma, SourceFull, TargetFull, 32, target_bit_depth, width, height, source_pitch,
      target_pitch,
      Variant<ConvertBitsIntegerFunc>{
          "sse41", convert_32_to_uintN_sse41<Target, Chroma, SourceFull, TargetFull>,
          IsaRequirement::Sse41},
      expected_hash));
  cases.push_back(make_convert_bits_float_case(
      conversion, Chroma, SourceFull, TargetFull, 32, target_bit_depth, width, height, source_pitch,
      target_pitch,
      Variant<ConvertBitsIntegerFunc>{
          "avx2-fma", convert_32_to_uintN_avx2<Target, Chroma, SourceFull, TargetFull>,
          IsaRequirement::Avx2Fma},
      expected_hash));
}

}  // namespace avsut::test
