#pragma once

#include "filters/overlay/intel/444convert_avx2.h"
#include "filters/overlay/intel/444convert_sse.h"

#include "support/comparators.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace avsut::test {

using OverlayChromaFunction = void (*)(BYTE*, const BYTE*, int, int, int, int);

enum class OverlayChromaOperation {
  Yv12ToYv24,
  Yv16ToYv24,
  Yv24ToYv12,
  Yv24ToYv16,
};

enum class OverlayChromaSampleKind {
  U8,
  U16LessThan16Bit,
  U16FullRange,
  Float,
};

struct OverlayChromaCase {
  OverlayChromaOperation operation;
  OverlayChromaSampleKind sample_kind;
  std::size_t source_width_pixels{};
  std::size_t source_height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<OverlayChromaFunction> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline const char* overlay_chroma_operation_name(OverlayChromaOperation operation) {
  switch (operation) {
    case OverlayChromaOperation::Yv12ToYv24:
      return "Yv12ToYv24";
    case OverlayChromaOperation::Yv16ToYv24:
      return "Yv16ToYv24";
    case OverlayChromaOperation::Yv24ToYv12:
      return "Yv24ToYv12";
    case OverlayChromaOperation::Yv24ToYv16:
      return "Yv24ToYv16";
  }
  return "Unknown";
}

inline const char* overlay_chroma_sample_name(OverlayChromaSampleKind sample_kind) {
  switch (sample_kind) {
    case OverlayChromaSampleKind::U8:
      return "U8";
    case OverlayChromaSampleKind::U16LessThan16Bit:
      return "U16LessThan16Bit";
    case OverlayChromaSampleKind::U16FullRange:
      return "U16FullRange";
    case OverlayChromaSampleKind::Float:
      return "Float";
  }
  return "Unknown";
}

inline std::size_t overlay_chroma_destination_width(const OverlayChromaCase& test_case) {
  switch (test_case.operation) {
    case OverlayChromaOperation::Yv12ToYv24:
    case OverlayChromaOperation::Yv16ToYv24:
      return test_case.source_width_pixels * 2;
    case OverlayChromaOperation::Yv24ToYv12:
    case OverlayChromaOperation::Yv24ToYv16:
      return test_case.source_width_pixels / 2;
  }
  return 0;
}

inline std::size_t overlay_chroma_destination_height(const OverlayChromaCase& test_case) {
  switch (test_case.operation) {
    case OverlayChromaOperation::Yv12ToYv24:
      return test_case.source_height * 2;
    case OverlayChromaOperation::Yv16ToYv24:
    case OverlayChromaOperation::Yv24ToYv16:
      return test_case.source_height;
    case OverlayChromaOperation::Yv24ToYv12:
      return test_case.source_height / 2;
  }
  return 0;
}

inline std::string overlay_chroma_variant_name(const Variant<OverlayChromaFunction>& variant) {
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

inline std::string overlay_chroma_case_name(const OverlayChromaCase& test_case) {
  std::ostringstream stream;
  stream << overlay_chroma_operation_name(test_case.operation) << '_'
         << overlay_chroma_sample_name(test_case.sample_kind) << "_SrcWidth"
         << test_case.source_width_pixels << "_SrcHeight" << test_case.source_height << "_SrcPitch"
         << test_case.source_pitch << "_DstWidth" << overlay_chroma_destination_width(test_case)
         << "_DstHeight" << overlay_chroma_destination_height(test_case) << "_DstPitch"
         << test_case.destination_pitch << "_Alignment0";
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << "_PatternChromaAnchors_" << overlay_chroma_variant_name(test_case.variant);
  return stream.str();
}

inline OverlayChromaCase make_overlay_chroma_case(
    OverlayChromaOperation operation, OverlayChromaSampleKind sample_kind,
    std::size_t source_width_pixels, std::size_t source_height, std::size_t source_pitch,
    std::size_t destination_pitch, Variant<OverlayChromaFunction> variant,
    std::string expected_hash, std::uint32_t seed = 0) {
  OverlayChromaCase result{operation,
                           sample_kind,
                           source_width_pixels,
                           source_height,
                           source_pitch,
                           destination_pitch,
                           std::move(variant),
                           std::move(expected_hash),
                           seed,
                           {}};
  result.name = overlay_chroma_case_name(result);
  return result;
}

inline void PrintTo(const OverlayChromaCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_overlay_chroma_input(PlaneView<T> source, OverlayChromaSampleKind sample_kind,
                               std::uint32_t seed = 0) {
  if constexpr (std::is_integral_v<T>) {
    constexpr std::array<std::uint32_t, 16> kAnchors{
        0U,   1U,    2U,     15U,    16U,    127U,   128U,   255U,
        256U, 1023U, 16383U, 16384U, 32767U, 32768U, 65534U, 65535U,
    };
    const auto mask = sample_kind == OverlayChromaSampleKind::U16LessThan16Bit
                          ? 0x3fffU
                          : static_cast<std::uint32_t>(std::numeric_limits<T>::max());
    const auto anchor_offset = static_cast<std::size_t>(seed % kAnchors.size());
    const auto seed_perturbation = (seed >> 8U) & 0xffU;
    for (std::size_t y = 0; y < source.height(); ++y) {
      for (std::size_t x = 0; x < source.width(); ++x) {
        const auto anchor = kAnchors[(x * 5U + y * 7U + anchor_offset) % kAnchors.size()];
        const auto perturbation = static_cast<std::uint32_t>(x * 277U + y * 4099U +
                                                             (x ^ y) * 17U + seed_perturbation);
        source.row(y)[x] = static_cast<T>((anchor + perturbation) & mask);
      }
    }
  } else {
    static_assert(std::is_same_v<T, float>);
    constexpr std::array<float, 12> kAnchors{
        -2.0F, -1.0F, -0.125F, 0.0F, 0.03125F, 0.125F, 0.5F, 1.0F, 3.5F, 16.0F, 63.75F, 255.5F,
    };
    const auto anchor_offset = static_cast<std::size_t>(seed % kAnchors.size());
    const auto seed_perturbation = static_cast<int>((seed >> 8U) % 19U);
    for (std::size_t y = 0; y < source.height(); ++y) {
      for (std::size_t x = 0; x < source.width(); ++x) {
        const auto anchor = kAnchors[(x * 3U + y * 5U + anchor_offset) % kAnchors.size()];
        const auto perturbation =
            static_cast<float>(static_cast<int>((x * 11U + y * 13U + seed_perturbation) % 19U) -
                               9) *
            0.015625F;
        source.row(y)[x] = anchor + perturbation;
      }
    }
  }
}

template <typename T>
T overlay_chroma_horizontal_average(T first, T second) {
  if constexpr (std::is_integral_v<T>) {
    return static_cast<T>((static_cast<std::uint64_t>(first) + second + 1U) / 2U);
  } else {
    return static_cast<T>((static_cast<double>(first) + second) * 0.5);
  }
}

template <typename T>
T overlay_chroma_2x2_average(T first, T second, T third, T fourth) {
  if constexpr (std::is_integral_v<T>) {
    return static_cast<T>((static_cast<std::uint64_t>(first) + second + third + fourth + 2U) / 4U);
  } else {
    return static_cast<T>((static_cast<double>(first) + second + third + fourth) * 0.25);
  }
}

template <typename T>
void make_overlay_chroma_reference(PlaneView<const T> source, PlaneView<T> destination,
                                   const OverlayChromaCase& test_case) {
  switch (test_case.operation) {
    case OverlayChromaOperation::Yv12ToYv24:
      for (std::size_t y = 0; y < source.height(); ++y) {
        for (std::size_t x = 0; x < source.width(); ++x) {
          const T value = source.row(y)[x];
          destination.row(y * 2)[x * 2] = value;
          destination.row(y * 2)[x * 2 + 1] = value;
          destination.row(y * 2 + 1)[x * 2] = value;
          destination.row(y * 2 + 1)[x * 2 + 1] = value;
        }
      }
      return;
    case OverlayChromaOperation::Yv16ToYv24:
      for (std::size_t y = 0; y < source.height(); ++y) {
        for (std::size_t x = 0; x < source.width(); ++x) {
          const T value = source.row(y)[x];
          destination.row(y)[x * 2] = value;
          destination.row(y)[x * 2 + 1] = value;
        }
      }
      return;
    case OverlayChromaOperation::Yv24ToYv12:
      for (std::size_t y = 0; y < destination.height(); ++y) {
        const auto* row0 = source.row(y * 2);
        const auto* row1 = source.row(y * 2 + 1);
        for (std::size_t x = 0; x < destination.width(); ++x) {
          destination.row(y)[x] = overlay_chroma_2x2_average(row0[x * 2], row0[x * 2 + 1],
                                                             row1[x * 2], row1[x * 2 + 1]);
        }
      }
      return;
    case OverlayChromaOperation::Yv24ToYv16:
      for (std::size_t y = 0; y < destination.height(); ++y) {
        const auto* row = source.row(y);
        for (std::size_t x = 0; x < destination.width(); ++x) {
          destination.row(y)[x] = overlay_chroma_horizontal_average(row[x * 2], row[x * 2 + 1]);
        }
      }
      return;
  }
}

inline std::uint64_t overlay_chroma_ulp_distance(float lhs, float rhs) {
  if (lhs == rhs) {
    return 0;
  }
  std::uint32_t lhs_bits{};
  std::uint32_t rhs_bits{};
  std::memcpy(&lhs_bits, &lhs, sizeof(lhs_bits));
  std::memcpy(&rhs_bits, &rhs, sizeof(rhs_bits));
  constexpr std::uint32_t kSignBit = 0x80000000U;
  if ((lhs_bits & kSignBit) != (rhs_bits & kSignBit)) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  const auto lhs_magnitude = lhs_bits & ~kSignBit;
  const auto rhs_magnitude = rhs_bits & ~kSignBit;
  return lhs_magnitude >= rhs_magnitude ? static_cast<std::uint64_t>(lhs_magnitude - rhs_magnitude)
                                        : static_cast<std::uint64_t>(rhs_magnitude - lhs_magnitude);
}

inline ::testing::AssertionResult compare_overlay_chroma_float(PlaneView<const float> expected,
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
        return ::testing::AssertionFailure() << "row=" << y << " col=" << x
                                             << " non-finite expected=" << lhs << " actual=" << rhs;
      }
      const float absolute_error = std::abs(lhs - rhs);
      const auto ulps = overlay_chroma_ulp_distance(lhs, rhs);
      if (absolute_error <= absolute_floor || ulps <= maximum_ulps) {
        continue;
      }
      return ::testing::AssertionFailure()
             << "row=" << y << " col=" << x << " expected=" << lhs << " actual=" << rhs
             << " absolute_error=" << absolute_error << " ulps=" << ulps
             << " allowed_ulps=" << maximum_ulps << " absolute_floor=" << absolute_floor;
    }
  }
  return ::testing::AssertionSuccess();
}

template <typename T>
void run_overlay_chroma_case(const OverlayChromaCase& test_case) {
  const auto destination_width = overlay_chroma_destination_width(test_case);
  const auto destination_height = overlay_chroma_destination_height(test_case);
  GuardedVideoBuffer<T> source(test_case.source_width_pixels, test_case.source_height,
                               test_case.source_pitch, 64);
  GuardedVideoBuffer<T> expected(destination_width, destination_height, test_case.destination_pitch,
                                 64);
  GuardedVideoBuffer<T> actual(destination_width, destination_height, test_case.destination_pitch,
                               64);

  fill_overlay_chroma_input(source.view(), test_case.sample_kind, test_case.seed);
  const auto source_snapshot = source.snapshot_active();
  make_overlay_chroma_reference(source.view().as_const(), expected.view(), test_case);

  const bool upsampling = test_case.operation == OverlayChromaOperation::Yv12ToYv24 ||
                          test_case.operation == OverlayChromaOperation::Yv16ToYv24;
  const auto width_argument =
      upsampling ? test_case.source_width_pixels : destination_width * sizeof(T);
  const auto height_argument = upsampling ? test_case.source_height : destination_height;
  test_case.variant.function(reinterpret_cast<BYTE*>(actual.view().data()),
                             reinterpret_cast<const BYTE*>(source.view().data()),
                             static_cast<int>(actual.view().pitch_bytes()),
                             static_cast<int>(source.view().pitch_bytes()),
                             static_cast<int>(width_argument), static_cast<int>(height_argument));

  if constexpr (std::is_integral_v<T>) {
    EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
        << test_case.name << " reference mismatch for variant " << test_case.variant.name;
    EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
        << test_case.name << " stable output hash mismatch";
  } else {
    EXPECT_TRUE(compare_overlay_chroma_float(expected.view().as_const(), actual.view().as_const()))
        << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  }
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << test_case.name << " modified the source input";
  EXPECT_TRUE(source.memory_intact())
      << test_case.name << " source padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " destination padding or guards were corrupted";
}

}  // namespace avsut::test
