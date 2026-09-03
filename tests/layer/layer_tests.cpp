#include <gtest/gtest.h>

#include "layer_test_helpers.h"
#include "layer_colorkey_test_helpers.h"
#include "layer_frame_invert_test_helpers.h"
#include "layer_invert_test_helpers.h"
#include "layer_mask_test_helpers.h"
#include "layer_packed_blend_test_helpers.h"
#include "layer_planarrgb_add_test_helpers.h"
#include "layer_planarrgb_add_float_test_helpers.h"
#include "layer_planarrgb_lighten_darken_test_helpers.h"
#include "layer_planarrgb_lighten_darken_float_test_helpers.h"
#include "layer_planarrgb_mul_test_helpers.h"
#include "layer_planarrgb_mul_float_test_helpers.h"
#include "layer_rgb32_add_test_helpers.h"
#include "layer_rgb32_fast_test_helpers.h"
#include "layer_rgb32_lighten_darken_test_helpers.h"
#include "layer_rgb32_mul_test_helpers.h"
#include "layer_rgb32_subtract_test_helpers.h"
#include "layer_yuv_mul_test_helpers.h"
#include "layer_yuv_mul_float_test_helpers.h"
#include "layer_yuy2_fast_test_helpers.h"

#include "support/cpu_features.h"

#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace avsut::test {
namespace {

std::string layer_expected_hash(MaskMode mode, int bits_per_pixel, bool full_opacity) {
  if (bits_per_pixel == 8) {
    if (full_opacity) {
      switch (mode) {
        case MASK444:
          return "14dbbcc50651f5bb";
        case MASK420:
          return "7712bdab1d1469cb";
        case MASK422:
          return "babb2b1e8c3e743f";
        case MASK420_MPEG2:
          return "9ea76060583478b9";
        case MASK422_MPEG2:
          return "421f8079ac704fbe";
        case MASK420_TOPLEFT:
          return "dc1e7036ac538b83";
        case MASK422_TOPLEFT:
          return "6ce91f1354061b87";
        default:
          break;
      }
    } else {
      switch (mode) {
        case MASK444:
          return "3e803ad138c2bd56";
        case MASK420:
          return "2012bddf9fac9abb";
        case MASK422:
          return "5ef2fcdf3375f0a3";
        case MASK420_MPEG2:
          return "53333e3e85b5b2a6";
        case MASK422_MPEG2:
          return "902a84966e047bab";
        case MASK420_TOPLEFT:
          return "64baadc5d6e8d46e";
        case MASK422_TOPLEFT:
          return "20599fe4adfa7a58";
        default:
          break;
      }
    }
  } else if (full_opacity) {
    switch (mode) {
      case MASK444:
        return "74812dbb85566db1";
      case MASK420:
        return "bc9ce1f2cb6e988a";
      case MASK422:
        return "8437830a12e92639";
      case MASK420_MPEG2:
        return "b1560b0d4785b73f";
      case MASK422_MPEG2:
        return "e380ef45516a95b6";
      case MASK420_TOPLEFT:
        return "41f13dbf16bd6b5a";
      case MASK422_TOPLEFT:
        return "3080c53ada902cf8";
      default:
        break;
    }
  } else {
    switch (mode) {
      case MASK444:
        return "f13eb0bc0b2bf106";
      case MASK420:
        return "2ecff2c240d70de8";
      case MASK422:
        return "4d6583d3c52ba529";
      case MASK420_MPEG2:
        return "7ecd08024daf47d2";
      case MASK422_MPEG2:
        return "9170ad6c1c643eb4";
      case MASK420_TOPLEFT:
        return "4ce86c41e85d4a69";
      case MASK422_TOPLEFT:
        return "602b491cbf1736aa";
      default:
        break;
    }
  }
  return {};
}

std::vector<LayerYuvAddCase> layer_yuv_add_cases() {
  std::vector<LayerYuvAddCase> cases;
  for (const int bits_per_pixel : {8, 16}) {
    const auto max_value = (1 << bits_per_pixel) - 1;
    const auto width = bits_per_pixel == 8 ? std::size_t{17} : std::size_t{9};
    const auto max_name = "Max" + std::to_string(max_value);
    const auto partial = max_value * 3 / 5;
    const auto partial_name = "Partial" + std::to_string(partial);
    for (const auto opacity : {max_value, partial}) {
      const auto& opacity_name = opacity == max_value ? max_name : partial_name;
      for (const auto placement : {PLACEMENT_MPEG1, PLACEMENT_MPEG2, PLACEMENT_TOPLEFT}) {
        const auto mask420_mode = layer_mask_mode(true, 420, placement);
        const auto mask422_mode = layer_mask_mode(true, 422, placement);
        cases.push_back(make_layer_yuv_add_case(
            true, 420, placement, bits_per_pixel, width, 5, opacity, opacity_name,
            Variant<LayerYuvAddFuncPtr>{"sse41",
                                        layer_sse41_function(true, 420, placement, bits_per_pixel),
                                        IsaRequirement::Sse41},
            layer_expected_hash(mask420_mode, bits_per_pixel, opacity == max_value)));
        cases.push_back(make_layer_yuv_add_case(
            true, 420, placement, bits_per_pixel, width, 5, opacity, opacity_name,
            Variant<LayerYuvAddFuncPtr>{"avx2",
                                        layer_avx2_function(true, 420, placement, bits_per_pixel),
                                        IsaRequirement::Avx2},
            layer_expected_hash(mask420_mode, bits_per_pixel, opacity == max_value)));
        cases.push_back(make_layer_yuv_add_case(
            true, 422, placement, bits_per_pixel, width, 5, opacity, opacity_name,
            Variant<LayerYuvAddFuncPtr>{"sse41",
                                        layer_sse41_function(true, 422, placement, bits_per_pixel),
                                        IsaRequirement::Sse41},
            layer_expected_hash(mask422_mode, bits_per_pixel, opacity == max_value)));
        cases.push_back(make_layer_yuv_add_case(
            true, 422, placement, bits_per_pixel, width, 5, opacity, opacity_name,
            Variant<LayerYuvAddFuncPtr>{"avx2",
                                        layer_avx2_function(true, 422, placement, bits_per_pixel),
                                        IsaRequirement::Avx2},
            layer_expected_hash(mask422_mode, bits_per_pixel, opacity == max_value)));
      }
      cases.push_back(make_layer_yuv_add_case(
          false, 420, PLACEMENT_MPEG2, bits_per_pixel, width, 5, opacity, opacity_name,
          Variant<LayerYuvAddFuncPtr>{
              "sse41", layer_sse41_function(false, 420, PLACEMENT_MPEG2, bits_per_pixel),
              IsaRequirement::Sse41},
          layer_expected_hash(MASK444, bits_per_pixel, opacity == max_value)));
      cases.push_back(make_layer_yuv_add_case(
          false, 420, PLACEMENT_MPEG2, bits_per_pixel, width, 5, opacity, opacity_name,
          Variant<LayerYuvAddFuncPtr>{
              "avx2", layer_avx2_function(false, 420, PLACEMENT_MPEG2, bits_per_pixel),
              IsaRequirement::Avx2},
          layer_expected_hash(MASK444, bits_per_pixel, opacity == max_value)));
    }
  }
  cases.push_back(make_layer_yuv_add_case(
      true, 420, PLACEMENT_MPEG2, 8, 23, 7, 153, "Partial153",
      Variant<LayerYuvAddFuncPtr>{"sse41", layer_sse41_function(true, 420, PLACEMENT_MPEG2, 8),
                                  IsaRequirement::Sse41},
      "3a7b2db2d25bf0b3", 0xF30F1B01U));
  cases.push_back(make_layer_yuv_add_case(
      true, 420, PLACEMENT_MPEG2, 8, 23, 7, 153, "Partial153",
      Variant<LayerYuvAddFuncPtr>{"avx2", layer_avx2_function(true, 420, PLACEMENT_MPEG2, 8),
                                  IsaRequirement::Avx2},
      "3a7b2db2d25bf0b3", 0xF30F1B01U));
  return cases;
}

class LayerYuvAddKernels : public ::testing::TestWithParam<LayerYuvAddCase> {};

TEST_P(LayerYuvAddKernels, MatchesPlacementAwareReference) {
  const auto& test_case = GetParam();
  if (!test_case.variant.function) {
    GTEST_SKIP() << "upstream did not provide " << test_case.variant.name
                 << " Layer YUV add function";
  }
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bits_per_pixel == 8) {
    run_layer_yuv_add_case<std::uint8_t>(test_case);
  } else {
    run_layer_yuv_add_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerYuvAddKernels, ::testing::ValuesIn(layer_yuv_add_cases()),
                         [](const ::testing::TestParamInfo<LayerYuvAddCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerYuvMulCase> layer_yuv_mul_cases() {
  constexpr int bits_per_pixel = 16;
  constexpr int placement = PLACEMENT_MPEG2;
  constexpr std::size_t width_pixels = 15;
  constexpr std::size_t height_pixels = 7;
  constexpr std::size_t destination_pitch = 64;
  constexpr std::size_t overlay_pitch = 80;
  constexpr std::size_t mask_pitch = 96;
  constexpr int opacity = 39321;
  constexpr std::uint32_t seed = 0xF30F2501U;
  const Variant<LayerYuvMulFunction> variant{
      "avx2", layer_yuv_mul_avx2_function(false, 420, placement, bits_per_pixel),
      IsaRequirement::Avx2};
  const Variant<LayerYuvMulFunction> chroma_variant{
      "avx2", layer_yuv_mul_avx2_function(true, 420, placement, bits_per_pixel),
      IsaRequirement::Avx2};
  return {
      make_layer_yuv_mul_case("Yv12", false, 420, placement, bits_per_pixel, width_pixels,
                              height_pixels, destination_pitch, overlay_pitch, mask_pitch,
                              opacity, "Partial39321", variant, "f18b7ce631492d6f", seed),
      make_layer_yuv_mul_case("Yv12", true, 420, placement, bits_per_pixel, width_pixels,
                              height_pixels, destination_pitch, overlay_pitch, mask_pitch,
                              opacity, "Partial39321", chroma_variant, "89271b963a11b9d3", seed),
  };
}

class LayerYuvMulKernels : public ::testing::TestWithParam<LayerYuvMulCase> {};

TEST_P(LayerYuvMulKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_yuv_mul_case<std::uint16_t>(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerYuvMulKernels, ::testing::ValuesIn(layer_yuv_mul_cases()),
                         [](const ::testing::TestParamInfo<LayerYuvMulCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerYuvMulFloatCase> layer_yuv_mul_float_cases() {
  constexpr int colorspace = 420;
  constexpr int placement = PLACEMENT_MPEG2;
  constexpr std::size_t width_pixels = 7;
  constexpr std::size_t height_pixels = 3;
  constexpr std::size_t destination_pitch = 32;
  constexpr std::size_t overlay_pitch = 48;
  constexpr float opacity = 0.37F;
  return {
      make_layer_yuv_mul_float_case("Yv12", false, false, colorspace, placement, width_pixels,
                                    height_pixels, destination_pitch, overlay_pitch, 0, opacity,
                                    "37Pct"),
      make_layer_yuv_mul_float_case("Yv12", false, true, colorspace, placement, width_pixels,
                                    height_pixels, destination_pitch, overlay_pitch, 40, opacity,
                                    "37Pct"),
      make_layer_yuv_mul_float_case("Yv12", true, false, colorspace, placement, width_pixels,
                                    height_pixels, destination_pitch, overlay_pitch, 0, opacity,
                                    "37Pct"),
      make_layer_yuv_mul_float_case("Yv12", true, true, colorspace, placement, width_pixels,
                                    height_pixels, destination_pitch, overlay_pitch, 64, opacity,
                                    "37Pct"),
  };
}

class LayerYuvMulFloatKernels : public ::testing::TestWithParam<LayerYuvMulFloatCase> {};

TEST_P(LayerYuvMulFloatKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!test_case.variant.function) {
    GTEST_SKIP() << "upstream did not provide " << test_case.variant.name
                 << " float Layer YUV multiply function";
  }
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_yuv_mul_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerYuvMulFloatKernels,
                         ::testing::ValuesIn(layer_yuv_mul_float_cases()),
                         [](const ::testing::TestParamInfo<LayerYuvMulFloatCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerMaskCase> layer_mask_cases() {
  constexpr std::size_t width_pixels = 9;
  constexpr std::size_t height = 3;
  constexpr std::size_t source_pitch = 64;
  constexpr std::size_t alpha_pitch = 80;
  return {
      make_layer_mask_case(width_pixels, height, source_pitch, alpha_pitch,
                           Variant<LayerMaskFunction>{"sse2", mask_sse2, IsaRequirement::Sse2},
                           "1867ef60337953e9"),
      make_layer_mask_case(width_pixels, height, source_pitch, alpha_pitch,
                           Variant<LayerMaskFunction>{"avx2", mask_avx2, IsaRequirement::Avx2},
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
      make_layer_colorkey_case(
          width_pixels, height, pitch, color, tolerance_b, tolerance_g, tolerance_r,
          Variant<LayerColorKeyMaskFunction>{"avx2", colorkeymask_avx2, IsaRequirement::Avx2},
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
      make_layer_rgb32_fast_case(
          width_pixels, height, destination_pitch, overlay_pitch,
          Variant<LayerRgb32FastFunction>{"avx2", layer_rgb32_fast_avx2, IsaRequirement::Avx2},
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
          width_pixels, height, destination_pitch, overlay_pitch, full_level, "Full257",
          Variant<LayerRgb32AddFunction>{"avx2", layer_rgb32_add_avx2<false>, IsaRequirement::Avx2},
          "c1653fdf75d25f9e"),
      make_layer_rgb32_add_case(
          width_pixels, height, destination_pitch, overlay_pitch, partial_level, "Partial173",
          Variant<LayerRgb32AddFunction>{"sse2", layer_rgb32_add_sse2<false>, IsaRequirement::Sse2},
          "8a5a44620cfb2a74"),
      make_layer_rgb32_add_case(
          width_pixels, height, destination_pitch, overlay_pitch, partial_level, "Partial173",
          Variant<LayerRgb32AddFunction>{"avx2", layer_rgb32_add_avx2<false>, IsaRequirement::Avx2},
          "8a5a44620cfb2a74"),
  };
  for (const auto& variant : {
           Variant<LayerRgb32AddFunction>{"sse2", layer_rgb32_add_sse2<false>, IsaRequirement::Sse2},
           Variant<LayerRgb32AddFunction>{"avx2", layer_rgb32_add_avx2<false>, IsaRequirement::Avx2}}) {
    cases.push_back(make_layer_rgb32_add_case(
        13, 5, 64, 80, partial_level, "Partial173", variant, "ec203ae5164c3a95",
        0xF30F2002U));
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
          width_pixels, height, destination_pitch, overlay_pitch, full_level, "Full257",
          Variant<LayerRgb32SubtractFunction>{"avx2", layer_rgb32_subtract_avx2<false>,
                                              IsaRequirement::Avx2},
          "6ea3751be3e04826"),
      make_layer_rgb32_subtract_case(
          width_pixels, height, destination_pitch, overlay_pitch, partial_level, "Partial173",
          Variant<LayerRgb32SubtractFunction>{"sse2", layer_rgb32_subtract_sse2<false>,
                                              IsaRequirement::Sse2},
          "340ea8bf55efe193"),
      make_layer_rgb32_subtract_case(
          width_pixels, height, destination_pitch, overlay_pitch, partial_level, "Partial173",
          Variant<LayerRgb32SubtractFunction>{"avx2", layer_rgb32_subtract_avx2<false>,
                                              IsaRequirement::Avx2},
          "340ea8bf55efe193"),
  };
  for (const auto& variant : {
           Variant<LayerRgb32SubtractFunction>{"sse2", layer_rgb32_subtract_sse2<false>,
                                               IsaRequirement::Sse2},
           Variant<LayerRgb32SubtractFunction>{"avx2", layer_rgb32_subtract_avx2<false>,
                                               IsaRequirement::Avx2}}) {
    cases.push_back(make_layer_rgb32_subtract_case(
        13, 5, 64, 80, partial_level, "Partial173", variant, "e5d0d747c6bcf94c",
        0xF30F2003U));
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
          overlay_pitch, full_level, "Full257", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"avx2", layer_rgb32_lighten_darken_avx2<LIGHTEN>,
                                                   IsaRequirement::Avx2},
          "e3275a96d0ef2da4"),
      make_layer_rgb32_lighten_darken_case(
          LayerRgb32LightenDarkenMode::Lighten, width_pixels, height, destination_pitch,
          overlay_pitch, partial_level, "Partial173", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"sse2", layer_rgb32_lighten_darken_sse2<LIGHTEN>,
                                                   IsaRequirement::Sse2},
          "8ae014b1d520042a"),
      make_layer_rgb32_lighten_darken_case(
          LayerRgb32LightenDarkenMode::Lighten, width_pixels, height, destination_pitch,
          overlay_pitch, partial_level, "Partial173", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"avx2", layer_rgb32_lighten_darken_avx2<LIGHTEN>,
                                                   IsaRequirement::Avx2},
          "8ae014b1d520042a"),
      make_layer_rgb32_lighten_darken_case(
          LayerRgb32LightenDarkenMode::Darken, width_pixels, height, destination_pitch,
          overlay_pitch, full_level, "Full257", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"sse2", layer_rgb32_lighten_darken_sse2<DARKEN>,
                                                   IsaRequirement::Sse2},
          "1e5764fa360abc46"),
      make_layer_rgb32_lighten_darken_case(
          LayerRgb32LightenDarkenMode::Darken, width_pixels, height, destination_pitch,
          overlay_pitch, full_level, "Full257", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"avx2", layer_rgb32_lighten_darken_avx2<DARKEN>,
                                                   IsaRequirement::Avx2},
          "1e5764fa360abc46"),
      make_layer_rgb32_lighten_darken_case(
          LayerRgb32LightenDarkenMode::Darken, width_pixels, height, destination_pitch,
          overlay_pitch, partial_level, "Partial173", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"sse2", layer_rgb32_lighten_darken_sse2<DARKEN>,
                                                   IsaRequirement::Sse2},
          "f9984ff13b4d9cd9"),
      make_layer_rgb32_lighten_darken_case(
          LayerRgb32LightenDarkenMode::Darken, width_pixels, height, destination_pitch,
          overlay_pitch, partial_level, "Partial173", threshold,
          Variant<LayerRgb32LightenDarkenFunction>{"avx2", layer_rgb32_lighten_darken_avx2<DARKEN>,
                                                   IsaRequirement::Avx2},
          "f9984ff13b4d9cd9"),
  };
  for (const auto mode : {LayerRgb32LightenDarkenMode::Lighten,
                          LayerRgb32LightenDarkenMode::Darken}) {
    for (const auto& variant : {
             Variant<LayerRgb32LightenDarkenFunction>{
                 "sse2",
                 mode == LayerRgb32LightenDarkenMode::Lighten
                     ? layer_rgb32_lighten_darken_sse2<LIGHTEN>
                     : layer_rgb32_lighten_darken_sse2<DARKEN>,
                 IsaRequirement::Sse2},
             Variant<LayerRgb32LightenDarkenFunction>{
                 "avx2",
                 mode == LayerRgb32LightenDarkenMode::Lighten
                     ? layer_rgb32_lighten_darken_avx2<LIGHTEN>
                     : layer_rgb32_lighten_darken_avx2<DARKEN>,
                 IsaRequirement::Avx2}}) {
      cases.push_back(make_layer_rgb32_lighten_darken_case(
          mode, 13, 5, 64, 80, partial_level, "Partial173", threshold, variant,
          mode == LayerRgb32LightenDarkenMode::Lighten ? "f9cf16b2acd1c59c"
                                                       : "cdd15890bdbca3b5",
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
      cases.push_back(make_layer_rgb32_mul_case(
          use_chroma, width_pixels, height, destination_pitch, overlay_pitch, level.first,
          level.second,
          Variant<LayerRgb32MulFunction>{
              "avx2", use_chroma ? layer_rgb32_mul_avx2<true> : layer_rgb32_mul_avx2<false>,
              IsaRequirement::Avx2},
          use_chroma ? (level.first == full_level ? "fcb791dc58b06344" : "36675815f43b074b")
                     : (level.first == full_level ? "13bd888dabfc7d28" : "3bf166e71030c1a2")));
    }
  }
  for (const bool use_chroma : {false, true}) {
    for (const auto& variant : {
             Variant<LayerRgb32MulFunction>{
                 "sse2", use_chroma ? layer_rgb32_mul_sse2<true> : layer_rgb32_mul_sse2<false>,
                 IsaRequirement::Sse2},
             Variant<LayerRgb32MulFunction>{
                 "avx2", use_chroma ? layer_rgb32_mul_avx2<true> : layer_rgb32_mul_avx2<false>,
                 IsaRequirement::Avx2}}) {
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

std::string layer_invert_expected_hash(int bits_per_pixel, bool chroma) {
  if (bits_per_pixel == 8) {
    return chroma ? "63dae183f4e233e3" : "3b5bf145fdaed0cb";
  }
  switch (bits_per_pixel) {
    case 10:
      return chroma ? "7c15f9f33b835a8a" : "5c304cf2053ce54b";
    case 12:
      return chroma ? "67303d0b4081f759" : "b533f26d2adcd798";
    case 14:
      return chroma ? "fef1da539fd5077b" : "bb93d2e087f3ffb1";
    case 16:
      return chroma ? "0c914f9bead77af9" : "744ac3dfbf30785f";
    default:
      return {};
  }
}

std::vector<LayerInvertCase> layer_invert_cases() {
  std::vector<LayerInvertCase> cases;
  constexpr std::size_t height = 3;
  constexpr std::size_t source_pitch = 96;
  constexpr std::size_t destination_pitch = 128;

  for (const bool chroma : {false, true}) {
    cases.push_back(make_layer_invert_case(
        LayerInvertElement::UInt8, chroma, 8, 64, height, source_pitch, destination_pitch,
        Variant<LayerInvertFuncPtr>{
            "sse2", chroma ? invert_plane_sse2_u8<true> : invert_plane_sse2_u8<false>,
            IsaRequirement::Sse2},
        layer_invert_expected_hash(8, chroma)));
    cases.push_back(make_layer_invert_case(
        LayerInvertElement::UInt8, chroma, 8, 64, height, source_pitch, destination_pitch,
        Variant<LayerInvertFuncPtr>{
            "avx2", chroma ? invert_plane_avx2_u8<true> : invert_plane_avx2_u8<false>,
            IsaRequirement::Avx2},
        layer_invert_expected_hash(8, chroma)));
  }

  for (const int bits_per_pixel : {10, 12, 14, 16}) {
    for (const bool chroma : {false, true}) {
      cases.push_back(make_layer_invert_case(
          LayerInvertElement::UInt16, chroma, bits_per_pixel, 32, height, source_pitch,
          destination_pitch,
          Variant<LayerInvertFuncPtr>{
              "sse2", chroma ? invert_plane_sse2_u16<true> : invert_plane_sse2_u16<false>,
              IsaRequirement::Sse2},
          layer_invert_expected_hash(bits_per_pixel, chroma)));
      cases.push_back(make_layer_invert_case(
          LayerInvertElement::UInt16, chroma, bits_per_pixel, 32, height, source_pitch,
          destination_pitch,
          Variant<LayerInvertFuncPtr>{
              "avx2", chroma ? invert_plane_avx2_u16<true> : invert_plane_avx2_u16<false>,
              IsaRequirement::Avx2},
          layer_invert_expected_hash(bits_per_pixel, chroma)));
    }
  }

  for (const bool chroma : {false, true}) {
    cases.push_back(make_layer_invert_case(
        LayerInvertElement::Float32, chroma, 32, 16, height, source_pitch, destination_pitch,
        Variant<LayerInvertFuncPtr>{
            "sse2", chroma ? invert_plane_sse2_f32<true> : invert_plane_sse2_f32<false>,
            IsaRequirement::Sse2}));
    cases.push_back(make_layer_invert_case(
        LayerInvertElement::Float32, chroma, 32, 16, height, source_pitch, destination_pitch,
        Variant<LayerInvertFuncPtr>{
            "avx2", chroma ? invert_plane_avx2_f32<true> : invert_plane_avx2_f32<false>,
            IsaRequirement::Avx2}));
  }
  for (const bool chroma : {false, true}) {
    cases.push_back(make_layer_invert_case(
        LayerInvertElement::UInt16, chroma, 12, 32, 5, 96, 128,
        Variant<LayerInvertFuncPtr>{
            "sse2", chroma ? invert_plane_sse2_u16<true> : invert_plane_sse2_u16<false>,
            IsaRequirement::Sse2},
        chroma ? "ae22902194acc072" : "3d1cad7f0dcd05de", 0xF30F2101U));
    cases.push_back(make_layer_invert_case(
        LayerInvertElement::UInt16, chroma, 12, 32, 5, 96, 128,
        Variant<LayerInvertFuncPtr>{
            "avx2", chroma ? invert_plane_avx2_u16<true> : invert_plane_avx2_u16<false>,
            IsaRequirement::Avx2},
        chroma ? "ae22902194acc072" : "3d1cad7f0dcd05de", 0xF30F2101U));
    cases.push_back(make_layer_invert_case(
        LayerInvertElement::Float32, chroma, 32, 16, 5, 96, 128,
        Variant<LayerInvertFuncPtr>{
            "sse2", chroma ? invert_plane_sse2_f32<true> : invert_plane_sse2_f32<false>,
            IsaRequirement::Sse2},
        {}, 0xF30F2102U));
    cases.push_back(make_layer_invert_case(
        LayerInvertElement::Float32, chroma, 32, 16, 5, 96, 128,
        Variant<LayerInvertFuncPtr>{
            "avx2", chroma ? invert_plane_avx2_f32<true> : invert_plane_avx2_f32<false>,
            IsaRequirement::Avx2},
        {}, 0xF30F2102U));
  }
  return cases;
}

class LayerInvertKernels : public ::testing::TestWithParam<LayerInvertCase> {};

TEST_P(LayerInvertKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_invert_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerInvertKernels, ::testing::ValuesIn(layer_invert_cases()),
                         [](const ::testing::TestParamInfo<LayerInvertCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerFrameInvert8Case> layer_frame_invert_8_cases() {
  constexpr std::size_t row_bytes = 96;
  constexpr std::size_t height = 3;
  constexpr std::uint32_t mask = 0xa5c33c5aU;
  std::vector<LayerFrameInvert8Case> cases{
      make_layer_frame_invert_8_case(row_bytes, height, row_bytes, mask,
                                     Variant<LayerFrameInvert8Function>{
                                         "sse2", invert_frame_inplace_sse2, IsaRequirement::Sse2},
                                     "4f10696e78a99b2e"),
      make_layer_frame_invert_8_case(row_bytes, height, row_bytes, mask,
                                     Variant<LayerFrameInvert8Function>{
                                         "avx2", invert_frame_inplace_avx2, IsaRequirement::Avx2},
                                     "4f10696e78a99b2e"),
  };
  for (const auto& variant : {
           Variant<LayerFrameInvert8Function>{"sse2", invert_frame_inplace_sse2, IsaRequirement::Sse2},
           Variant<LayerFrameInvert8Function>{"avx2", invert_frame_inplace_avx2, IsaRequirement::Avx2}}) {
    cases.push_back(make_layer_frame_invert_8_case(
        128, 5, 128, 0x6d3a91c7U, variant, "e3e4e26f74e919b2", 0xF30F2201U));
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
      make_layer_frame_invert_16_case(
          row_bytes, height, row_bytes, mask,
          Variant<LayerFrameInvert16Function>{"sse2", invert_frame_uint16_inplace_sse2,
                                              IsaRequirement::Sse2},
          "7bdc59443981516a"),
      make_layer_frame_invert_16_case(
          row_bytes, height, row_bytes, mask,
          Variant<LayerFrameInvert16Function>{"avx2", invert_frame_uint16_inplace_avx2,
                                              IsaRequirement::Avx2},
          "7bdc59443981516a"),
  };
  for (const auto& variant : {
           Variant<LayerFrameInvert16Function>{"sse2", invert_frame_uint16_inplace_sse2,
                                                IsaRequirement::Sse2},
           Variant<LayerFrameInvert16Function>{"avx2", invert_frame_uint16_inplace_avx2,
                                                IsaRequirement::Avx2}}) {
    cases.push_back(make_layer_frame_invert_16_case(
        128, 5, 128, 0x0f1e2d3c4b5a6978ULL, variant, "d345be7dbcf36a8d", 0xF30F2202U));
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
  constexpr std::size_t destination_alignment_offset = 3;
  constexpr std::size_t overlay_alignment_offset = 5;
  std::vector<LayerYuy2FastCase> cases{make_layer_yuy2_fast_case(
      width_pixels, height, destination_pitch, overlay_pitch, destination_alignment_offset,
      overlay_alignment_offset,
      Variant<LayerYuy2FastFunction>{"avx2", layer_yuy2_or_rgb32_fast_avx2, IsaRequirement::Avx2},
      "467d20be64ba47ef")};
  cases.push_back(make_layer_yuy2_fast_case(
      31, 7, 80, 96, 7, 11,
      Variant<LayerYuy2FastFunction>{"avx2", layer_yuy2_or_rgb32_fast_avx2, IsaRequirement::Avx2},
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

std::string layer_packed_blend_expected_hash(bool has_separate_mask, int opacity) {
  if (has_separate_mask) {
    return opacity == 255 ? "f8ed38a808275fff" : "43898c99879818e0";
  }
  return opacity == 255 ? "7340a277058b7fe7" : "f4637421ba6b129d";
}

std::vector<LayerPackedBlendCase> layer_packed_blend_cases() {
  std::vector<LayerPackedBlendCase> cases;
  constexpr std::size_t width = 9;
  constexpr std::size_t height = 3;
  constexpr std::size_t destination_pitch = 64;
  constexpr std::size_t overlay_pitch = 80;
  constexpr std::size_t mask_pitch = 32;
  for (const bool has_separate_mask : {false, true}) {
    for (const auto [opacity, opacity_name] :
         {std::pair{255, std::string{"Full255"}}, std::pair{173, std::string{"Partial173"}}}) {
      cases.push_back(make_layer_packed_blend_case(
          has_separate_mask, width, height, destination_pitch, overlay_pitch, mask_pitch, opacity,
          opacity_name, Variant<LayerPackedBlendFuncPtr>{"sse41", nullptr, IsaRequirement::Sse41},
          layer_packed_blend_expected_hash(has_separate_mask, opacity)));
      get_layer_packedrgb_blend_functions_sse41(has_separate_mask, 8,
                                                &cases.back().variant.function);
      cases.back().name = layer_packed_blend_case_name(cases.back());
      cases.push_back(make_layer_packed_blend_case(
          has_separate_mask, width, height, destination_pitch, overlay_pitch, mask_pitch, opacity,
          opacity_name, Variant<LayerPackedBlendFuncPtr>{"avx2", nullptr, IsaRequirement::Avx2},
          layer_packed_blend_expected_hash(has_separate_mask, opacity)));
      get_layer_packedrgb_blend_functions_avx2(has_separate_mask, 8,
                                               &cases.back().variant.function);
      cases.back().name = layer_packed_blend_case_name(cases.back());
    }
  }
  for (const bool has_separate_mask : {false, true}) {
    const auto expected_hash = has_separate_mask ? "5c65743a4a7186a1" : "ed9f1b3092c1d01d";
    cases.push_back(make_layer_packed_blend_case(
        has_separate_mask, 13, 7, 64, 80, 32, 173, "Partial173",
        Variant<LayerPackedBlendFuncPtr>{"sse41", nullptr, IsaRequirement::Sse41}, expected_hash,
        0xF30F1C01U));
    get_layer_packedrgb_blend_functions_sse41(has_separate_mask, 8,
                                              &cases.back().variant.function);
    cases.back().name = layer_packed_blend_case_name(cases.back());
    cases.push_back(make_layer_packed_blend_case(
        has_separate_mask, 13, 7, 64, 80, 32, 173, "Partial173",
        Variant<LayerPackedBlendFuncPtr>{"avx2", nullptr, IsaRequirement::Avx2}, expected_hash,
        0xF30F1C01U));
    get_layer_packedrgb_blend_functions_avx2(has_separate_mask, 8,
                                             &cases.back().variant.function);
    cases.back().name = layer_packed_blend_case_name(cases.back());
  }
  return cases;
}

class LayerPackedBlendKernels : public ::testing::TestWithParam<LayerPackedBlendCase> {};

TEST_P(LayerPackedBlendKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!test_case.variant.function) {
    GTEST_SKIP() << "upstream did not provide " << test_case.variant.name
                 << " packed RGB blend function";
  }
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_packed_blend_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerPackedBlendKernels,
                         ::testing::ValuesIn(layer_packed_blend_cases()),
                         [](const ::testing::TestParamInfo<LayerPackedBlendCase>& info) {
                           return info.param.name;
                         });

std::string layer_planarrgb_add_expected_hash(bool blend_alpha, int bits_per_pixel,
                                              bool full_opacity) {
  if (bits_per_pixel == 8) {
    if (blend_alpha) {
      return full_opacity ? "0e7a603078865d27" : "75680622264aad12";
    }
    return full_opacity ? "65773d6eb085583d" : "72a225f6f71380e3";
  }
  if (blend_alpha) {
    return full_opacity ? "94fcfcd368ebb5f1" : "b54328941bb23a77";
  }
  return full_opacity ? "173587b424c0f452" : "0b6d809dfd24a08e";
}

std::vector<LayerPlanarRgbAddCase> layer_planarrgb_add_cases() {
  std::vector<LayerPlanarRgbAddCase> cases;
  for (const int bits_per_pixel : {8, 16}) {
    const auto max_value = (1 << bits_per_pixel) - 1;
    const auto width = bits_per_pixel == 8 ? std::size_t{37} : std::size_t{19};
    const auto destination_pitch = bits_per_pixel == 8 ? std::size_t{64} : std::size_t{96};
    const auto overlay_pitch = bits_per_pixel == 8 ? std::size_t{80} : std::size_t{112};
    const auto mask_pitch = bits_per_pixel == 8 ? std::size_t{64} : std::size_t{96};
    for (const bool blend_alpha : {false, true}) {
      for (const int opacity : {max_value, max_value * 3 / 5}) {
        const auto opacity_name = opacity == max_value ? "Max" + std::to_string(max_value)
                                                       : "Partial" + std::to_string(opacity);
        cases.push_back(make_layer_planarrgb_add_case(
            blend_alpha, bits_per_pixel, width, 3, destination_pitch, overlay_pitch, mask_pitch,
            opacity, opacity_name, "sse41", IsaRequirement::Sse41, false,
            layer_planarrgb_add_expected_hash(blend_alpha, bits_per_pixel, opacity == max_value)));
        cases.push_back(make_layer_planarrgb_add_case(
            blend_alpha, bits_per_pixel, width, 3, destination_pitch, overlay_pitch, mask_pitch,
            opacity, opacity_name, "avx2", IsaRequirement::Avx2, true,
            layer_planarrgb_add_expected_hash(blend_alpha, bits_per_pixel, opacity == max_value)));
      }
    }
  }
  cases.push_back(make_layer_planarrgb_add_case(
      true, 8, 37, 3, 64, 80, 64, 153, "Partial153", "sse41", IsaRequirement::Sse41, false,
      "3cd5bd02a423c7ce", 0xF30F1D01U));
  cases.push_back(make_layer_planarrgb_add_case(
      true, 8, 37, 3, 64, 80, 64, 153, "Partial153", "avx2", IsaRequirement::Avx2, true,
      "3cd5bd02a423c7ce", 0xF30F1D01U));
  cases.push_back(make_layer_planarrgb_add_case(
      false, 16, 19, 3, 96, 112, 96, 39321, "Partial39321", "sse41", IsaRequirement::Sse41,
      false, "e38ad4005cdc3354", 0xF30F1D02U));
  cases.push_back(make_layer_planarrgb_add_case(
      false, 16, 19, 3, 96, 112, 96, 39321, "Partial39321", "avx2", IsaRequirement::Avx2, true,
      "e38ad4005cdc3354", 0xF30F1D02U));
  return cases;
}

class LayerPlanarRgbAddKernels : public ::testing::TestWithParam<LayerPlanarRgbAddCase> {};

TEST_P(LayerPlanarRgbAddKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!test_case.variant.function) {
    GTEST_SKIP() << "upstream did not provide " << test_case.variant.name
                 << " planar RGB add function";
  }
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bits_per_pixel == 8) {
    run_layer_planarrgb_add_case<std::uint8_t>(test_case);
  } else {
    run_layer_planarrgb_add_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerPlanarRgbAddKernels,
                         ::testing::ValuesIn(layer_planarrgb_add_cases()),
                         [](const ::testing::TestParamInfo<LayerPlanarRgbAddCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerPlanarRgbMulCase> layer_planarrgb_mul_cases() {
  constexpr std::size_t width_8 = 13;
  constexpr std::size_t height_8 = 5;
  constexpr std::size_t destination_pitch_8 = 64;
  constexpr std::size_t overlay_pitch_8 = 80;
  constexpr std::size_t mask_pitch_8 = 64;
  constexpr int opacity_8 = 173;
  constexpr std::uint32_t seed_8 = 0xF30F2601U;
  constexpr std::size_t width_16 = 19;
  constexpr std::size_t height_16 = 3;
  constexpr std::size_t destination_pitch_16 = 96;
  constexpr std::size_t overlay_pitch_16 = 112;
  constexpr std::size_t mask_pitch_16 = 96;
  constexpr int opacity_16 = 39321;
  constexpr std::uint32_t seed_16 = 0xF30F2602U;

  return {
      make_layer_planarrgb_mul_case(
          true, false, false, 8, width_8, height_8, destination_pitch_8, overlay_pitch_8, 0,
          opacity_8, "Partial173", seed_8, "d7d14f69613e8ab7"),
      make_layer_planarrgb_mul_case(
          true, true, false, 8, width_8, height_8, destination_pitch_8, overlay_pitch_8,
          mask_pitch_8, opacity_8, "Partial173", seed_8, "cbde729c940360ea"),
      make_layer_planarrgb_mul_case(
          false, true, true, 8, width_8, height_8, destination_pitch_8, overlay_pitch_8,
          mask_pitch_8, opacity_8, "Partial173", seed_8, "5da3d80d721eb390"),
      make_layer_planarrgb_mul_case(
          false, false, false, 16, width_16, height_16, destination_pitch_16, overlay_pitch_16, 0,
          opacity_16, "Partial39321", seed_16, "22b6fd463dc8a1df"),
      make_layer_planarrgb_mul_case(
          false, true, false, 16, width_16, height_16, destination_pitch_16, overlay_pitch_16,
          mask_pitch_16, opacity_16, "Partial39321", seed_16, "f14bf6c13c9f2146"),
      make_layer_planarrgb_mul_case(
          true, true, true, 16, width_16, height_16, destination_pitch_16, overlay_pitch_16,
          mask_pitch_16, opacity_16, "Partial39321", seed_16, "26015322e993a08b"),
  };
}

class LayerPlanarRgbMulKernels : public ::testing::TestWithParam<LayerPlanarRgbMulCase> {};

TEST_P(LayerPlanarRgbMulKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!test_case.variant.function) {
    GTEST_SKIP() << "upstream did not provide " << test_case.variant.name
                 << " planar RGB multiply function";
  }
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bits_per_pixel == 8) {
    run_layer_planarrgb_mul_case<std::uint8_t>(test_case);
  } else {
    run_layer_planarrgb_mul_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerPlanarRgbMulKernels,
                         ::testing::ValuesIn(layer_planarrgb_mul_cases()),
                         [](const ::testing::TestParamInfo<LayerPlanarRgbMulCase>& info) {
                           return info.param.name;
                         });

std::vector<LayerPlanarRgbLightenDarkenCase> layer_planarrgb_lighten_darken_cases() {
  constexpr std::size_t width_8 = 13;
  constexpr std::size_t height_8 = 5;
  constexpr std::size_t destination_pitch_8 = 64;
  constexpr std::size_t overlay_pitch_8 = 80;
  constexpr std::size_t mask_pitch_8 = 64;
  constexpr int opacity_8 = 173;
  constexpr int threshold_8 = 5;
  constexpr std::uint32_t seed_8 = 0xF30F2701U;
  constexpr std::array<const char*, 6> expected_hashes_8{
      "d33bfa57a0ccd32b", "30cef31c41f99edc", "12e881b2fdfcd8af",
      "063b67c29df51430", "d9cd18d9fcf683af", "31d7f46c31e68b6e"};
  constexpr std::size_t width_16 = 19;
  constexpr std::size_t height_16 = 3;
  constexpr std::size_t destination_pitch_16 = 96;
  constexpr std::size_t overlay_pitch_16 = 112;
  constexpr std::size_t mask_pitch_16 = 96;
  constexpr int opacity_16 = 39321;
  constexpr int threshold_16 = 1024;
  constexpr std::uint32_t seed_16 = 0xF30F2702U;
  constexpr std::array<const char*, 6> expected_hashes_16{
      "3904d7f20cf3229d", "b3fe8ee978f1cb1b", "6d157cb630a939b0",
      "85c55acf49cc1fff", "bd3640c18e728de2", "767a931cf4887977"};
  std::vector<LayerPlanarRgbLightenDarkenCase> cases;
  std::size_t hash_index = 0;
  for (const auto mode : {LayerPlanarRgbLightenDarkenMode::Lighten,
                          LayerPlanarRgbLightenDarkenMode::Darken}) {
    for (const auto& alpha_case : {
             std::tuple<bool, bool, std::size_t>{false, false, 0},
             std::tuple<bool, bool, std::size_t>{true, false, mask_pitch_8},
             std::tuple<bool, bool, std::size_t>{true, true, mask_pitch_8}}) {
      const auto [has_alpha, blend_alpha, mask_pitch] = alpha_case;
      cases.push_back(make_layer_planarrgb_lighten_darken_case(
          mode, has_alpha, blend_alpha, 8, width_8, height_8, destination_pitch_8,
          overlay_pitch_8, mask_pitch, opacity_8, "Partial173", threshold_8, "5", seed_8,
          expected_hashes_8[hash_index++]));
    }
  }
  hash_index = 0;
  for (const auto mode : {LayerPlanarRgbLightenDarkenMode::Lighten,
                          LayerPlanarRgbLightenDarkenMode::Darken}) {
    for (const auto& alpha_case : {
             std::tuple<bool, bool, std::size_t>{false, false, 0},
             std::tuple<bool, bool, std::size_t>{true, false, mask_pitch_16},
             std::tuple<bool, bool, std::size_t>{true, true, mask_pitch_16}}) {
      const auto [has_alpha, blend_alpha, mask_pitch] = alpha_case;
      cases.push_back(make_layer_planarrgb_lighten_darken_case(
          mode, has_alpha, blend_alpha, 16, width_16, height_16, destination_pitch_16,
          overlay_pitch_16, mask_pitch, opacity_16, "Partial39321", threshold_16, "1024", seed_16,
          expected_hashes_16[hash_index++]));
    }
  }
  return cases;
}

class LayerPlanarRgbLightenDarkenKernels
    : public ::testing::TestWithParam<LayerPlanarRgbLightenDarkenCase> {};

TEST_P(LayerPlanarRgbLightenDarkenKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!test_case.variant.function) {
    GTEST_SKIP() << "upstream did not provide " << test_case.variant.name
                 << " planar RGB lighten/darken function";
  }
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bits_per_pixel == 8) {
    run_layer_planarrgb_lighten_darken_case<std::uint8_t>(test_case);
  } else {
    run_layer_planarrgb_lighten_darken_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Kernels, LayerPlanarRgbLightenDarkenKernels,
    ::testing::ValuesIn(layer_planarrgb_lighten_darken_cases()),
    [](const ::testing::TestParamInfo<LayerPlanarRgbLightenDarkenCase>& info) {
      return info.param.name;
    });

std::vector<LayerPlanarRgbMulFloatCase> layer_planarrgb_mul_float_cases() {
  constexpr std::size_t width = 7;
  constexpr std::size_t height = 3;
  constexpr std::size_t destination_pitch = 32;
  constexpr std::size_t overlay_pitch = 48;
  constexpr std::size_t mask_pitch = 32;
  constexpr float opacity = 0.37F;
  return {
      make_layer_planarrgb_mul_float_case(false, false, false, width, height, destination_pitch,
                                          overlay_pitch, 0, opacity, "37Pct"),
      make_layer_planarrgb_mul_float_case(true, false, false, width, height, destination_pitch,
                                          overlay_pitch, 0, opacity, "37Pct"),
      make_layer_planarrgb_mul_float_case(false, true, false, width, height, destination_pitch,
                                          overlay_pitch, mask_pitch, opacity, "37Pct"),
      make_layer_planarrgb_mul_float_case(true, true, false, width, height, destination_pitch,
                                          overlay_pitch, mask_pitch, opacity, "37Pct"),
      make_layer_planarrgb_mul_float_case(false, true, true, width, height, destination_pitch,
                                          overlay_pitch, mask_pitch, opacity, "37Pct"),
      make_layer_planarrgb_mul_float_case(true, true, true, width, height, destination_pitch,
                                          overlay_pitch, mask_pitch, opacity, "37Pct"),
  };
}

class LayerPlanarRgbMulFloatKernels : public ::testing::TestWithParam<LayerPlanarRgbMulFloatCase> {};

TEST_P(LayerPlanarRgbMulFloatKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!test_case.variant.function) {
    GTEST_SKIP() << "upstream did not provide " << test_case.variant.name
                 << " float planar RGB multiply function";
  }
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_planarrgb_mul_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(
    Kernels, LayerPlanarRgbMulFloatKernels, ::testing::ValuesIn(layer_planarrgb_mul_float_cases()),
    [](const ::testing::TestParamInfo<LayerPlanarRgbMulFloatCase>& info) {
      return info.param.name;
    });

std::vector<LayerPlanarRgbLightenDarkenFloatCase>
layer_planarrgb_lighten_darken_float_cases() {
  constexpr std::size_t width = 7;
  constexpr std::size_t height = 3;
  constexpr std::size_t destination_pitch = 32;
  constexpr std::size_t overlay_pitch = 48;
  constexpr std::size_t mask_pitch = 32;
  constexpr float opacity = 0.37F;
  constexpr float threshold = 0.05F;
  std::vector<LayerPlanarRgbLightenDarkenFloatCase> cases;
  for (const bool lighten : {true, false}) {
    for (const auto& alpha_case : {
             std::tuple<bool, bool, std::size_t>{false, false, 0},
             std::tuple<bool, bool, std::size_t>{true, false, mask_pitch},
             std::tuple<bool, bool, std::size_t>{true, true, mask_pitch}}) {
      const auto [has_alpha, blend_alpha, case_mask_pitch] = alpha_case;
      cases.push_back(make_layer_planarrgb_lighten_darken_float_case(
          lighten, has_alpha, blend_alpha, width, height, destination_pitch, overlay_pitch,
          case_mask_pitch, opacity, "37Pct", threshold, "0p05"));
    }
  }
  return cases;
}

class LayerPlanarRgbLightenDarkenFloatKernels
    : public ::testing::TestWithParam<LayerPlanarRgbLightenDarkenFloatCase> {};

TEST_P(LayerPlanarRgbLightenDarkenFloatKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!test_case.variant.function) {
    GTEST_SKIP() << "upstream did not provide " << test_case.variant.name
                 << " float planar RGB lighten/darken function";
  }
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_planarrgb_lighten_darken_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(
    Kernels, LayerPlanarRgbLightenDarkenFloatKernels,
    ::testing::ValuesIn(layer_planarrgb_lighten_darken_float_cases()),
    [](const ::testing::TestParamInfo<LayerPlanarRgbLightenDarkenFloatCase>& info) {
      return info.param.name;
    });

std::vector<LayerPlanarRgbAddFloatCase> layer_planarrgb_add_float_cases() {
  constexpr std::size_t width = 7;
  constexpr std::size_t height = 3;
  constexpr std::size_t destination_pitch = 32;
  constexpr std::size_t overlay_pitch = 48;
  constexpr std::size_t mask_pitch = 32;
  constexpr float opacity = 0.37F;
  std::vector<LayerPlanarRgbAddFloatCase> cases;
  for (const bool chroma : {false, true}) {
    for (const auto& alpha_case : {
             std::tuple<bool, bool, std::size_t>{false, false, 0},
             std::tuple<bool, bool, std::size_t>{true, false, mask_pitch},
             std::tuple<bool, bool, std::size_t>{true, true, mask_pitch}}) {
      const auto [has_alpha, blend_alpha, case_mask_pitch] = alpha_case;
      cases.push_back(make_layer_planarrgb_add_float_case(
          chroma, has_alpha, blend_alpha, width, height, destination_pitch, overlay_pitch,
          case_mask_pitch, opacity, "37Pct"));
    }
  }
  return cases;
}

class LayerPlanarRgbAddFloatKernels : public ::testing::TestWithParam<LayerPlanarRgbAddFloatCase> {};

TEST_P(LayerPlanarRgbAddFloatKernels, MatchesIndependentReference) {
  const auto& test_case = GetParam();
  if (!test_case.variant.function) {
    GTEST_SKIP() << "upstream did not provide " << test_case.variant.name
                 << " float planar RGB add function";
  }
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_layer_planarrgb_add_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(
    Kernels, LayerPlanarRgbAddFloatKernels, ::testing::ValuesIn(layer_planarrgb_add_float_cases()),
    [](const ::testing::TestParamInfo<LayerPlanarRgbAddFloatCase>& info) {
      return info.param.name;
    });

}  // namespace
}  // namespace avsut::test
