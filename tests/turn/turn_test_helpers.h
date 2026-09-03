#pragma once

#include "filters/turn.h"

#include "support/cpu_features.h"
#include "support/deterministic_data.h"
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

enum class TurnDirection { Left, Right, Half };

struct TurnCase {
  std::string format;
  TurnDirection direction;
  bool reverse_quarter_turn{};
  std::size_t width_pixels{};
  std::size_t height_pixels{};
  std::size_t bytes_per_pixel{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  std::uint32_t seed{};
  TurnFuncPtr scalar_function{};
  Variant<TurnFuncPtr> variant;
  std::string expected_hash;
  std::string name;
};

inline void PrintTo(const TurnCase& test_case, std::ostream* stream) { *stream << test_case.name; }

inline std::string turn_direction_name(TurnDirection direction) {
  switch (direction) {
    case TurnDirection::Left:
      return "Left";
    case TurnDirection::Right:
      return "Right";
    case TurnDirection::Half:
      return "Half";
  }
  return "Unknown";
}

inline std::string turn_variant_name(const Variant<TurnFuncPtr>& variant) {
  std::string result = "Variant";
  bool capitalize = true;
  for (const char character : variant.name) {
    if (character == '_' || character == '-') {
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

inline std::string turn_case_name(const std::string& format, TurnDirection direction,
                                  std::size_t width_pixels, std::size_t height_pixels,
                                  std::size_t source_pitch, std::size_t destination_pitch,
                                  std::uint32_t seed, const Variant<TurnFuncPtr>& variant) {
  std::ostringstream stream;
  stream << format << turn_direction_name(direction) << "_Width" << width_pixels << "_Height"
         << height_pixels << "_SrcPitch" << source_pitch << "_DstPitch" << destination_pitch
         << "_Seed" << std::uppercase << std::hex << seed << "_" << turn_variant_name(variant);
  return stream.str();
}

inline TurnCase make_turn_case(std::string format, TurnDirection direction,
                               std::size_t width_pixels, std::size_t height_pixels,
                               std::size_t bytes_per_pixel, std::size_t source_pitch,
                               std::size_t destination_pitch, std::uint32_t seed,
                               TurnFuncPtr scalar_function, Variant<TurnFuncPtr> variant,
                               std::string expected_hash = {}, bool reverse_quarter_turn = false) {
  TurnCase result{std::move(format),
                  direction,
                  reverse_quarter_turn,
                  width_pixels,
                  height_pixels,
                  bytes_per_pixel,
                  source_pitch,
                  destination_pitch,
                  seed,
                  scalar_function,
                  std::move(variant),
                  std::move(expected_hash),
                  {}};
  result.name =
      turn_case_name(result.format, result.direction, result.width_pixels, result.height_pixels,
                     result.source_pitch, result.destination_pitch, result.seed, result.variant);
  return result;
}

inline std::size_t destination_width_pixels(const TurnCase& test_case) {
  return test_case.direction == TurnDirection::Half ? test_case.width_pixels
                                                    : test_case.height_pixels;
}

inline std::size_t destination_height_pixels(const TurnCase& test_case) {
  return test_case.direction == TurnDirection::Half ? test_case.height_pixels
                                                    : test_case.width_pixels;
}

inline std::size_t source_row_bytes(const TurnCase& test_case) {
  return test_case.width_pixels * test_case.bytes_per_pixel;
}

inline std::size_t destination_row_bytes(const TurnCase& test_case) {
  return destination_width_pixels(test_case) * test_case.bytes_per_pixel;
}

inline void map_turn_reference(const TurnCase& test_case, PlaneView<const std::uint8_t> source,
                               PlaneView<std::uint8_t> destination) {
  if (source.width() != source_row_bytes(test_case) || source.height() != test_case.height_pixels ||
      destination.width() != destination_row_bytes(test_case) ||
      destination.height() != destination_height_pixels(test_case)) {
    throw std::invalid_argument("Turn reference dimensions do not match case");
  }

  auto reference_direction = test_case.direction;
  if (test_case.reverse_quarter_turn) {
    if (reference_direction == TurnDirection::Left) {
      reference_direction = TurnDirection::Right;
    } else if (reference_direction == TurnDirection::Right) {
      reference_direction = TurnDirection::Left;
    }
  }

  for (std::size_t source_y = 0; source_y < test_case.height_pixels; ++source_y) {
    for (std::size_t source_x = 0; source_x < test_case.width_pixels; ++source_x) {
      std::size_t destination_x = 0;
      std::size_t destination_y = 0;
      switch (reference_direction) {
        case TurnDirection::Left:
          destination_x = source_y;
          destination_y = test_case.width_pixels - 1 - source_x;
          break;
        case TurnDirection::Right:
          destination_x = test_case.height_pixels - 1 - source_y;
          destination_y = source_x;
          break;
        case TurnDirection::Half:
          destination_x = test_case.width_pixels - 1 - source_x;
          destination_y = test_case.height_pixels - 1 - source_y;
          break;
      }

      const auto* source_pixel = source.row(source_y) + source_x * test_case.bytes_per_pixel;
      auto* destination_pixel =
          destination.row(destination_y) + destination_x * test_case.bytes_per_pixel;
      std::copy_n(source_pixel, test_case.bytes_per_pixel, destination_pixel);
    }
  }
}

inline ::testing::AssertionResult compare_turn_pixels(const TurnCase& test_case,
                                                      PlaneView<const std::uint8_t> expected,
                                                      PlaneView<const std::uint8_t> actual) {
  if (expected.width() != actual.width() || expected.height() != actual.height()) {
    return ::testing::AssertionFailure() << "dimension mismatch";
  }

  for (std::size_t y = 0; y < expected.height(); ++y) {
    for (std::size_t x = 0; x < expected.width(); ++x) {
      if (expected.row(y)[x] != actual.row(y)[x]) {
        const std::size_t pixel_column = x / test_case.bytes_per_pixel;
        const std::size_t channel_byte = x % test_case.bytes_per_pixel;
        return ::testing::AssertionFailure()
               << "format=" << test_case.format
               << " direction=" << turn_direction_name(test_case.direction)
               << " variant=" << test_case.variant.name << " width=" << test_case.width_pixels
               << " height=" << test_case.height_pixels << " src_pitch=" << test_case.source_pitch
               << " dst_pitch=" << test_case.destination_pitch << " seed=0x" << std::uppercase
               << std::hex << test_case.seed << std::dec << " row=" << y
               << " pixel=" << pixel_column << " channel_byte=" << channel_byte
               << " expected=" << +expected.row(y)[x] << " actual=" << +actual.row(y)[x];
      }
    }
  }
  return ::testing::AssertionSuccess();
}

inline void invoke_turn(TurnFuncPtr function, PlaneView<const std::uint8_t> source,
                        PlaneView<std::uint8_t> destination) {
  function(source.data(), destination.data(), static_cast<int>(source.active_row_bytes()),
           static_cast<int>(source.height()), static_cast<int>(source.pitch_bytes()),
           static_cast<int>(destination.pitch_bytes()));
}

inline void run_turn_case(const TurnCase& test_case) {
  const auto source_row_size = source_row_bytes(test_case);
  const auto destination_row_size = destination_row_bytes(test_case);
  GuardedVideoBuffer<std::uint8_t> source(source_row_size, test_case.height_pixels,
                                          test_case.source_pitch, 32);
  GuardedVideoBuffer<std::uint8_t> expected(
      destination_row_size, destination_height_pixels(test_case), test_case.destination_pitch, 32);
  GuardedVideoBuffer<std::uint8_t> scalar(
      destination_row_size, destination_height_pixels(test_case), test_case.destination_pitch, 32);
  GuardedVideoBuffer<std::uint8_t> actual(
      destination_row_size, destination_height_pixels(test_case), test_case.destination_pitch, 32);

  fill_random(source.view(), test_case.seed);
  const auto source_snapshot = source.snapshot_active();
  map_turn_reference(test_case, source.view().as_const(), expected.view());

  invoke_turn(test_case.scalar_function, source.view().as_const(), scalar.view());
  invoke_turn(test_case.variant.function, source.view().as_const(), actual.view());

  EXPECT_TRUE(compare_turn_pixels(test_case, expected.view().as_const(), scalar.view().as_const()));
  EXPECT_TRUE(compare_turn_pixels(test_case, scalar.view().as_const(), actual.view().as_const()));
  if (!test_case.expected_hash.empty()) {
    EXPECT_EQ(format_hash(hash_active(scalar.view().as_const())), test_case.expected_hash);
  }
  EXPECT_TRUE(source.active_matches(source_snapshot));
  EXPECT_TRUE(source.memory_intact());
  EXPECT_TRUE(expected.memory_intact());
  EXPECT_TRUE(scalar.memory_intact());
  EXPECT_TRUE(actual.memory_intact());
}

}  // namespace avsut::test
