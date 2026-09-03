#pragma once

#include "convert_bits_integer_test_helpers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace avsut::test {

struct ConvertBitsDitherCase {
  IntegerStorage storage{};
  bool chroma{};
  bool source_full{};
  bool target_full{};
  int source_bit_depth{};
  int target_bit_depth{};
  int dither_bit_depth{};
  std::size_t width{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t target_pitch{};
  Variant<ConvertBitsIntegerFunc> variant;
  std::string expected_hash;
  std::string name;
};

inline void PrintTo(const ConvertBitsDitherCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline ConvertBitsDitherCase make_convert_bits_dither_case(
    IntegerStorage storage, bool chroma, bool source_full, bool target_full, int source_bit_depth,
    int target_bit_depth, int dither_bit_depth, std::size_t width, std::size_t height,
    std::size_t source_pitch, std::size_t target_pitch, Variant<ConvertBitsIntegerFunc> variant,
    std::string expected_hash) {
  ConvertBitsDitherCase result{storage,
                               chroma,
                               source_full,
                               target_full,
                               source_bit_depth,
                               target_bit_depth,
                               dither_bit_depth,
                               width,
                               height,
                               source_pitch,
                               target_pitch,
                               std::move(variant),
                               std::move(expected_hash),
                               {}};
  std::ostringstream stream;
  stream << (chroma ? "Chroma" : "Luma") << "_Src" << source_bit_depth
         << (source_full ? "Full" : "Limited") << "_Dst" << target_bit_depth
         << (target_full ? "Full" : "Limited") << "_DitherOrderedTo" << dither_bit_depth
         << "_Storage" << integer_storage_name(storage) << "_Width" << width << "_Height" << height
         << "_SrcPitch" << source_pitch << "_DstPitch" << target_pitch << "_PatternBoundaryValues_"
         << integer_variant_name(result.variant.name);
  result.name = stream.str();
  return result;
}

inline std::uint32_t bayer_value(std::size_t x, std::size_t y, int order) {
  std::uint32_t value = 0;
  for (int bit = 0; bit < order; ++bit) {
    const auto x_bit = static_cast<std::uint32_t>((x >> bit) & 1U);
    const auto y_bit = static_cast<std::uint32_t>((y >> bit) & 1U);
    const bool transposed_base = order == 4;
    const std::uint32_t quadrant =
        transposed_base ? (y_bit == 0 ? (x_bit == 0 ? 0U : 3U) : (x_bit == 0 ? 2U : 1U))
                        : (y_bit == 0 ? (x_bit == 0 ? 0U : 2U) : (x_bit == 0 ? 3U : 1U));
    value = value * 4U + quadrant;
  }
  return value;
}

inline std::uint32_t remap_dither_source(std::uint32_t source,
                                         const ConvertBitsDitherCase& test_case) {
  if (test_case.source_full == test_case.target_full) {
    return source;
  }
  const auto source_range =
      integer_range(test_case.chroma, test_case.source_full, test_case.source_bit_depth);
  const auto target_range =
      integer_range(test_case.chroma, test_case.target_full, test_case.source_bit_depth);
  const float source_offset = static_cast<float>(source_range.offset);
  const float target_offset = static_cast<float>(target_range.offset);
  const float factor =
      static_cast<float>(target_range.span) / static_cast<float>(source_range.span);
  const int rounded = static_cast<int>((static_cast<float>(source) - source_offset) * factor +
                                       target_offset + 0.5F);
  return static_cast<std::uint32_t>(
      std::clamp(rounded, 0, static_cast<int>(bit_depth_max(test_case.source_bit_depth))));
}

inline std::uint32_t convert_bits_dither_reference(std::uint32_t source, std::size_t x,
                                                   std::size_t y,
                                                   const ConvertBitsDitherCase& test_case) {
  const int bit_difference = test_case.source_bit_depth - test_case.dither_bit_depth;
  const int order = (bit_difference + 1) / 2;
  const std::size_t matrix_size = std::size_t{1} << order;
  std::uint32_t correction = bayer_value(x & (matrix_size - 1), y & (matrix_size - 1), order);
  if ((bit_difference & 1) != 0) {
    correction >>= 1;
  }

  const auto remapped = remap_dither_source(source, test_case);
  int quantized{};
  if (test_case.dither_bit_depth < 8) {
    const float half_correction =
        static_cast<float>((std::uint32_t{1} << bit_difference) - 1) / 2.0F;
    quantized = static_cast<int>(static_cast<float>(remapped) + static_cast<float>(correction) -
                                 half_correction) >>
                bit_difference;
  } else {
    quantized = static_cast<int>((remapped + correction) >> bit_difference);
  }

  const int target_maximum = static_cast<int>(bit_depth_max(test_case.target_bit_depth));
  if (test_case.target_bit_depth != test_case.dither_bit_depth) {
    const int dither_maximum =
        static_cast<int>((std::uint32_t{1} << test_case.dither_bit_depth) - 1);
    quantized = std::min(quantized, dither_maximum);
    if (test_case.dither_bit_depth < 8) {
      quantized =
          static_cast<int>(static_cast<float>(quantized) * (static_cast<float>(target_maximum) /
                                                            static_cast<float>(dither_maximum)) +
                           0.5F);
    } else {
      quantized <<= test_case.target_bit_depth - test_case.dither_bit_depth;
    }
  }
  return static_cast<std::uint32_t>(std::clamp(quantized, 0, target_maximum));
}

template <typename Source, typename Target>
void run_convert_bits_dither_case_typed(const ConvertBitsDitherCase& test_case) {
  GuardedVideoBuffer<Source> source(test_case.width, test_case.height, test_case.source_pitch, 64);
  GuardedVideoBuffer<Target> expected(test_case.width, test_case.height, test_case.target_pitch,
                                      64);
  GuardedVideoBuffer<Target> actual(test_case.width, test_case.height, test_case.target_pitch, 64);

  ConvertBitsIntegerCase input_case{test_case.storage,
                                    test_case.chroma,
                                    test_case.source_full,
                                    test_case.target_full,
                                    test_case.source_bit_depth,
                                    test_case.target_bit_depth,
                                    test_case.width,
                                    test_case.height,
                                    test_case.source_pitch,
                                    test_case.target_pitch,
                                    {},
                                    {},
                                    {}};
  fill_convert_bits_integer_source(source.view(), input_case);
  const auto source_snapshot = source.snapshot_active();
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width; ++x) {
      expected.view().row(y)[x] = static_cast<Target>(
          convert_bits_dither_reference(source.view().row(y)[x], x, y, test_case));
    }
  }

  test_case.variant.function(
      reinterpret_cast<const BYTE*>(source.view().data()),
      reinterpret_cast<BYTE*>(actual.view().data()),
      static_cast<int>(test_case.width * sizeof(Source)), static_cast<int>(test_case.height),
      static_cast<int>(test_case.source_pitch), static_cast<int>(test_case.target_pitch),
      test_case.source_bit_depth, test_case.target_bit_depth, test_case.dither_bit_depth);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name;
  EXPECT_EQ(format_hash(hash_active(actual.view().as_const())), test_case.expected_hash)
      << test_case.name;
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name;
  EXPECT_TRUE(source.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
  EXPECT_TRUE(actual.memory_intact()) << test_case.name;
}

inline void run_convert_bits_dither_case(const ConvertBitsDitherCase& test_case) {
  switch (test_case.storage) {
    case IntegerStorage::UInt8ToUInt8:
      run_convert_bits_dither_case_typed<std::uint8_t, std::uint8_t>(test_case);
      break;
    case IntegerStorage::UInt16ToUInt8:
      run_convert_bits_dither_case_typed<std::uint16_t, std::uint8_t>(test_case);
      break;
    case IntegerStorage::UInt16ToUInt16:
      run_convert_bits_dither_case_typed<std::uint16_t, std::uint16_t>(test_case);
      break;
    case IntegerStorage::UInt8ToUInt16:
      throw std::invalid_argument("ordered-dither widening case is unsupported");
  }
}

template <typename Source, typename Target, bool Chroma, bool SourceFull, bool TargetFull>
void add_convert_bits_dither_variants(std::vector<ConvertBitsDitherCase>& cases,
                                      IntegerStorage storage, int source_bit_depth,
                                      int target_bit_depth, int dither_bit_depth,
                                      std::size_t height, std::size_t source_pitch,
                                      std::size_t target_pitch, const char* expected_hash) {
  constexpr std::size_t width = 32;
  cases.push_back(make_convert_bits_dither_case(
      storage, Chroma, SourceFull, TargetFull, source_bit_depth, target_bit_depth, dither_bit_depth,
      width, height, source_pitch, target_pitch,
      Variant<ConvertBitsIntegerFunc>{
          "sse41",
          convert_ordered_dither_uint_sse41<Source, Target, Chroma, SourceFull, TargetFull>,
          IsaRequirement::Sse41},
      expected_hash));
  cases.push_back(make_convert_bits_dither_case(
      storage, Chroma, SourceFull, TargetFull, source_bit_depth, target_bit_depth, dither_bit_depth,
      width, height, source_pitch, target_pitch,
      Variant<ConvertBitsIntegerFunc>{
          "avx2", convert_ordered_dither_uint_avx2<Source, Target, Chroma, SourceFull, TargetFull>,
          IsaRequirement::Avx2},
      expected_hash));
}

}  // namespace avsut::test
