#pragma once

#include "filters/intel/layer_avx2.h"
#include "filters/intel/layer_sse.h"

#include "support/deterministic_data.h"
#include "support/comparators.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace avsut::test {

using LayerFrameInvert8Function = void (*)(BYTE*, int, int, int, int);
using LayerFrameInvert16Function = void (*)(BYTE*, int, int, int, std::uint64_t);

struct LayerFrameInvert8Case {
  std::size_t row_bytes{};
  std::size_t height{};
  std::size_t pitch{};
  std::uint32_t mask{};
  Variant<LayerFrameInvert8Function> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

struct LayerFrameInvert16Case {
  std::size_t row_bytes{};
  std::size_t height{};
  std::size_t pitch{};
  std::uint64_t mask{};
  Variant<LayerFrameInvert16Function> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

template <typename Function>
inline std::string layer_frame_invert_variant_name(const Variant<Function>& variant) {
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

inline std::string layer_frame_invert_8_case_name(const LayerFrameInvert8Case& test_case) {
  std::ostringstream stream;
  stream << "PackedFrame8_RowBytes" << test_case.row_bytes << "_Height" << test_case.height
         << "_Pitch" << test_case.pitch << "_Mask" << std::uppercase << std::hex << std::setw(8)
         << std::setfill('0') << test_case.mask << std::dec;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed << std::dec;
  }
  stream << (test_case.seed == 0 ? "_PatternByteRamp_" : "_PatternFixedRandom_")
         << layer_frame_invert_variant_name(test_case.variant);
  return stream.str();
}

inline std::string layer_frame_invert_16_case_name(const LayerFrameInvert16Case& test_case) {
  std::ostringstream stream;
  stream << "PackedFrame16_RowBytes" << test_case.row_bytes << "_Height" << test_case.height
         << "_Pitch" << test_case.pitch << "_Mask" << std::uppercase << std::hex << std::setw(16)
         << std::setfill('0') << test_case.mask << std::dec;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed << std::dec;
  }
  stream << (test_case.seed == 0 ? "_PatternWordRamp_" : "_PatternFixedRandom_")
         << layer_frame_invert_variant_name(test_case.variant);
  return stream.str();
}

inline LayerFrameInvert8Case make_layer_frame_invert_8_case(
    std::size_t row_bytes, std::size_t height, std::size_t pitch, std::uint32_t mask,
    Variant<LayerFrameInvert8Function> variant, std::string expected_hash = {},
    std::uint32_t seed = 0) {
  if (row_bytes == 0 || height == 0 || pitch != row_bytes || (pitch % 32) != 0) {
    throw std::invalid_argument(
        "Layer 8-bit frame inversion requires a non-empty 32-byte aligned row");
  }
  LayerFrameInvert8Case result{
      row_bytes, height, pitch, mask, std::move(variant), std::move(expected_hash), seed, {}};
  result.name = layer_frame_invert_8_case_name(result);
  return result;
}

inline LayerFrameInvert16Case make_layer_frame_invert_16_case(
    std::size_t row_bytes, std::size_t height, std::size_t pitch, std::uint64_t mask,
    Variant<LayerFrameInvert16Function> variant, std::string expected_hash = {},
    std::uint32_t seed = 0) {
  if (row_bytes == 0 || (row_bytes % 2) != 0 || height == 0 || pitch != row_bytes ||
      (pitch % 32) != 0) {
    throw std::invalid_argument(
        "Layer 16-bit frame inversion requires a non-empty even 32-byte aligned row");
  }
  LayerFrameInvert16Case result{
      row_bytes, height, pitch, mask, std::move(variant), std::move(expected_hash), seed, {}};
  result.name = layer_frame_invert_16_case_name(result);
  return result;
}

inline void PrintTo(const LayerFrameInvert8Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const LayerFrameInvert16Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_layer_frame_invert_8_input(PlaneView<std::uint8_t> frame,
                                            std::uint32_t seed = 0) {
  if (seed != 0) {
    fill_random(frame, seed);
    return;
  }
  for (std::size_t y = 0; y < frame.height(); ++y) {
    for (std::size_t x = 0; x < frame.width(); ++x) {
      const auto value = 0x17U + 37U * static_cast<unsigned>(x) + 91U * static_cast<unsigned>(y) +
                         ((13U * static_cast<unsigned>(x)) ^ (29U * static_cast<unsigned>(y)));
      frame.row(y)[x] = static_cast<std::uint8_t>(value & 0xffU);
    }
  }
}

inline void fill_layer_frame_invert_16_input(PlaneView<std::uint16_t> frame,
                                             std::uint32_t seed = 0) {
  if (seed != 0) {
    fill_random(frame, seed);
    return;
  }
  for (std::size_t y = 0; y < frame.height(); ++y) {
    for (std::size_t x = 0; x < frame.width(); ++x) {
      const auto value = 0x1357U + 0x2468U * static_cast<unsigned>(x) +
                         0x1111U * static_cast<unsigned>(y) +
                         ((7U * static_cast<unsigned>(x)) ^ (19U * static_cast<unsigned>(y)));
      frame.row(y)[x] = static_cast<std::uint16_t>(value & 0xffffU);
    }
  }
}

inline void apply_layer_frame_invert_8_reference(const LayerFrameInvert8Case& test_case,
                                                 PlaneView<std::uint8_t> frame) {
  for (std::size_t y = 0; y < frame.height(); ++y) {
    for (std::size_t x = 0; x < frame.width(); ++x) {
      const auto shift = 8U * static_cast<unsigned>(x & 3U);
      frame.row(y)[x] ^= static_cast<std::uint8_t>((test_case.mask >> shift) & 0xffU);
    }
  }
}

inline void apply_layer_frame_invert_16_reference(const LayerFrameInvert16Case& test_case,
                                                  PlaneView<std::uint16_t> frame) {
  for (std::size_t y = 0; y < frame.height(); ++y) {
    for (std::size_t x = 0; x < frame.width(); ++x) {
      const auto shift = 16U * static_cast<unsigned>(x & 3U);
      frame.row(y)[x] ^= static_cast<std::uint16_t>((test_case.mask >> shift) & 0xffffU);
    }
  }
}

inline void run_layer_frame_invert_8_case(const LayerFrameInvert8Case& test_case) {
  GuardedVideoBuffer<std::uint8_t> actual(test_case.row_bytes, test_case.height, test_case.pitch,
                                          64);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.row_bytes, test_case.height, test_case.pitch,
                                            64);
  fill_layer_frame_invert_8_input(actual.view(), test_case.seed);
  for (std::size_t y = 0; y < actual.view().height(); ++y) {
    std::copy_n(actual.view().row(y), actual.view().width(), expected.view().row(y));
  }
  apply_layer_frame_invert_8_reference(test_case, expected.view());

  test_case.variant.function(reinterpret_cast<BYTE*>(actual.view().data()),
                             static_cast<int>(test_case.pitch),
                             static_cast<int>(test_case.row_bytes),
                             static_cast<int>(test_case.height), static_cast<int>(test_case.mask));

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  const auto actual_hash = format_hash(hash_active(actual.view().as_const()));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " corrupted frame bytes beyond the pitched rows";
  EXPECT_TRUE(expected.memory_intact()) << test_case.name << " corrupted reference storage";
}

inline void run_layer_frame_invert_16_case(const LayerFrameInvert16Case& test_case) {
  const auto width_words = test_case.row_bytes / sizeof(std::uint16_t);
  GuardedVideoBuffer<std::uint16_t> actual(width_words, test_case.height, test_case.pitch, 64);
  GuardedVideoBuffer<std::uint16_t> expected(width_words, test_case.height, test_case.pitch, 64);
  fill_layer_frame_invert_16_input(actual.view(), test_case.seed);
  for (std::size_t y = 0; y < actual.view().height(); ++y) {
    std::copy_n(actual.view().row(y), actual.view().width(), expected.view().row(y));
  }
  apply_layer_frame_invert_16_reference(test_case, expected.view());

  test_case.variant.function(
      reinterpret_cast<BYTE*>(actual.view().data()), static_cast<int>(test_case.pitch),
      static_cast<int>(test_case.row_bytes), static_cast<int>(test_case.height), test_case.mask);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  const auto actual_hash = format_hash(hash_active(actual.view().as_const()));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " corrupted frame bytes beyond the pitched rows";
  EXPECT_TRUE(expected.memory_intact()) << test_case.name << " corrupted reference storage";
}

}  // namespace avsut::test
