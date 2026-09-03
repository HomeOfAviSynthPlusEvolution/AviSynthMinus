#pragma once

#include "filters/conditional/intel/conditional_functions_sse.h"
#include "filters/intel/focus_sse.h"

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
#include <string>
#include <type_traits>
#include <utility>

namespace avsut::test {

using SumFunction = double (*)(const std::uint8_t*, std::size_t, std::size_t, std::size_t);
using SadIntFunction = int (*)(const BYTE*, const BYTE*, int, int, std::size_t, std::size_t);
using SadWideFunction = std::int64_t (*)(const BYTE*, const BYTE*, int, int, std::size_t,
                                         std::size_t);

struct SumCase {
  std::size_t width_bytes{};
  std::size_t height{};
  std::size_t pitch_bytes{};
  Variant<SumFunction> variant;
  std::uint32_t seed{};
  std::string name;
};

struct SadIntCase {
  std::string format;
  bool packed_rgb{};
  std::size_t width_bytes{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t other_pitch{};
  Variant<SadIntFunction> variant;
  std::uint32_t seed{};
  std::string name;
};

struct SadWideCase {
  std::string format;
  std::size_t bytes_per_sample{};
  bool packed_rgb{};
  std::size_t width_samples{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t other_pitch{};
  Variant<SadWideFunction> variant;
  std::uint32_t seed{};
  std::string name;
};

template <typename Function>
std::string conditional_variant_name(const Variant<Function>& variant) {
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

inline std::string sum_case_name(std::size_t width_bytes, std::size_t height,
                                 std::size_t pitch_bytes, const Variant<SumFunction>& variant,
                                 std::uint32_t seed) {
  std::ostringstream stream;
  stream << "Plane8_WidthBytes" << width_bytes << "_Height" << height << "_Pitch" << pitch_bytes;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternBoundaryRamp_" : "_PatternFixedRandom_")
         << conditional_variant_name(variant);
  return stream.str();
}

inline std::string sad_int_case_name(const std::string& format, std::size_t width_bytes,
                                     std::size_t height, std::size_t source_pitch,
                                     std::size_t other_pitch,
                                     const Variant<SadIntFunction>& variant,
                                     std::uint32_t seed) {
  std::ostringstream stream;
  stream << format << "_WidthBytes" << width_bytes << "_Height" << height << "_SrcPitch"
         << source_pitch << "_OtherPitch" << other_pitch;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternBoundaryRamp_" : "_PatternFixedRandom_")
         << conditional_variant_name(variant);
  return stream.str();
}

inline std::string sad_wide_case_name(const std::string& format, std::size_t width_samples,
                                      std::size_t height, std::size_t source_pitch,
                                      std::size_t other_pitch,
                                      const Variant<SadWideFunction>& variant,
                                      std::uint32_t seed) {
  std::ostringstream stream;
  stream << format << "_WidthSamples" << width_samples << "_Height" << height << "_SrcPitch"
         << source_pitch << "_OtherPitch" << other_pitch;
  if (seed != 0) {
    stream << "_Seed" << std::uppercase << std::hex << seed;
  }
  stream << (seed == 0 ? "_PatternBoundaryRamp_" : "_PatternFixedRandom_")
         << conditional_variant_name(variant);
  return stream.str();
}

inline SumCase make_sum_case(std::size_t width_bytes, std::size_t height, std::size_t pitch_bytes,
                             Variant<SumFunction> variant, std::uint32_t seed = 0) {
  SumCase result{width_bytes, height, pitch_bytes, std::move(variant), seed, {}};
  result.name =
      sum_case_name(result.width_bytes, result.height, result.pitch_bytes, result.variant,
                    result.seed);
  return result;
}

inline SadIntCase make_sad_int_case(std::string format, bool packed_rgb, std::size_t width_bytes,
                                    std::size_t height, std::size_t source_pitch,
                                    std::size_t other_pitch, Variant<SadIntFunction> variant,
                                    std::uint32_t seed = 0) {
  SadIntCase result{std::move(format), packed_rgb,  width_bytes,        height,
                    source_pitch,      other_pitch, std::move(variant), seed, {}};
  result.name = sad_int_case_name(result.format, result.width_bytes, result.height,
                                  result.source_pitch, result.other_pitch, result.variant,
                                  result.seed);
  return result;
}

inline SadWideCase make_sad_wide_case(std::string format, std::size_t bytes_per_sample,
                                      bool packed_rgb, std::size_t width_samples,
                                      std::size_t height, std::size_t source_pitch,
                                      std::size_t other_pitch, Variant<SadWideFunction> variant,
                                      std::uint32_t seed = 0) {
  SadWideCase result{
      std::move(format), bytes_per_sample,   packed_rgb, width_samples, height, source_pitch,
      other_pitch,       std::move(variant), seed, {}};
  result.name = sad_wide_case_name(result.format, result.width_samples, result.height,
                                   result.source_pitch, result.other_pitch, result.variant,
                                   result.seed);
  return result;
}

inline void PrintTo(const SumCase& test_case, std::ostream* stream) { *stream << test_case.name; }

inline void PrintTo(const SadIntCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const SadWideCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void fill_sum_input(PlaneView<std::uint8_t> view, std::uint32_t seed = 0) {
  if (seed != 0) {
    XorShift32 generator(seed);
    for (std::size_t y = 0; y < view.height(); ++y) {
      for (std::size_t x = 0; x < view.width(); ++x) {
        view.row(y)[x] = static_cast<std::uint8_t>(generator.next());
      }
    }
    return;
  }

  constexpr std::array<std::uint8_t, 11> anchors{0U,   1U,   17U,  31U,  63U, 127U,
                                                  191U, 223U, 254U, 255U, 42U};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      const auto index = y * view.width() + x;
      view.row(y)[x] = anchors[(index + y * 3U) % anchors.size()];
    }
  }
}

template <typename T>
void fill_sad_inputs(PlaneView<T> source, PlaneView<T> other, bool packed_rgb,
                     std::size_t bytes_per_sample, std::uint32_t seed = 0) {
  static_assert(!std::is_const_v<T>);
  ASSERT_EQ(source.width(), other.width());
  ASSERT_EQ(source.height(), other.height());
  const auto max_value = std::numeric_limits<T>::max();

  if (seed != 0) {
    XorShift32 generator(seed);
    for (std::size_t y = 0; y < source.height(); ++y) {
      for (std::size_t x = 0; x < source.width(); ++x) {
        source.row(y)[x] = static_cast<T>(generator.next());
        other.row(y)[x] = static_cast<T>(generator.next());
        if (packed_rgb && (x % 4U) == 3U) {
          source.row(y)[x] = max_value;
          other.row(y)[x] = static_cast<T>(0U);
        }
      }
    }
    return;
  }

  constexpr std::array<std::uint32_t, 9> anchors{0U, 1U, 7U, 31U, 63U, 127U, 191U, 251U, 255U};
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < source.width(); ++x) {
      const auto index = y * source.width() + x;
      const auto base = static_cast<std::uint64_t>(anchors[(index + y * 2U) % anchors.size()]);
      const auto first =
          (base * 257U + x * 13U + y * 29U) % (static_cast<std::uint64_t>(max_value) + 1U);
      const auto delta = static_cast<std::uint64_t>((x * 5U + y * 11U) % 37U);
      source.row(y)[x] = static_cast<T>(first);
      other.row(y)[x] =
          static_cast<T>((first + delta) % (static_cast<std::uint64_t>(max_value) + 1U));
      if (packed_rgb && (x % 4U) == 3U) {
        source.row(y)[x] = static_cast<T>(max_value);
        other.row(y)[x] = static_cast<T>(0U);
      }
    }
  }
  (void)bytes_per_sample;
}

template <typename T>
std::int64_t sad_reference(PlaneView<const T> source, PlaneView<const T> other, bool packed_rgb) {
  std::int64_t result = 0;
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < source.width(); ++x) {
      if (packed_rgb && (x % 4U) == 3U) {
        continue;
      }
      result += std::llabs(static_cast<long long>(source.row(y)[x]) -
                           static_cast<long long>(other.row(y)[x]));
    }
  }
  return result;
}

inline void run_sum_case(const SumCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source(test_case.width_bytes, test_case.height,
                                          test_case.pitch_bytes, 64);
  fill_sum_input(source.view(), test_case.seed);
  const auto snapshot = source.snapshot_active();
  std::uint64_t expected = 0;
  for (std::size_t y = 0; y < source.view().height(); ++y) {
    for (std::size_t x = 0; x < source.view().width(); ++x) {
      expected += source.view().row(y)[x];
    }
  }

  const double actual =
      test_case.variant.function(source.view().data(), source.view().height(),
                                 source.view().width(), source.view().pitch_bytes());
  EXPECT_EQ(actual, static_cast<double>(expected))
      << test_case.name << " sum mismatch for variant " << test_case.variant.name;
  EXPECT_TRUE(source.active_matches(snapshot)) << test_case.name << " modified source pixels";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " corrupted source padding or guards";
}

inline void run_sad_int_case(const SadIntCase& test_case) {
  GuardedVideoBuffer<std::uint8_t> source(test_case.width_bytes, test_case.height,
                                          test_case.source_pitch, 64);
  GuardedVideoBuffer<std::uint8_t> other(test_case.width_bytes, test_case.height,
                                         test_case.other_pitch, 64);
  fill_sad_inputs(source.view(), other.view(), test_case.packed_rgb, 1, test_case.seed);
  const auto source_snapshot = source.snapshot_active();
  const auto other_snapshot = other.snapshot_active();
  const auto expected =
      sad_reference(source.view().as_const(), other.view().as_const(), test_case.packed_rgb);

  const int actual = test_case.variant.function(reinterpret_cast<const BYTE*>(source.view().data()),
                                                reinterpret_cast<const BYTE*>(other.view().data()),
                                                static_cast<int>(source.view().pitch_bytes()),
                                                static_cast<int>(other.view().pitch_bytes()),
                                                test_case.width_bytes, test_case.height);
  EXPECT_EQ(actual, expected) << test_case.name << " SAD mismatch for variant "
                              << test_case.variant.name;
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << test_case.name << " modified source pixels";
  EXPECT_TRUE(other.active_matches(other_snapshot)) << test_case.name << " modified other pixels";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " corrupted source padding or guards";
  EXPECT_TRUE(other.memory_intact()) << test_case.name << " corrupted other padding or guards";
}

template <typename T>
void run_sad_wide_case(const SadWideCase& test_case) {
  const auto source_width = test_case.width_samples;
  GuardedVideoBuffer<T> source(source_width, test_case.height, test_case.source_pitch, 64);
  GuardedVideoBuffer<T> other(source_width, test_case.height, test_case.other_pitch, 64);
  fill_sad_inputs(source.view(), other.view(), test_case.packed_rgb,
                  test_case.bytes_per_sample, test_case.seed);
  const auto source_snapshot = source.snapshot_active();
  const auto other_snapshot = other.snapshot_active();
  const auto expected =
      sad_reference(source.view().as_const(), other.view().as_const(), test_case.packed_rgb);
  const auto rowsize = source_width * sizeof(T);

  const auto actual = test_case.variant.function(
      reinterpret_cast<const BYTE*>(source.view().data()),
      reinterpret_cast<const BYTE*>(other.view().data()),
      static_cast<int>(source.view().pitch_bytes()), static_cast<int>(other.view().pitch_bytes()),
      rowsize, test_case.height);
  EXPECT_EQ(actual, expected) << test_case.name << " SAD mismatch for variant "
                              << test_case.variant.name;
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << test_case.name << " modified source pixels";
  EXPECT_TRUE(other.active_matches(other_snapshot)) << test_case.name << " modified other pixels";
  EXPECT_TRUE(source.memory_intact()) << test_case.name << " corrupted source padding or guards";
  EXPECT_TRUE(other.memory_intact()) << test_case.name << " corrupted other padding or guards";
}

}  // namespace avsut::test
