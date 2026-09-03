#include <gtest/gtest.h>

#include "overlay_mask_test_helpers.h"

#include "support/cpu_features.h"

#include <string>
#include <vector>

namespace avsut::test {
namespace {

void add_overlay_integer_variants(std::vector<OverlayIntegerCase>& cases, MaskMode mask_mode,
                                  int bits_per_pixel, std::size_t width_pixels,
                                  std::size_t height_pixels, int opacity,
                                  const std::string& opacity_label, const char* expected_hash,
                                  std::uint32_t seed = 0) {
  const auto scalar = get_overlay_blend_masked_fn_c(mask_mode != MASK444, mask_mode);
  const auto sse41 = get_overlay_blend_masked_fn_sse41(mask_mode != MASK444, mask_mode);
  const auto avx2 = get_overlay_blend_masked_fn_avx2(mask_mode != MASK444, mask_mode);
  cases.push_back(make_overlay_integer_case(
      mask_mode, bits_per_pixel, width_pixels, height_pixels, opacity, opacity_label, scalar,
      Variant<OverlayMaskedFuncPtr>{"c", scalar, IsaRequirement::Scalar}, expected_hash, seed));
  cases.push_back(make_overlay_integer_case(
      mask_mode, bits_per_pixel, width_pixels, height_pixels, opacity, opacity_label, scalar,
      Variant<OverlayMaskedFuncPtr>{"sse4.1", sse41, IsaRequirement::Sse41}, expected_hash, seed));
  cases.push_back(make_overlay_integer_case(
      mask_mode, bits_per_pixel, width_pixels, height_pixels, opacity, opacity_label, scalar,
      Variant<OverlayMaskedFuncPtr>{"avx2", avx2, IsaRequirement::Avx2}, expected_hash, seed));
}

std::vector<OverlayIntegerCase> overlay_integer_cases() {
  std::vector<OverlayIntegerCase> cases;
  for (const int bits_per_pixel : {8, 10, 12, 14, 16}) {
    const auto max_value = (1 << bits_per_pixel) - 1;
    const auto width = bits_per_pixel == 8 ? std::size_t{37} : std::size_t{19};
    add_overlay_integer_variants(cases, MASK444, bits_per_pixel, width, 5, max_value,
                                 "Max" + std::to_string(max_value),
                                 bits_per_pixel == 8    ? "717dbe6f2f93a134"
                                 : bits_per_pixel == 10 ? "a86145a9206e4f30"
                                 : bits_per_pixel == 12 ? "23ef7cbd42993196"
                                 : bits_per_pixel == 14 ? "ec5e822c6087c15a"
                                                        : "fca3f28bfea3779b");
    add_overlay_integer_variants(cases, MASK444, bits_per_pixel, width, 5, max_value * 3 / 5,
                                 "Partial" + std::to_string(max_value * 3 / 5),
                                 bits_per_pixel == 8    ? "224516ed791da2bb"
                                 : bits_per_pixel == 10 ? "e834be02f0577dd4"
                                 : bits_per_pixel == 12 ? "c6a4389df48a3f4a"
                                 : bits_per_pixel == 14 ? "8d03ce5f6bc237b8"
                                                        : "0f69c1bfe430b07e");
  }
  for (const auto mask_mode : {MASK420, MASK422}) {
    const bool mask420 = mask_mode == MASK420;
    add_overlay_integer_variants(cases, mask_mode, 8, 17, 5, 255, "Max255",
                                 mask420 ? "f82db7d94634ce88" : "2c69e85ccb7afc35");
    add_overlay_integer_variants(cases, mask_mode, 8, 17, 5, 153, "Partial153",
                                 mask420 ? "5e45c1d988e60008" : "6b5b4d5386b1d317");
    add_overlay_integer_variants(cases, mask_mode, 16, 17, 5, 65535, "Max65535",
                                 mask420 ? "d598775ac75506ab" : "bb843dccb2f0646f");
    add_overlay_integer_variants(cases, mask_mode, 16, 17, 5, 39321, "Partial39321",
                                 mask420 ? "4e75a56021d46239" : "bcf6c37afc46ac99");
  }
  add_overlay_integer_variants(cases, MASK420, 8, 37, 5, 153, "Partial153", "fc2ec5d742c01e71",
                               0xF30C1E01U);
  add_overlay_integer_variants(cases, MASK422, 16, 19, 5, 39321, "Partial39321", "85f6c5f6a605d418",
                               0xF30C1E02U);
  return cases;
}

void add_overlay_float_variants(std::vector<OverlayFloatCase>& cases, MaskMode mask_mode,
                                float opacity, const char* opacity_label) {
  const auto scalar = get_overlay_blend_masked_float_fn_c(mask_mode != MASK444, mask_mode);
  const auto sse41 = get_overlay_blend_masked_float_fn_sse41(mask_mode != MASK444, mask_mode);
  const auto avx2 = get_overlay_blend_masked_float_fn_avx2(mask_mode != MASK444, mask_mode);
  const auto width = mask_mode == MASK444 ? std::size_t{13} : std::size_t{17};
  cases.push_back(make_overlay_float_case(
      mask_mode, width, 5, opacity, opacity_label, scalar,
      Variant<OverlayMaskedFloatFuncPtr>{"c", scalar, IsaRequirement::Scalar}));
  cases.push_back(make_overlay_float_case(
      mask_mode, width, 5, opacity, opacity_label, scalar,
      Variant<OverlayMaskedFloatFuncPtr>{"sse4.1", sse41, IsaRequirement::Sse41}));
  cases.push_back(make_overlay_float_case(
      mask_mode, width, 5, opacity, opacity_label, scalar,
      Variant<OverlayMaskedFloatFuncPtr>{"avx2", avx2, IsaRequirement::Avx2}));
}

std::vector<OverlayFloatCase> overlay_float_cases() {
  std::vector<OverlayFloatCase> cases;
  for (const auto mask_mode : {MASK444, MASK420, MASK422}) {
    add_overlay_float_variants(cases, mask_mode, 1.0F, "Full");
    add_overlay_float_variants(cases, mask_mode, 0.63F, "Partial63");
  }
  return cases;
}

class OverlayIntegerMaskedKernels : public ::testing::TestWithParam<OverlayIntegerCase> {};

TEST_P(OverlayIntegerMaskedKernels, MatchesReferenceAcrossMaskPlacement) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bits_per_pixel == 8) {
    run_overlay_integer_case_typed<std::uint8_t>(test_case);
  } else {
    run_overlay_integer_case_typed<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, OverlayIntegerMaskedKernels,
                         ::testing::ValuesIn(overlay_integer_cases()),
                         [](const ::testing::TestParamInfo<OverlayIntegerCase>& info) {
                           return info.param.name;
                         });

class OverlayFloatMaskedKernels : public ::testing::TestWithParam<OverlayFloatCase> {};

TEST_P(OverlayFloatMaskedKernels, MatchesReferenceWithinTolerance) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_overlay_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, OverlayFloatMaskedKernels,
                         ::testing::ValuesIn(overlay_float_cases()),
                         [](const ::testing::TestParamInfo<OverlayFloatCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
