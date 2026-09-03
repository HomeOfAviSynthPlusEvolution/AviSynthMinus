#pragma once

#include "convert/intel/convert_rgb_sse.h"

#include "support/comparators.h"
#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"
#include "support/variant_registry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace avsut::test {

using PackedRgbConversionFunction = void (*)(const BYTE*, BYTE*, std::size_t, std::size_t,
                                             std::size_t, std::size_t);

enum class PackedRgbOutputPaddingPolicy {
  Preserve,
  AllowToSimdAlignment,
};

struct PackedRgbConversionCase {
  std::string operation;
  std::size_t bytes_per_component{};
  std::size_t source_components{};
  std::size_t destination_components{};
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<PackedRgbConversionFunction> variant;
  std::string expected_hash;
  PackedRgbOutputPaddingPolicy output_padding_policy{PackedRgbOutputPaddingPolicy::Preserve};
  std::string name;
};

template <typename Function>
inline std::string convert_rgb_variant_name(const Variant<Function>& variant) {
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

inline std::string packed_rgb_conversion_case_name(const PackedRgbConversionCase& test_case) {
  std::ostringstream stream;
  stream << test_case.operation << "_Width" << test_case.width_pixels << "_Height"
         << test_case.height << "_SrcPitch" << test_case.source_pitch << "_DstPitch"
         << test_case.destination_pitch << "_PatternChannelRamp_"
         << convert_rgb_variant_name(test_case.variant);
  return stream.str();
}

inline PackedRgbConversionCase make_packed_rgb_conversion_case(
    std::string operation, std::size_t bytes_per_component, std::size_t source_components,
    std::size_t destination_components, std::size_t width_pixels, std::size_t height,
    std::size_t source_pitch, std::size_t destination_pitch,
    Variant<PackedRgbConversionFunction> variant, std::string expected_hash,
    PackedRgbOutputPaddingPolicy output_padding_policy = PackedRgbOutputPaddingPolicy::Preserve) {
  PackedRgbConversionCase result{std::move(operation),  bytes_per_component,
                                 source_components,     destination_components,
                                 width_pixels,          height,
                                 source_pitch,          destination_pitch,
                                 std::move(variant),    std::move(expected_hash),
                                 output_padding_policy, {}};
  result.name = packed_rgb_conversion_case_name(result);
  return result;
}

inline void PrintTo(const PackedRgbConversionCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void fill_packed_rgb_input(PlaneView<T> view, std::size_t width_pixels, std::size_t components) {
  static_assert(std::is_integral_v<T>);
  constexpr std::array<std::uint32_t, 16> anchors{0U,      1U,      2U,      15U,    16U,  127U,
                                                  128U,    191U,    254U,    255U,   256U, 0x7fffU,
                                                  0x8000U, 0xff00U, 0xfffeU, 0xffffU};
  const auto max_value = static_cast<std::uint32_t>(std::numeric_limits<T>::max());
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      for (std::size_t channel = 0; channel < components; ++channel) {
        const auto anchor = anchors[(x * 5U + y * 7U + channel * 11U) % anchors.size()];
        const auto perturbation = static_cast<std::uint32_t>(x * 257U + y * 4099U + channel * 109U);
        view.row(y)[x * components + channel] = static_cast<T>((anchor + perturbation) & max_value);
      }
    }
  }
}

template <typename T>
void make_packed_rgb_conversion_reference(PlaneView<const T> source, PlaneView<T> destination,
                                          const PackedRgbConversionCase& test_case) {
  const auto max_value = std::numeric_limits<T>::max();
  for (std::size_t y = 0; y < test_case.height; ++y) {
    for (std::size_t x = 0; x < test_case.width_pixels; ++x) {
      for (std::size_t channel = 0; channel < 3; ++channel) {
        destination.row(y)[x * test_case.destination_components + channel] =
            source.row(y)[x * test_case.source_components + channel];
      }
      if (test_case.destination_components == 4) {
        destination.row(y)[x * test_case.destination_components + 3] = max_value;
      }
    }
  }
}

template <typename T>
void run_packed_rgb_conversion_case(const PackedRgbConversionCase& test_case) {
  static_assert(std::is_integral_v<T>);
  const auto source_width = test_case.width_pixels * test_case.source_components;
  const auto destination_width = test_case.width_pixels * test_case.destination_components;
  GuardedVideoBuffer<T> source(source_width, test_case.height, test_case.source_pitch, 64);
  GuardedVideoBuffer<T> expected(destination_width, test_case.height, test_case.destination_pitch,
                                 64);
  GuardedVideoBuffer<T> actual(destination_width, test_case.height, test_case.destination_pitch,
                               64);

  fill_packed_rgb_input(source.view(), test_case.width_pixels, test_case.source_components);
  const auto source_snapshot = source.snapshot_active();
  make_packed_rgb_conversion_reference(source.view().as_const(), expected.view(), test_case);

  test_case.variant.function(reinterpret_cast<const BYTE*>(source.view().data()),
                             reinterpret_cast<BYTE*>(actual.view().data()),
                             source.view().pitch_bytes(), actual.view().pitch_bytes(),
                             test_case.width_pixels, test_case.height);

  EXPECT_TRUE(compare_exact(expected.view().as_const(), actual.view().as_const()))
      << test_case.name << " reference mismatch for variant " << test_case.variant.name;
  EXPECT_EQ(format_hash(hash_active(expected.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch";
  EXPECT_TRUE(source.active_matches(source_snapshot))
      << test_case.name << " modified the source input";
  EXPECT_TRUE(source.memory_intact())
      << test_case.name << " source padding or guards were corrupted";
  EXPECT_TRUE(expected.memory_intact())
      << test_case.name << " reference padding or guards were corrupted";

  constexpr std::size_t kSimdAlignmentBytes = 16;
  const auto destination_active_row_bytes = destination_width * sizeof(T);
  const auto padding_to_simd_alignment =
      (kSimdAlignmentBytes - (destination_active_row_bytes % kSimdAlignmentBytes)) %
      kSimdAlignmentBytes;
  const auto output_padding_protected_from =
      test_case.output_padding_policy == PackedRgbOutputPaddingPolicy::AllowToSimdAlignment
          ? std::min(actual.view().pitch_bytes(),
                     destination_active_row_bytes + padding_to_simd_alignment)
          : destination_active_row_bytes;
  const bool permitted_tail_write =
      test_case.output_padding_policy == PackedRgbOutputPaddingPolicy::AllowToSimdAlignment &&
      actual.guards_intact() && !actual.padding_intact() &&
      actual.padding_intact_from(output_padding_protected_from);
  if (permitted_tail_write) {
    std::cout << "[INFO] " << test_case.name << " modified output padding within the permitted "
              << kSimdAlignmentBytes << "-byte SIMD tail"
              << " (active_row_bytes=" << destination_active_row_bytes
              << ", protected_from_byte=" << output_padding_protected_from << ")\n";
  }
  EXPECT_TRUE(actual.guards_intact())
      << test_case.name << " output allocation guards were corrupted";
  EXPECT_TRUE(actual.padding_intact_from(output_padding_protected_from))
      << test_case.name << " modified output padding after the permitted SIMD tail boundary "
      << output_padding_protected_from;
}

}  // namespace avsut::test
