#pragma once

#include "convert/intel/convert_bits_avx2.h"
#include "convert/intel/convert_bits_sse.h"

#include "support/comparators.h"
#include "support/cpu_features.h"
#include "support/deterministic_data.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
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

using ConvertBitsIntegerFunc = void (*)(const BYTE*, BYTE*, int, int, int, int, int, int, int);

enum class IntegerStorage { UInt8ToUInt8, UInt8ToUInt16, UInt16ToUInt8, UInt16ToUInt16 };

struct ConvertBitsIntegerCase {
  IntegerStorage storage{};
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
  std::size_t output_padding_protected_from{};
};

inline void PrintTo(const ConvertBitsIntegerCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline const char* integer_storage_name(IntegerStorage storage) {
  switch (storage) {
    case IntegerStorage::UInt8ToUInt8:
      return "UInt8ToUInt8";
    case IntegerStorage::UInt8ToUInt16:
      return "UInt8ToUInt16";
    case IntegerStorage::UInt16ToUInt8:
      return "UInt16ToUInt8";
    case IntegerStorage::UInt16ToUInt16:
      return "UInt16ToUInt16";
  }
  return "Unknown";
}

inline std::string integer_variant_name(const std::string& name) {
  std::string result = "Variant";
  bool capitalize = true;
  for (const char character : name) {
    if (character == '-' || character == '_') {
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

inline ConvertBitsIntegerCase make_convert_bits_integer_case(
    IntegerStorage storage, bool chroma, bool source_full, bool target_full, int source_bit_depth,
    int target_bit_depth, std::size_t width, std::size_t height, std::size_t source_pitch,
    std::size_t target_pitch, Variant<ConvertBitsIntegerFunc> variant, std::string expected_hash,
    std::uint32_t seed = 0, std::size_t output_padding_protected_from = 0) {
  ConvertBitsIntegerCase result{
      storage, chroma, source_full,  target_full,  source_bit_depth,   target_bit_depth,
      width,   height, source_pitch, target_pitch, std::move(variant), std::move(expected_hash),
      seed, {}, output_padding_protected_from};
  std::ostringstream stream;
  stream << (chroma ? "Chroma" : "Luma") << "_Src" << source_bit_depth
         << (source_full ? "Full" : "Limited") << "_Dst" << target_bit_depth
         << (target_full ? "Full" : "Limited") << "_Storage" << integer_storage_name(storage)
         << "_Width" << width << "_Height" << height << "_SrcPitch" << source_pitch << "_DstPitch"
         << target_pitch;
  if (result.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << result.seed;
  }
  stream << "_DitherNone_"
         << (result.seed == 0 ? "PatternBoundaryValues_" : "PatternFixedRandom_")
         << integer_variant_name(result.variant.name);
  result.name = stream.str();
  return result;
}

struct IntegerRange {
  double offset{};
  double span{};
};

inline std::uint32_t bit_depth_max(int bit_depth) {
  if (bit_depth < 8 || bit_depth > 16) {
    throw std::invalid_argument("integer bit depth must be in 8..16");
  }
  return (std::uint32_t{1} << bit_depth) - 1;
}

inline IntegerRange integer_range(bool chroma, bool full, int bit_depth) {
  const double maximum = static_cast<double>(bit_depth_max(bit_depth));
  if (chroma) {
    return {static_cast<double>(std::uint32_t{1} << (bit_depth - 1)),
            full ? maximum / 2.0 : static_cast<double>(112U << (bit_depth - 8))};
  }
  return {full ? 0.0 : static_cast<double>(16U << (bit_depth - 8)),
          full ? maximum : static_cast<double>(219U << (bit_depth - 8))};
}

template <typename T>
void fill_convert_bits_integer_source(PlaneView<T> source,
                                      const ConvertBitsIntegerCase& test_case) {
  static_assert(std::is_integral_v<T>);
  const auto maximum = bit_depth_max(test_case.source_bit_depth);
  if (test_case.seed != 0) {
    XorShift32 generator(test_case.seed);
    for (std::size_t y = 0; y < source.height(); ++y) {
      for (std::size_t x = 0; x < source.width(); ++x) {
        source.row(y)[x] = static_cast<T>(generator.next() & maximum);
      }
    }
    return;
  }
  const auto range =
      integer_range(test_case.chroma, test_case.source_full, test_case.source_bit_depth);
  const auto lower =
      static_cast<std::uint32_t>(std::max(0.0, std::ceil(range.offset - range.span)));
  const auto upper = static_cast<std::uint32_t>(
      std::min(static_cast<double>(maximum), std::floor(range.offset + range.span)));
  const auto offset = static_cast<std::uint32_t>(range.offset);
  const std::array<std::uint32_t, 16> anchors{0,
                                              1,
                                              lower,
                                              lower < maximum ? lower + 1 : lower,
                                              offset > 0 ? offset - 1 : 0,
                                              offset,
                                              offset < maximum ? offset + 1 : offset,
                                              maximum / 4,
                                              maximum / 2,
                                              (maximum * 3) / 4,
                                              upper > 0 ? upper - 1 : 0,
                                              upper,
                                              upper < maximum ? upper + 1 : upper,
                                              maximum > 1 ? maximum - 1 : maximum,
                                              maximum,
                                              maximum / 3};
  const auto anchor_offset = static_cast<std::size_t>(test_case.seed % anchors.size());
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < source.width(); ++x) {
      source.row(y)[x] = static_cast<T>(anchors[(x + 5 * y + anchor_offset) % anchors.size()]);
    }
  }
}

inline std::uint32_t convert_bits_integer_reference(std::uint32_t source,
                                                    const ConvertBitsIntegerCase& test_case) {
  const auto target_maximum = bit_depth_max(test_case.target_bit_depth);
  if (!test_case.source_full && !test_case.target_full) {
    if (test_case.target_bit_depth > test_case.source_bit_depth) {
      return source << (test_case.target_bit_depth - test_case.source_bit_depth);
    }
    const int shift = test_case.source_bit_depth - test_case.target_bit_depth;
    const std::uint32_t round = std::uint32_t{1} << (shift - 1);
    const auto source_maximum = bit_depth_max(test_case.source_bit_depth);
    return std::min(source + round, source_maximum) >> shift;
  }

  const auto source_range =
      integer_range(test_case.chroma, test_case.source_full, test_case.source_bit_depth);
  const auto target_range =
      integer_range(test_case.chroma, test_case.target_full, test_case.target_bit_depth);
  const double mapped = (static_cast<double>(source) - source_range.offset) *
                            (target_range.span / source_range.span) +
                        target_range.offset;
  const auto rounded = static_cast<std::int64_t>(std::floor(mapped + 0.5));
  return static_cast<std::uint32_t>(
      std::clamp<std::int64_t>(rounded, 0, static_cast<std::int64_t>(target_maximum)));
}

template <typename Source, typename Target>
void run_convert_bits_integer_case_typed(const ConvertBitsIntegerCase& test_case) {
  GuardedVideoBuffer<Source> source(test_case.width, test_case.height, test_case.source_pitch, 64);
  GuardedVideoBuffer<Target> expected(test_case.width, test_case.height, test_case.target_pitch,
                                      64);
  GuardedVideoBuffer<Target> actual(test_case.width, test_case.height, test_case.target_pitch, 64);
  fill_convert_bits_integer_source(source.view(), test_case);
  const auto source_snapshot = source.snapshot_active();
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width; ++x) {
      expected.view().row(y)[x] =
          static_cast<Target>(convert_bits_integer_reference(source.view().row(y)[x], test_case));
    }
  }

  test_case.variant.function(
      reinterpret_cast<const BYTE*>(source.view().data()),
      reinterpret_cast<BYTE*>(actual.view().data()),
      static_cast<int>(test_case.width * sizeof(Source)), static_cast<int>(test_case.height),
      static_cast<int>(test_case.source_pitch), static_cast<int>(test_case.target_pitch),
      test_case.source_bit_depth, test_case.target_bit_depth, test_case.target_bit_depth);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name;
  const auto actual_hash = format_hash(hash_active(actual.view().as_const()));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name << " modified the source";
  EXPECT_TRUE(source.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.guards_intact()) << test_case.name << " output guards";
  const auto active_row_bytes = test_case.width * sizeof(Target);
  const auto protected_from = test_case.output_padding_protected_from == 0
                                  ? active_row_bytes
                                  : test_case.output_padding_protected_from;
  const bool permitted_tail_write =
      protected_from > active_row_bytes && !actual.padding_intact() &&
      actual.padding_intact_from(protected_from);
  if (permitted_tail_write) {
    std::cout << "[INFO] " << test_case.name
              << " modified output padding within the permitted SIMD tail"
              << " (active_row_bytes=" << active_row_bytes
              << ", protected_from_byte=" << protected_from << ")\n";
  }
  EXPECT_TRUE(actual.padding_intact_from(protected_from))
      << test_case.name << " modified output padding after the permitted SIMD tail boundary "
      << protected_from;
}

inline void run_convert_bits_integer_case(const ConvertBitsIntegerCase& test_case) {
  switch (test_case.storage) {
    case IntegerStorage::UInt8ToUInt8:
      run_convert_bits_integer_case_typed<std::uint8_t, std::uint8_t>(test_case);
      break;
    case IntegerStorage::UInt8ToUInt16:
      run_convert_bits_integer_case_typed<std::uint8_t, std::uint16_t>(test_case);
      break;
    case IntegerStorage::UInt16ToUInt8:
      run_convert_bits_integer_case_typed<std::uint16_t, std::uint8_t>(test_case);
      break;
    case IntegerStorage::UInt16ToUInt16:
      run_convert_bits_integer_case_typed<std::uint16_t, std::uint16_t>(test_case);
      break;
  }
}

template <typename Source, typename Target, bool Chroma, bool SourceFull, bool TargetFull>
void add_convert_bits_integer_variants(std::vector<ConvertBitsIntegerCase>& cases,
                                       IntegerStorage storage, int source_bit_depth,
                                       int target_bit_depth, std::size_t source_pitch,
                                       std::size_t target_pitch, const char* expected_hash,
                                       std::uint32_t seed = 0, std::size_t width = 32,
                                       std::size_t height = 5,
                                       std::size_t output_padding_protected_from = 0) {
  cases.push_back(make_convert_bits_integer_case(
      storage, Chroma, SourceFull, TargetFull, source_bit_depth, target_bit_depth, width, height,
      source_pitch, target_pitch,
      Variant<ConvertBitsIntegerFunc>{
          "sse41", convert_uint_sse41<Source, Target, Chroma, SourceFull, TargetFull>,
          IsaRequirement::Sse41},
      expected_hash, seed, output_padding_protected_from));
  cases.push_back(make_convert_bits_integer_case(
      storage, Chroma, SourceFull, TargetFull, source_bit_depth, target_bit_depth, width, height,
      source_pitch, target_pitch,
      Variant<ConvertBitsIntegerFunc>{
          "avx2", convert_uint_avx2<Source, Target, Chroma, SourceFull, TargetFull>,
          IsaRequirement::Avx2},
      expected_hash, seed, output_padding_protected_from));
}

}  // namespace avsut::test
