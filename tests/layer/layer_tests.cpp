#include <gtest/gtest.h>
#include "support/compat_375.h"
#include "filters/layer.h"
#include "support/cpu_features.h"
#include "layer_mask_test_helpers.h"
#include "layer_colorkey_test_helpers.h"
#include "layer_rgb32_fast_test_helpers.h"
#include "layer_rgb32_add_test_helpers.h"
#include "layer_rgb32_subtract_test_helpers.h"
#include "layer_rgb32_lighten_darken_test_helpers.h"
#include "layer_rgb32_mul_test_helpers.h"
#include "layer_frame_invert_test_helpers.h"
#include "layer_yuy2_fast_test_helpers.h"

namespace avsut::test {
namespace {

std::vector<LayerMaskCase> layer_mask_cases() {
  constexpr std::size_t width_pixels = 9;
  constexpr std::size_t height = 3;
  constexpr std::size_t source_pitch = 64;
  constexpr std::size_t alpha_pitch = 80;
  return {
      make_layer_mask_case(width_pixels, height, source_pitch, alpha_pitch,
                           Variant<LayerMaskFunction>{"sse2", mask_sse2, IsaRequirement::Sse2},
                           "1867ef60337953e9"),
  };
}

class LayerMaskKernels : public ::testing::TestWithParam<LayerMaskCase> {};

TEST_P(LayerMaskKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_mask_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerMaskKernels, ::testing::ValuesIn(layer_mask_cases()),
                         [](const ::testing::TestParamInfo<LayerMaskCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerColorKeyMaskCase> layer_colorkey_cases() {
  constexpr std::size_t width_pixels = 13;
  constexpr std::size_t height = 3;
  constexpr std::size_t pitch = 64;
  constexpr int color = 0x00c87828;
  constexpr int tolerance_b = 3;
  constexpr int tolerance_g = 5;
  constexpr int tolerance_r = 7;
  return {
      make_layer_colorkey_case(
          width_pixels, height, pitch, color, tolerance_b, tolerance_g, tolerance_r,
          Variant<LayerColorKeyMaskFunction>{"sse2", colorkeymask_sse2, IsaRequirement::Sse2},
          "0c2bba8d6e79353b"),
  };
}

class LayerColorKeyMaskKernels : public ::testing::TestWithParam<LayerColorKeyMaskCase> {};

TEST_P(LayerColorKeyMaskKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_colorkey_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerColorKeyMaskKernels,
                         ::testing::ValuesIn(layer_colorkey_cases()),
                         [](const ::testing::TestParamInfo<LayerColorKeyMaskCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerRgb32FastCase> layer_rgb32_fast_cases() {
  constexpr std::size_t width_pixels = 11;
  constexpr std::size_t height = 3;
  constexpr std::size_t destination_pitch = 64;
  constexpr std::size_t overlay_pitch = 80;
  return {
      make_layer_rgb32_fast_case(
          width_pixels, height, destination_pitch, overlay_pitch,
          Variant<LayerRgb32FastFunction>{"sse2", layer_rgb32_fast_sse2, IsaRequirement::Sse2},
          "dbfeb368cbc16789"),
  };
}

class LayerRgb32FastKernels : public ::testing::TestWithParam<LayerRgb32FastCase> {};

TEST_P(LayerRgb32FastKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_rgb32_fast_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerRgb32FastKernels,
                         ::testing::ValuesIn(layer_rgb32_fast_cases()),
                         [](const ::testing::TestParamInfo<LayerRgb32FastCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerRgb32AddCase> layer_rgb32_add_cases() {
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height = 3;
  constexpr std::size_t destination_pitch = 64;
  constexpr std::size_t overlay_pitch = 80;
  constexpr int full_level = 257;
  constexpr int partial_level = 173;
  std::vector<LayerRgb32AddCase> cases{
      make_layer_rgb32_add_case(
          width_pixels, height, destination_pitch, overlay_pitch, full_level, "Full257",
          Variant<LayerRgb32AddFunction>{"sse2", layer_rgb32_add_sse2<false>, IsaRequirement::Sse2},
          "c1653fdf75d25f9e"),
      make_layer_rgb32_add_case(
          width_pixels, height, destination_pitch, overlay_pitch, partial_level, "Partial173",
          Variant<LayerRgb32AddFunction>{"sse2", layer_rgb32_add_sse2<false>, IsaRequirement::Sse2},
          "8a5a44620cfb2a74"),
  };
  for (const auto& variant : {
           Variant<LayerRgb32AddFunction>{"sse2", layer_rgb32_add_sse2<false>,
                                          IsaRequirement::Sse2},
       }) {
    cases.push_back(make_layer_rgb32_add_case(13, 5, 64, 80, partial_level, "Partial173", variant,
                                              "ec203ae5164c3a95", 0xF30F2002U));
  }
  return cases;
}

class LayerRgb32AddKernels : public ::testing::TestWithParam<LayerRgb32AddCase> {};

TEST_P(LayerRgb32AddKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_rgb32_add_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerRgb32AddKernels,
                         ::testing::ValuesIn(layer_rgb32_add_cases()),
                         [](const ::testing::TestParamInfo<LayerRgb32AddCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerRgb32SubtractCase> layer_rgb32_subtract_cases() {
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height = 3;
  constexpr std::size_t destination_pitch = 64;
  constexpr std::size_t overlay_pitch = 80;
  constexpr int full_level = 257;
  constexpr int partial_level = 173;
  std::vector<LayerRgb32SubtractCase> cases{
      make_layer_rgb32_subtract_case(
          width_pixels, height, destination_pitch, overlay_pitch, full_level, "Full257",
          Variant<LayerRgb32SubtractFunction>{"sse2", layer_rgb32_subtract_sse2<false>,
                                              IsaRequirement::Sse2},
          "6ea3751be3e04826"),
      make_layer_rgb32_subtract_case(
          width_pixels, height, destination_pitch, overlay_pitch, partial_level, "Partial173",
          Variant<LayerRgb32SubtractFunction>{"sse2", layer_rgb32_subtract_sse2<false>,
                                              IsaRequirement::Sse2},
          "340ea8bf55efe193"),
  };
  for (const auto& variant : {
           Variant<LayerRgb32SubtractFunction>{"sse2", layer_rgb32_subtract_sse2<false>,
                                               IsaRequirement::Sse2},
       }) {
    cases.push_back(make_layer_rgb32_subtract_case(13, 5, 64, 80, partial_level, "Partial173",
                                                   variant, "e5d0d747c6bcf94c", 0xF30F2003U));
  }
  return cases;
}

class LayerRgb32SubtractKernels : public ::testing::TestWithParam<LayerRgb32SubtractCase> {};

TEST_P(LayerRgb32SubtractKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_rgb32_subtract_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerRgb32SubtractKernels,
                         ::testing::ValuesIn(layer_rgb32_subtract_cases()),
                         [](const ::testing::TestParamInfo<LayerRgb32SubtractCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerRgb32LightenDarkenCase> layer_rgb32_lighten_darken_cases() {
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height = 3;
  constexpr std::size_t destination_pitch = 64;
  constexpr std::size_t overlay_pitch = 80;
  constexpr int full_level = 257;
  constexpr int partial_level = 173;
  constexpr int threshold = 5;
  std::vector<LayerRgb32LightenDarkenCase> cases{
      make_layer_rgb32_lighten_darken_case(
          LayerRgb32LightenDarkenMode::Lighten, width_pixels, height, destination_pitch,
          overlay_pitch, full_level, "Full257", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"sse2", layer_rgb32_lighten_darken_sse2<LIGHTEN>,
                                                   IsaRequirement::Sse2},
          "e3275a96d0ef2da4"),
      make_layer_rgb32_lighten_darken_case(
          LayerRgb32LightenDarkenMode::Lighten, width_pixels, height, destination_pitch,
          overlay_pitch, partial_level, "Partial173", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"sse2", layer_rgb32_lighten_darken_sse2<LIGHTEN>,
                                                   IsaRequirement::Sse2},
          "8ae014b1d520042a"),
      make_layer_rgb32_lighten_darken_case(
          LayerRgb32LightenDarkenMode::Darken, width_pixels, height, destination_pitch,
          overlay_pitch, full_level, "Full257", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"sse2", layer_rgb32_lighten_darken_sse2<DARKEN>,
                                                   IsaRequirement::Sse2},
          "1e5764fa360abc46"),
      make_layer_rgb32_lighten_darken_case(
          LayerRgb32LightenDarkenMode::Darken, width_pixels, height, destination_pitch,
          overlay_pitch, partial_level, "Partial173", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"sse2", layer_rgb32_lighten_darken_sse2<DARKEN>,
                                                   IsaRequirement::Sse2},
          "f9984ff13b4d9cd9"),
  };
  for (const auto mode :
       {LayerRgb32LightenDarkenMode::Lighten, LayerRgb32LightenDarkenMode::Darken}) {
    for (const auto& variant : {
             Variant<LayerRgb32LightenDarkenFunction>{"sse2",
                                                      mode == LayerRgb32LightenDarkenMode::Lighten
                                                          ? layer_rgb32_lighten_darken_sse2<LIGHTEN>
                                                          : layer_rgb32_lighten_darken_sse2<DARKEN>,
                                                      IsaRequirement::Sse2},
         }) {
      cases.push_back(make_layer_rgb32_lighten_darken_case(
          mode, 13, 5, 64, 80, partial_level, "Partial173", threshold, variant,
          mode == LayerRgb32LightenDarkenMode::Lighten ? "f9cf16b2acd1c59c" : "cdd15890bdbca3b5",
          mode == LayerRgb32LightenDarkenMode::Lighten ? 0xF30F2004U : 0xF30F2005U));
    }
  }
  return cases;
}

class LayerRgb32LightenDarkenKernels
    : public ::testing::TestWithParam<LayerRgb32LightenDarkenCase> {};

TEST_P(LayerRgb32LightenDarkenKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_rgb32_lighten_darken_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerRgb32LightenDarkenKernels,
                         ::testing::ValuesIn(layer_rgb32_lighten_darken_cases()),
                         [](const ::testing::TestParamInfo<LayerRgb32LightenDarkenCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerRgb32MulCase> layer_rgb32_mul_cases() {
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height = 3;
  constexpr std::size_t destination_pitch = 64;
  constexpr std::size_t overlay_pitch = 80;
  constexpr int full_level = 257;
  constexpr int partial_level = 173;
  std::vector<LayerRgb32MulCase> cases;
  for (const bool use_chroma : {false, true}) {
    for (const auto& level :
         {std::pair{full_level, "Full257"}, std::pair{partial_level, "Partial173"}}) {
      cases.push_back(make_layer_rgb32_mul_case(
          use_chroma, width_pixels, height, destination_pitch, overlay_pitch, level.first,
          level.second,
          Variant<LayerRgb32MulFunction>{
              "sse2", use_chroma ? layer_rgb32_mul_sse2<true> : layer_rgb32_mul_sse2<false>,
              IsaRequirement::Sse2},
          use_chroma ? (level.first == full_level ? "fcb791dc58b06344" : "36675815f43b074b")
                     : (level.first == full_level ? "13bd888dabfc7d28" : "3bf166e71030c1a2")));
    }
  }
  for (const bool use_chroma : {false, true}) {
    for (const auto& variant : {
             Variant<LayerRgb32MulFunction>{
                 "sse2", use_chroma ? layer_rgb32_mul_sse2<true> : layer_rgb32_mul_sse2<false>,
                 IsaRequirement::Sse2},
         }) {
      cases.push_back(make_layer_rgb32_mul_case(
          use_chroma, 13, 5, 64, 80, partial_level, "Partial173", variant,
          use_chroma ? "9cb7c4a09565d6a5" : "4a6100a54c6da70d", 0xF30F2001U));
    }
  }
  return cases;
}

class LayerRgb32MulKernels : public ::testing::TestWithParam<LayerRgb32MulCase> {};

TEST_P(LayerRgb32MulKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_rgb32_mul_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerRgb32MulKernels,
                         ::testing::ValuesIn(layer_rgb32_mul_cases()),
                         [](const ::testing::TestParamInfo<LayerRgb32MulCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerFrameInvert8Case> layer_frame_invert_8_cases() {
  constexpr std::size_t row_bytes = 96;
  constexpr std::size_t height = 3;
  constexpr std::uint32_t mask = 0xa5c33c5aU;
  std::vector<LayerFrameInvert8Case> cases{
      make_layer_frame_invert_8_case(
          row_bytes, height, row_bytes, mask,
          Variant<LayerFrameInvert8Function>{"sse2", invert_frame_sse2, IsaRequirement::Sse2},
          "4f10696e78a99b2e"),
  };
  for (const auto& variant : {
           Variant<LayerFrameInvert8Function>{"sse2", invert_frame_sse2, IsaRequirement::Sse2},
       }) {
    cases.push_back(make_layer_frame_invert_8_case(128, 5, 128, 0x6d3a91c7U, variant,
                                                   "e3e4e26f74e919b2", 0xF30F2201U));
  }
  return cases;
}

class LayerFrameInvert8Kernels : public ::testing::TestWithParam<LayerFrameInvert8Case> {};

TEST_P(LayerFrameInvert8Kernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_frame_invert_8_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerFrameInvert8Kernels,
                         ::testing::ValuesIn(layer_frame_invert_8_cases()),
                         [](const ::testing::TestParamInfo<LayerFrameInvert8Case>& info) {
                           return info.param.name;
                         });

std::vector<LayerFrameInvert16Case> layer_frame_invert_16_cases() {
  constexpr std::size_t row_bytes = 96;
  constexpr std::size_t height = 3;
  constexpr std::uint64_t mask = 0x1234fedcba987654ULL;
  std::vector<LayerFrameInvert16Case> cases{
      make_layer_frame_invert_16_case(row_bytes, height, row_bytes, mask,
                                      Variant<LayerFrameInvert16Function>{
                                          "sse2", invert_frame_uint16_sse2, IsaRequirement::Sse2},
                                      "7bdc59443981516a"),
  };
  for (const auto& variant : {
           Variant<LayerFrameInvert16Function>{"sse2", invert_frame_uint16_sse2,
                                               IsaRequirement::Sse2},
       }) {
    cases.push_back(make_layer_frame_invert_16_case(128, 5, 128, 0x0f1e2d3c4b5a6978ULL, variant,
                                                    "d345be7dbcf36a8d", 0xF30F2202U));
  }
  return cases;
}

class LayerFrameInvert16Kernels : public ::testing::TestWithParam<LayerFrameInvert16Case> {};

TEST_P(LayerFrameInvert16Kernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_frame_invert_16_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerFrameInvert16Kernels,
                         ::testing::ValuesIn(layer_frame_invert_16_cases()),
                         [](const ::testing::TestParamInfo<LayerFrameInvert16Case>& info) {
                           return info.param.name;
                         });

std::vector<LayerYuy2FastCase> layer_yuy2_fast_cases() {
  constexpr std::size_t width_pixels = 19;
  constexpr std::size_t height = 3;
  constexpr std::size_t destination_pitch = 64;
  constexpr std::size_t overlay_pitch = 80;
  // The release SSE2 kernel requires aligned rows; scalar tails still cover odd widths.
  constexpr std::size_t destination_alignment_offset = 0;
  constexpr std::size_t overlay_alignment_offset = 0;
  std::vector<LayerYuy2FastCase> cases{make_layer_yuy2_fast_case(
      width_pixels, height, destination_pitch, overlay_pitch, destination_alignment_offset,
      overlay_alignment_offset,
      Variant<LayerYuy2FastFunction>{"sse2", layer_yuy2_fast_sse2, IsaRequirement::Sse2},
      "467d20be64ba47ef")};
  cases.push_back(make_layer_yuy2_fast_case(
      31, 7, 80, 96, 0, 0,
      Variant<LayerYuy2FastFunction>{"sse2", layer_yuy2_fast_sse2, IsaRequirement::Sse2},
      "21be240352c36f33", 0xF30F2301U));
  return cases;
}

class LayerYuy2FastKernels : public ::testing::TestWithParam<LayerYuy2FastCase> {};

TEST_P(LayerYuy2FastKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_yuy2_fast_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerYuy2FastKernels,
                         ::testing::ValuesIn(layer_yuy2_fast_cases()),
                         [](const ::testing::TestParamInfo<LayerYuy2FastCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
