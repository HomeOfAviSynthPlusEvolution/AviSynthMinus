#pragma once

#include "focus_test_helpers.h"

#include <array>

namespace avsut::test {

using FocusRgb32FuncPtr = void (*)(BYTE*, const BYTE*, std::size_t, std::size_t, std::size_t,
                                   std::size_t, std::size_t);
using FocusRgb64FuncPtr = FocusRgb32FuncPtr;
using FocusYuy2FuncPtr = FocusRgb32FuncPtr;

struct FocusRgb32Case {
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  std::size_t amount{};
  Variant<FocusRgb32FuncPtr> variant;
  std::string expected_hash;
  std::string name;
};

struct FocusRgb64Case {
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  std::size_t amount{};
  Variant<FocusRgb64FuncPtr> variant;
  std::string expected_hash;
  std::string name;
};

struct FocusYuy2Case {
  std::size_t width_pixels{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  std::size_t amount{};
  Variant<FocusYuy2FuncPtr> variant;
  std::string expected_hash;
  std::string name;
};

template <typename Function>
inline std::string focus_packed_case_name(const char* format, std::size_t width_pixels,
                                          std::size_t height, std::size_t source_pitch,
                                          std::size_t destination_pitch, std::size_t amount,
                                          const Variant<Function>& variant) {
  std::ostringstream stream;
  stream << format << "_Width" << width_pixels << "_Height" << height << "_SrcPitch" << source_pitch
         << "_DstPitch" << destination_pitch << "_Amount" << amount << "_PatternBoundaryRamp_"
         << focus_variant_name(variant);
  return stream.str();
}

inline FocusRgb32Case make_focus_rgb32_case(std::size_t width_pixels, std::size_t height,
                                            std::size_t source_pitch, std::size_t destination_pitch,
                                            std::size_t amount, Variant<FocusRgb32FuncPtr> variant,
                                            std::string expected_hash) {
  FocusRgb32Case result{width_pixels,
                        height,
                        source_pitch,
                        destination_pitch,
                        amount,
                        std::move(variant),
                        std::move(expected_hash),
                        {}};
  result.name = focus_packed_case_name("Rgb32Horizontal", result.width_pixels, result.height,
                                       result.source_pitch, result.destination_pitch, result.amount,
                                       result.variant);
  return result;
}

inline FocusRgb64Case make_focus_rgb64_case(std::size_t width_pixels, std::size_t height,
                                            std::size_t source_pitch, std::size_t destination_pitch,
                                            std::size_t amount, Variant<FocusRgb64FuncPtr> variant,
                                            std::string expected_hash) {
  FocusRgb64Case result{width_pixels,
                        height,
                        source_pitch,
                        destination_pitch,
                        amount,
                        std::move(variant),
                        std::move(expected_hash),
                        {}};
  result.name = focus_packed_case_name("Rgb64Horizontal", result.width_pixels, result.height,
                                       result.source_pitch, result.destination_pitch, result.amount,
                                       result.variant);
  return result;
}

inline FocusYuy2Case make_focus_yuy2_case(std::size_t width_pixels, std::size_t height,
                                          std::size_t source_pitch, std::size_t destination_pitch,
                                          std::size_t amount, Variant<FocusYuy2FuncPtr> variant,
                                          std::string expected_hash) {
  FocusYuy2Case result{width_pixels,
                       height,
                       source_pitch,
                       destination_pitch,
                       amount,
                       std::move(variant),
                       std::move(expected_hash),
                       {}};
  result.name = focus_packed_case_name("Yuy2Horizontal", result.width_pixels, result.height,
                                       result.source_pitch, result.destination_pitch, result.amount,
                                       result.variant);
  return result;
}

inline void PrintTo(const FocusRgb32Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const FocusRgb64Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline void PrintTo(const FocusYuy2Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

template <typename T>
void apply_focus_rgb_reference(PlaneView<const T> source, PlaneView<T> destination,
                               std::size_t width_pixels, std::size_t amount) {
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto* left = source.row(y) + (x == 0 ? 0 : x - 1) * 4;
      const auto* center = source.row(y) + x * 4;
      const auto* right = source.row(y) + (x + 1 == width_pixels ? x : x + 1) * 4;
      for (std::size_t channel = 0; channel < 4; ++channel) {
        destination.row(y)[x * 4 + channel] =
            focus_reference_pixel(left[channel], center[channel], right[channel], amount);
      }
    }
  }
}

template <typename T>
void run_focus_rgb_case(const FocusRgb32Case& test_case, FocusRgb32FuncPtr function) {
  const auto width = test_case.width_pixels * 4;
  GuardedVideoBuffer<T> source(width, test_case.height, test_case.source_pitch, 32);
  GuardedVideoBuffer<T> expected(width, test_case.height, test_case.destination_pitch, 32);
  GuardedVideoBuffer<T> actual(width, test_case.height, test_case.destination_pitch, 32);
  fill_focus_input(source.view());
  const auto source_snapshot = source.snapshot_active();
  apply_focus_rgb_reference(source.view().as_const(), expected.view(), test_case.width_pixels,
                            test_case.amount);

  function(reinterpret_cast<BYTE*>(actual.view().data()),
           reinterpret_cast<const BYTE*>(source.view().data()), test_case.destination_pitch,
           test_case.source_pitch, test_case.height, test_case.width_pixels, test_case.amount);

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
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
}

inline void fill_focus_yuy2_input(PlaneView<std::uint8_t> view) {
  const auto groups = view.width() / 4;
  constexpr std::array<std::uint8_t, 8> values{0, 1, 16, 64, 128, 192, 254, 255};
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t group = 0; group < groups; ++group) {
      const auto index = y * groups + group;
      auto* row = view.row(y) + group * 4;
      row[0] = values[(index * 3U) % values.size()];
      row[1] = values[(index * 5U + 1U) % values.size()];
      row[2] = values[(index * 7U + 2U) % values.size()];
      row[3] = values[(index * 11U + 3U) % values.size()];
    }
  }
}

inline void apply_focus_yuy2_reference(PlaneView<const std::uint8_t> source,
                                       PlaneView<std::uint8_t> destination,
                                       std::size_t width_pixels, std::size_t amount) {
  const auto groups = width_pixels / 2;
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < width_pixels; ++x) {
      const auto luma_at = [&](std::size_t pixel) { return source.row(y)[pixel * 2]; };
      const auto left = luma_at(x == 0 ? 0 : x - 1);
      const auto center = luma_at(x);
      const auto right = luma_at(x + 1 == width_pixels ? x : x + 1);
      destination.row(y)[x * 2] = focus_reference_pixel(left, center, right, amount);
    }
    for (std::size_t group = 0; group < groups; ++group) {
      const auto* left = source.row(y) + (group == 0 ? 0 : group - 1) * 4;
      const auto* center = source.row(y) + group * 4;
      const auto* right = source.row(y) + (group + 1 == groups ? group : group + 1) * 4;
      destination.row(y)[group * 4 + 1] =
          focus_reference_pixel(left[1], center[1], right[1], amount);
      destination.row(y)[group * 4 + 3] =
          focus_reference_pixel(left[3], center[3], right[3], amount);
    }
  }
}

inline void run_focus_yuy2_case(const FocusYuy2Case& test_case) {
  const auto width_bytes = test_case.width_pixels * 2;
  GuardedVideoBuffer<std::uint8_t> source(width_bytes, test_case.height, test_case.source_pitch,
                                          32);
  GuardedVideoBuffer<std::uint8_t> expected(width_bytes, test_case.height,
                                            test_case.destination_pitch, 32);
  GuardedVideoBuffer<std::uint8_t> actual(width_bytes, test_case.height,
                                          test_case.destination_pitch, 32);
  fill_focus_yuy2_input(source.view());
  const auto source_snapshot = source.snapshot_active();
  apply_focus_yuy2_reference(source.view().as_const(), expected.view(), test_case.width_pixels,
                             test_case.amount);

  test_case.variant.function(actual.view().data(), source.view().data(),
                             test_case.destination_pitch, test_case.source_pitch, test_case.height,
                             test_case.width_pixels, test_case.amount);

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
  EXPECT_TRUE(actual.memory_intact())
      << test_case.name << " output padding or guards were corrupted";
}

}  // namespace avsut::test
