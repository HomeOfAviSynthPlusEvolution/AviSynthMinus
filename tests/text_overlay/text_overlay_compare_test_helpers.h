#pragma once

#include "filters/intel/text-overlay_sse.h"

#include "support/guarded_video_buffer.h"
#include "support/variant_registry.h"
#include "support/deterministic_data.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace avsut::test {

using TextOverlayCompareFunction = void (*)(std::uint32_t, int, const BYTE*, int, const BYTE*, int,
                                            int, int, int&, int&, int&, int&, double&);

struct TextOverlayCompareCase {
  std::string format;
  std::string mask_name;
  std::uint32_t mask{};
  int increment{};
  std::size_t rowsize_bytes{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t other_pitch{};
  std::size_t source_alignment_offset{};
  std::size_t other_alignment_offset{};
  int initial_sad{};
  int initial_sd{};
  int initial_pos{};
  int initial_neg{};
  double initial_ssd{};
  Variant<TextOverlayCompareFunction> variant;
  std::uint32_t seed{};
  std::string name;
};

inline std::string text_overlay_compare_variant_name(
    const Variant<TextOverlayCompareFunction>& variant) {
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

inline std::string text_overlay_compare_case_name(const TextOverlayCompareCase& test_case) {
  std::ostringstream stream;
  stream << test_case.format << "_Mask" << test_case.mask_name << "_Increment"
         << test_case.increment << "_RowBytes" << test_case.rowsize_bytes << "_Height"
         << test_case.height << "_SrcPitch" << test_case.source_pitch << "_OtherPitch"
         << test_case.other_pitch << "_SrcOffset" << test_case.source_alignment_offset
         << "_OtherOffset" << test_case.other_alignment_offset;
  if (test_case.seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << test_case.seed;
  }
  stream << (test_case.seed == 0 ? "_PatternBoundaryAnchors_" : "_PatternFixedRandom_")
         << text_overlay_compare_variant_name(test_case.variant);
  return stream.str();
}

inline TextOverlayCompareCase make_text_overlay_compare_case(
    std::string format, std::string mask_name, std::uint32_t mask, int increment,
    std::size_t rowsize_bytes, std::size_t height, std::size_t source_pitch,
    std::size_t other_pitch, std::size_t source_alignment_offset,
    std::size_t other_alignment_offset, int initial_sad, int initial_sd, int initial_pos,
    int initial_neg, double initial_ssd, Variant<TextOverlayCompareFunction> variant,
    std::uint32_t seed = 0) {
  if (format.empty() || mask_name.empty() || (increment != 3 && increment != 4) ||
      rowsize_bytes == 0 || height == 0 || rowsize_bytes % (increment * 4) != 0 ||
      source_pitch < rowsize_bytes || other_pitch < rowsize_bytes ||
      source_alignment_offset >= 32 || other_alignment_offset >= 32 || initial_sad < 0 ||
      initial_pos < 0 || initial_neg > 0 || initial_ssd < 0.0) {
    throw std::invalid_argument("invalid text-overlay compare dimensions or parameters");
  }
  TextOverlayCompareCase result{std::move(format),
                                std::move(mask_name),
                                mask,
                                increment,
                                rowsize_bytes,
                                height,
                                source_pitch,
                                other_pitch,
                                source_alignment_offset,
                                other_alignment_offset,
                                initial_sad,
                                initial_sd,
                                initial_pos,
                                initial_neg,
                                initial_ssd,
                                std::move(variant),
                                seed,
                                {}};
  result.name = text_overlay_compare_case_name(result);
  return result;
}

inline void PrintTo(const TextOverlayCompareCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_text_overlay_compare_inputs(const TextOverlayCompareCase& test_case,
                                             PlaneView<std::uint8_t> source,
                                             PlaneView<std::uint8_t> other) {
  if (test_case.seed != 0) {
    XorShift32 generator(test_case.seed);
    for (std::size_t y = 0; y < test_case.height; ++y) {
      for (std::size_t x = 0; x < test_case.rowsize_bytes; ++x) {
        source.row(y)[x] = static_cast<std::uint8_t>(generator.next());
        other.row(y)[x] = static_cast<std::uint8_t>(generator.next());
      }
    }
    return;
  }

  constexpr std::array<std::uint8_t, 16> anchors{0U,   1U,   2U,   17U,  31U,  63U,  64U,  95U,
                                                 127U, 128U, 159U, 191U, 223U, 254U, 255U, 42U};
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.rowsize_bytes; ++x) {
      source.row(y)[x] = anchors[(x + 5 * y + 1) % anchors.size()];
      other.row(y)[x] = anchors[(3 * x + 7 * y + 4) % anchors.size()];
    }
  }

  // Force both signed directions and the maximum byte delta on every selected lane.
  for (int lane = 0; lane < test_case.increment; ++lane) {
    const auto lane_mask = static_cast<std::uint8_t>(test_case.mask >> (lane * 8));
    if (lane_mask == 0) {
      continue;
    }
    source.row(0)[static_cast<std::size_t>(lane)] = std::numeric_limits<std::uint8_t>::max();
    other.row(0)[static_cast<std::size_t>(lane)] = 0U;
    source.row(0)[static_cast<std::size_t>(test_case.increment + lane)] = 0U;
    other.row(0)[static_cast<std::size_t>(test_case.increment + lane)] =
        std::numeric_limits<std::uint8_t>::max();
  }
}

struct TextOverlayCompareMetrics {
  std::int64_t sad{};
  std::int64_t sd{};
  int pos{};
  int neg{};
  double ssd{};
};

inline TextOverlayCompareMetrics text_overlay_compare_reference(
    const TextOverlayCompareCase& test_case, PlaneView<const std::uint8_t> source,
    PlaneView<const std::uint8_t> other) {
  TextOverlayCompareMetrics result{test_case.initial_sad, test_case.initial_sd,
                                   test_case.initial_pos, test_case.initial_neg,
                                   test_case.initial_ssd};
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.rowsize_bytes; ++x) {
      const auto lane = x % static_cast<std::size_t>(test_case.increment);
      const auto lane_mask = static_cast<std::uint8_t>(test_case.mask >> (lane * 8));
      const auto first = static_cast<int>(source.row(y)[x] & lane_mask);
      const auto second = static_cast<int>(other.row(y)[x] & lane_mask);
      const auto difference = first - second;
      result.sad += std::abs(difference);
      result.sd += difference;
      result.pos = std::max(result.pos, difference);
      result.neg = std::min(result.neg, difference);
      result.ssd += static_cast<double>(difference * difference);
    }
  }
  return result;
}

inline void run_text_overlay_compare_case(const TextOverlayCompareCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source(test_case.rowsize_bytes, test_case.height,
                                          test_case.source_pitch, 32,
                                          test_case.source_alignment_offset);
  GuardedVideoBuffer<std::uint8_t> other(test_case.rowsize_bytes, test_case.height,
                                         test_case.other_pitch, 32,
                                         test_case.other_alignment_offset);
  fill_text_overlay_compare_inputs(test_case, source.view(), other.view());
  const auto source_snapshot = source.snapshot_active();
  const auto other_snapshot = other.snapshot_active();
  const auto expected =
      text_overlay_compare_reference(test_case, source.view().as_const(), other.view().as_const());

  int actual_sad = test_case.initial_sad;
  int actual_sd = test_case.initial_sd;
  int actual_pos = test_case.initial_pos;
  int actual_neg = test_case.initial_neg;
  double actual_ssd = test_case.initial_ssd;
  test_case.variant.function(
      test_case.mask, test_case.increment, reinterpret_cast<const BYTE*>(source.view().data()),
      static_cast<int>(test_case.source_pitch), reinterpret_cast<const BYTE*>(other.view().data()),
      static_cast<int>(test_case.other_pitch), static_cast<int>(test_case.rowsize_bytes),
      static_cast<int>(test_case.height), actual_sad, actual_sd, actual_pos, actual_neg,
      actual_ssd);

  EXPECT_EQ(actual_sad, expected.sad)
      << test_case.name << " SAD mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(actual_sd, expected.sd)
      << test_case.name << " signed-difference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(actual_pos, expected.pos)
      << test_case.name << " positive maximum mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(actual_neg, expected.neg)
      << test_case.name << " negative maximum mismatch for variant " << test_case.variant.name;
  EXPECT_DOUBLE_EQ(actual_ssd, expected.ssd)
      << test_case.name << " squared-difference mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << test_case.name << " modified source pixels";
  EXPECT_TRUE(other.active_matches(other_snapshot)) << test_case.name << " modified other pixels";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " corrupted source padding or guards";
  EXPECT_TRUE(other.memory_intact()) << test_case.name << " corrupted other padding or guards";
}

}  // namespace avsut::test
