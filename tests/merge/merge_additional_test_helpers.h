#pragma once

#include "merge_float_test_helpers.h"

#include "filters/intel/merge_avx2.h"
#include "filters/intel/merge_sse.h"

#include "support/comparators.h"
#include "support/cpu_features.h"
#include "support/deterministic_data.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace avsut::test {

using Yuy2MergeFunc = void (*)(BYTE*, const BYTE*, int, int, int, int, int, int);
using Yuy2ReplaceFunc = void (*)(BYTE*, const BYTE*, int, int, int, int);
using AveragePlaneFunc = void (*)(BYTE*, const BYTE*, int, int, int, int);

enum class Yuy2MergeOperation { WeightedChroma, WeightedLuma, ReplaceLuma };

struct Yuy2MergeCase {
  Yuy2MergeOperation operation{};
  std::size_t width_bytes{};
  std::size_t height{};
  std::size_t destination_pitch{};
  std::size_t other_pitch{};
  int weight{};
  int inverse_weight{};
  Yuy2MergeFunc weighted_function{};
  Yuy2ReplaceFunc replace_function{};
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline void PrintTo(const Yuy2MergeCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline const char* yuy2_operation_name(Yuy2MergeOperation operation) {
  switch (operation) {
    case Yuy2MergeOperation::WeightedChroma:
      return "WeightedChroma";
    case Yuy2MergeOperation::WeightedLuma:
      return "WeightedLuma";
    case Yuy2MergeOperation::ReplaceLuma:
      return "ReplaceLuma";
  }
  return "Unknown";
}

inline Yuy2MergeCase make_yuy2_merge_case(Yuy2MergeOperation operation, std::size_t width_bytes,
                                          std::size_t height, std::size_t destination_pitch,
                                          std::size_t other_pitch, int weight,
                                          Yuy2MergeFunc weighted_function,
                                          Yuy2ReplaceFunc replace_function,
                                          std::string expected_hash, std::uint32_t seed = 0) {
  if (width_bytes == 0 || (width_bytes % 2) != 0) {
    throw std::invalid_argument("YUY2 row size must be a positive even value");
  }
  Yuy2MergeCase result{operation,
                       width_bytes,
                       height,
                       destination_pitch,
                       other_pitch,
                       weight,
                       32768 - weight,
                       weighted_function,
                       replace_function,
                       std::move(expected_hash),
                       seed,
                       {}};
  std::ostringstream stream;
  stream << yuy2_operation_name(operation) << "_WidthBytes" << width_bytes << "_Height" << height
         << "_DstPitch" << destination_pitch << "_OtherPitch" << other_pitch;
  if (operation != Yuy2MergeOperation::ReplaceLuma) {
    stream << "_Weight" << weight;
  }
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternBoundaryValues_" : "_PatternFixedRandom_")
         << "VariantSse2";
  result.name = stream.str();
  return result;
}

inline void fill_yuy2_inputs(PlaneView<std::uint8_t> destination, PlaneView<std::uint8_t> other,
                             std::uint32_t seed = 0) {
  if (seed != 0) {
    fill_random(destination, seed);
    fill_random(other, seed ^ 0xA5A5A5A5U);
    return;
  }
  constexpr std::uint8_t anchors[] = {0, 1, 15, 16, 127, 128, 254, 255};
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < destination.width(); ++x) {
      destination.row(y)[x] = anchors[(x + 3 * y) % std::size(anchors)];
      other.row(y)[x] = anchors[(5 * x + y + 3) % std::size(anchors)];
    }
  }
}

inline void apply_yuy2_merge_reference(const Yuy2MergeCase& test_case,
                                       PlaneView<std::uint8_t> destination,
                                       PlaneView<const std::uint8_t> other) {
  const bool replace_chroma = test_case.operation == Yuy2MergeOperation::WeightedChroma;
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width_bytes; ++x) {
      const bool is_chroma = (x % 2) != 0;
      if (test_case.operation == Yuy2MergeOperation::ReplaceLuma) {
        if (!is_chroma) {
          destination.row(y)[x] = other.row(y)[x];
        }
      } else if (is_chroma == replace_chroma) {
        const int first = destination.row(y)[x];
        const int second = other.row(y)[x];
        destination.row(y)[x] = static_cast<std::uint8_t>(
            (second * test_case.weight + first * test_case.inverse_weight + 16384) >> 15);
      }
    }
  }
}

inline void run_yuy2_merge_case(const Yuy2MergeCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> destination(test_case.width_bytes, test_case.height,
                                               test_case.destination_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> other(test_case.width_bytes, test_case.height,
                                         test_case.other_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> expected(test_case.width_bytes, test_case.height,
                                            test_case.destination_pitch, 64);

  fill_yuy2_inputs(destination.view(), other.view(), test_case.seed);
  copy_active(destination.view().as_const(), expected.view());
  const auto other_snapshot = other.snapshot_active();
  apply_yuy2_merge_reference(test_case, expected.view(), other.view().as_const());

  if (test_case.operation == Yuy2MergeOperation::ReplaceLuma) {
    test_case.replace_function(
        reinterpret_cast<BYTE*>(destination.view().data()),
        reinterpret_cast<const BYTE*>(other.view().data()),
        static_cast<int>(test_case.destination_pitch), static_cast<int>(test_case.other_pitch),
        static_cast<int>(test_case.width_bytes), static_cast<int>(test_case.height));
  } else {
    test_case.weighted_function(
        reinterpret_cast<BYTE*>(destination.view().data()),
        reinterpret_cast<const BYTE*>(other.view().data()),
        static_cast<int>(test_case.destination_pitch), static_cast<int>(test_case.other_pitch),
        static_cast<int>(test_case.width_bytes), static_cast<int>(test_case.height),
        test_case.weight, test_case.inverse_weight);
  }

  EXPECT_TRUE(compare_exact(expected.view().as_const(), destination.view().as_const()))
      << test_case.name;
  const auto actual_hash = format_hash(hash_active(destination.view().as_const()));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(other.active_matches(other_snapshot))
      << test_case.name << " modified the second input";
  EXPECT_TRUE(destination.memory_intact()) << test_case.name;
  EXPECT_TRUE(other.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
}

enum class AverageSample { UInt8, UInt16, Float };

struct AveragePlaneCase {
  AverageSample sample{};
  std::size_t width{};
  std::size_t height{};
  std::size_t destination_pitch{};
  std::size_t other_pitch{};
  std::size_t alignment_offset{};
  Variant<AveragePlaneFunc> variant;
  std::string expected_hash;
  std::uint32_t seed{};
  std::string name;
};

inline void PrintTo(const AveragePlaneCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline const char* average_sample_name(AverageSample sample) {
  switch (sample) {
    case AverageSample::UInt8:
      return "UInt8";
    case AverageSample::UInt16:
      return "UInt16";
    case AverageSample::Float:
      return "Float";
  }
  return "Unknown";
}

inline AveragePlaneCase make_average_plane_case(AverageSample sample, std::size_t width,
                                                std::size_t height, std::size_t destination_pitch,
                                                std::size_t other_pitch,
                                                std::size_t alignment_offset,
                                                Variant<AveragePlaneFunc> variant,
                                                std::string expected_hash = {},
                                                std::uint32_t seed = 0) {
  AveragePlaneCase result{sample,
                          width,
                          height,
                          destination_pitch,
                          other_pitch,
                          alignment_offset,
                          std::move(variant),
                          std::move(expected_hash),
                          seed,
                          {}};
  std::ostringstream stream;
  stream << average_sample_name(sample) << "_Width" << width << "_Height" << height << "_DstPitch"
         << destination_pitch << "_OtherPitch" << other_pitch << "_AlignOffset" << alignment_offset;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed << "_PatternFixedRandom";
  } else {
    stream << (sample == AverageSample::Float ? "_PatternFiniteAnchors" : "_PatternBoundaryValues");
  }
  stream << "_Variant";
  bool capitalize = true;
  for (const char character : result.variant.name) {
    stream << static_cast<char>(
        capitalize && character >= 'a' && character <= 'z' ? character - ('a' - 'A') : character);
    capitalize = character == '-';
  }
  result.name = stream.str();
  return result;
}

template <typename T>
void fill_average_inputs(PlaneView<T> destination, PlaneView<T> other) {
  static_assert(std::is_integral_v<T>);
  constexpr std::uint32_t anchors[] = {0, 1, 2, 127, 128, 254, 255, 65534, 65535};
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < destination.width(); ++x) {
      destination.row(y)[x] = static_cast<T>(anchors[(x + 2 * y) % std::size(anchors)]);
      other.row(y)[x] = static_cast<T>(anchors[(4 * x + y + 5) % std::size(anchors)]);
    }
  }
}

template <typename T>
void fill_average_inputs(PlaneView<T> destination, PlaneView<T> other, std::uint32_t seed) {
  static_assert(std::is_integral_v<T>);
  fill_random(destination, seed);
  fill_random(other, seed ^ 0xA5A5A5A5U);
}

inline void fill_average_float_inputs(PlaneView<float> destination, PlaneView<float> other) {
  constexpr float anchors[] = {-1000.5F, -17.25F, -0.125F, 0.0F, 0.125F, 31.75F, 255.5F, 4096.0F};
  for (std::size_t y = 0; y < destination.height(); ++y) {
    for (std::size_t x = 0; x < destination.width(); ++x) {
      destination.row(y)[x] = anchors[(x + 3 * y) % std::size(anchors)];
      other.row(y)[x] = anchors[(5 * x + y + 2) % std::size(anchors)];
    }
  }
}

inline void fill_average_float_inputs(PlaneView<float> destination, PlaneView<float> other,
                                      std::uint32_t seed) {
  fill_random(destination, seed);
  fill_random(other, seed ^ 0xA5A5A5A5U);
}

template <typename T>
void run_average_integer_case(const AveragePlaneCase& test_case) {
  GuardedVideoBuffer<T> destination(test_case.width, test_case.height, test_case.destination_pitch,
                                    64, test_case.alignment_offset);
  GuardedVideoBuffer<T> other(test_case.width, test_case.height, test_case.other_pitch, 64,
                              test_case.alignment_offset);
  GuardedVideoBuffer<T> expected(test_case.width, test_case.height, test_case.destination_pitch, 64,
                                 test_case.alignment_offset);
  if (test_case.seed == 0) {
    fill_average_inputs(destination.view(), other.view());
  } else {
    fill_average_inputs(destination.view(), other.view(), test_case.seed);
  }
  copy_active(destination.view().as_const(), expected.view());
  const auto other_snapshot = other.snapshot_active();
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width; ++x) {
      expected.view().row(y)[x] =
          static_cast<T>((static_cast<std::uint32_t>(expected.view().row(y)[x]) +
                          static_cast<std::uint32_t>(other.view().row(y)[x]) + 1) >>
                         1);
    }
  }
  test_case.variant.function(
      reinterpret_cast<BYTE*>(destination.view().data()),
      reinterpret_cast<const BYTE*>(other.view().data()),
      static_cast<int>(test_case.destination_pitch), static_cast<int>(test_case.other_pitch),
      static_cast<int>(test_case.width * sizeof(T)), static_cast<int>(test_case.height));
  EXPECT_TRUE(compare_exact(expected.view().as_const(), destination.view().as_const()))
      << test_case.name;
  const auto actual_hash = format_hash(hash_active(destination.view().as_const()));
  EXPECT_EQ(actual_hash, test_case.expected_hash)
      << test_case.name << " stable output hash mismatch; actual=" << actual_hash;
  EXPECT_TRUE(other.active_matches(other_snapshot)) << test_case.name;
  EXPECT_TRUE(destination.memory_intact()) << test_case.name;
  EXPECT_TRUE(other.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
}

inline void run_average_float_case(const AveragePlaneCase& test_case) {
  GuardedVideoBuffer<float> destination(test_case.width, test_case.height,
                                        test_case.destination_pitch, 64,
                                        test_case.alignment_offset);
  GuardedVideoBuffer<float> other(test_case.width, test_case.height, test_case.other_pitch, 64,
                                  test_case.alignment_offset);
  GuardedVideoBuffer<float> expected(test_case.width, test_case.height, test_case.destination_pitch,
                                     64, test_case.alignment_offset);
  if (test_case.seed == 0) {
    fill_average_float_inputs(destination.view(), other.view());
  } else {
    fill_average_float_inputs(destination.view(), other.view(), test_case.seed);
  }
  copy_active(destination.view().as_const(), expected.view());
  const auto other_snapshot = other.snapshot_active();
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width; ++x) {
      expected.view().row(y)[x] =
          static_cast<float>((static_cast<double>(expected.view().row(y)[x]) +
                              static_cast<double>(other.view().row(y)[x])) *
                             0.5);
    }
  }
  test_case.variant.function(
      reinterpret_cast<BYTE*>(destination.view().data()),
      reinterpret_cast<const BYTE*>(other.view().data()),
      static_cast<int>(test_case.destination_pitch), static_cast<int>(test_case.other_pitch),
      static_cast<int>(test_case.width * sizeof(float)), static_cast<int>(test_case.height));
  ASSERT_TRUE(require_finite(destination.view().as_const())) << test_case.name;
  EXPECT_TRUE(compare_float_ulp(expected.view().as_const(), destination.view().as_const()))
      << test_case.name;
  EXPECT_TRUE(other.active_matches(other_snapshot)) << test_case.name;
  EXPECT_TRUE(destination.memory_intact()) << test_case.name;
  EXPECT_TRUE(other.memory_intact()) << test_case.name;
  EXPECT_TRUE(expected.memory_intact()) << test_case.name;
}

inline void run_average_plane_case(const AveragePlaneCase& test_case) {
  switch (test_case.sample) {
    case AverageSample::UInt8:
      run_average_integer_case<std::uint8_t>(test_case);
      break;
    case AverageSample::UInt16:
      run_average_integer_case<std::uint16_t>(test_case);
      break;
    case AverageSample::Float:
      run_average_float_case(test_case);
      break;
  }
}

}  // namespace avsut::test
