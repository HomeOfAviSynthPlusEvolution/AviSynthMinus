#include <gtest/gtest.h>

#include "overlay_multiply_test_helpers.h"

#include "support/cpu_features.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace avsut::test {
namespace {

template <typename T, bool OpacityIsFull, bool HasMask>
Variant<OverlayMultiplyFunction> make_sse_variant() {
  return {"sse4.1", &overlay_multiply_sse_wrapper<T, OpacityIsFull, HasMask>,
          IsaRequirement::Sse41};
}

template <typename T, bool OpacityIsFull, bool HasMask>
Variant<OverlayMultiplyFunction> make_avx2_variant() {
  return {"avx2", &overlay_multiply_avx2_wrapper<T, OpacityIsFull, HasMask>, IsaRequirement::Avx2};
}

std::array<std::string, 3> overlay_multiply_expected_hashes(int bits_per_pixel, bool has_mask,
                                                            bool opacity_is_full) {
  if (bits_per_pixel == 8) {
    if (has_mask) {
      return opacity_is_full ? std::array<std::string, 3>{"52681713b47bb440", "66bce72c355fdfd4",
                                                          "2b3ca501208d4183"}
                             : std::array<std::string, 3>{"23fa58bf8050599a", "6926619635330f62",
                                                          "43284e32421df074"};
    }
    return opacity_is_full ? std::array<std::string, 3>{"d251722c9c00684a", "613f24ed35d85d4e",
                                                        "b1ee0170725041fa"}
                           : std::array<std::string, 3>{"523346b47ca72373", "6be6ccabaea8d228",
                                                        "835682001e129e31"};
  }
  if (bits_per_pixel == 10) {
    if (has_mask) {
      return opacity_is_full ? std::array<std::string, 3>{"b7fe6fc9122e68ba", "b7cd6c005f708315",
                                                          "6477a95ab1dcf82c"}
                             : std::array<std::string, 3>{"637da1ee0bc57326", "eb64e20f37cfe0c8",
                                                          "de1ab4a39226c161"};
    }
    return opacity_is_full ? std::array<std::string, 3>{"2bf60650a8506d8c", "dc57721c82071dac",
                                                        "59cd1b08c363a60f"}
                           : std::array<std::string, 3>{"8defdba9e6371b4d", "8e562cd1ee916775",
                                                        "116714c441c2548b"};
  }
  if (has_mask) {
    return opacity_is_full ? std::array<std::string, 3>{"0cd93288aacd33ef", "5d7c7cff00ea5fce",
                                                        "6fc6f29d0aed7a99"}
                           : std::array<std::string, 3>{"14cdca0da6519920", "ab90266063b171b8",
                                                        "e223150ede2c4e0e"};
  }
  return opacity_is_full ? std::array<std::string, 3>{"1b3dc8f270a12d85", "1e0be1e276b45220",
                                                      "68354bebd21b7aa7"}
                         : std::array<std::string, 3>{"c42cf37470af8b50", "a713d64d481a6f79",
                                                      "156f9bcc5b49dfc4"};
}

template <typename T, bool OpacityIsFull, bool HasMask>
void add_overlay_multiply_variants(std::vector<OverlayMultiplyCase>& cases, int bits_per_pixel,
                                   float opacity_f, int opacity, const char* opacity_label,
                                   std::uint32_t seed = 0,
                                   std::array<std::string, 3> random_expected_hashes = {}) {
  const std::size_t base_pitch = sizeof(T) == 1 ? 48 : 64;
  const std::size_t overlay_pitch = sizeof(T) == 1 ? 64 : 80;
  const std::size_t mask_pitch = sizeof(T) == 1 ? 48 : 64;
  constexpr std::size_t width = 11;
  constexpr std::size_t height = 3;
  const auto expected_hashes = seed == 0
                                   ? overlay_multiply_expected_hashes(bits_per_pixel, HasMask,
                                                                       OpacityIsFull)
                                   : std::move(random_expected_hashes);
  cases.push_back(make_overlay_multiply_case(
      bits_per_pixel, HasMask, OpacityIsFull, width, height, base_pitch, overlay_pitch, mask_pitch,
      4, 8, 12, opacity_f, opacity, opacity_label, make_sse_variant<T, OpacityIsFull, HasMask>(),
      expected_hashes, seed));
  cases.push_back(make_overlay_multiply_case(
      bits_per_pixel, HasMask, OpacityIsFull, width, height, base_pitch, overlay_pitch, mask_pitch,
      4, 8, 12, opacity_f, opacity, opacity_label, make_avx2_variant<T, OpacityIsFull, HasMask>(),
      expected_hashes, seed));
}

std::vector<OverlayMultiplyCase> overlay_multiply_cases() {
  std::vector<OverlayMultiplyCase> cases;
  for (const int bits_per_pixel : {8}) {
    add_overlay_multiply_variants<std::uint8_t, true, false>(cases, bits_per_pixel, 1.0F, 256,
                                                             "Full");
    add_overlay_multiply_variants<std::uint8_t, false, false>(cases, bits_per_pixel, 0.63F, 161,
                                                              "Partial63");
    add_overlay_multiply_variants<std::uint8_t, true, true>(cases, bits_per_pixel, 1.0F, 256,
                                                            "Full");
    add_overlay_multiply_variants<std::uint8_t, false, true>(cases, bits_per_pixel, 0.63F, 161,
                                                             "Partial63");
  }
  for (const int bits_per_pixel : {10, 16}) {
    add_overlay_multiply_variants<std::uint16_t, true, false>(cases, bits_per_pixel, 1.0F, 256,
                                                              "Full");
    add_overlay_multiply_variants<std::uint16_t, false, false>(cases, bits_per_pixel, 0.63F, 161,
                                                               "Partial63");
    add_overlay_multiply_variants<std::uint16_t, true, true>(cases, bits_per_pixel, 1.0F, 256,
                                                             "Full");
    add_overlay_multiply_variants<std::uint16_t, false, true>(cases, bits_per_pixel, 0.63F, 161,
                                                             "Partial63");
  }
  add_overlay_multiply_variants<std::uint8_t, false, true>(
      cases, 8, 0.63F, 161, "Partial63", 0xF30C1F01U,
      {"40257713ccdd951a", "53b006e9852db3b1", "1c6a084578b5cd88"});
  add_overlay_multiply_variants<std::uint16_t, false, false>(
      cases, 16, 0.63F, 161, "Partial63", 0xF30C1F02U,
      {"f091ab24414129c2", "74c6928bf299d49a", "a316091024093a41"});
  return cases;
}

class OverlayMultiplyKernels : public ::testing::TestWithParam<OverlayMultiplyCase> {};

TEST_P(OverlayMultiplyKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bits_per_pixel == 8) {
    run_overlay_multiply_case_typed<std::uint8_t>(test_case);
  } else {
    run_overlay_multiply_case_typed<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, OverlayMultiplyKernels,
                         ::testing::ValuesIn(overlay_multiply_cases()),
                         [](const ::testing::TestParamInfo<OverlayMultiplyCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
