#include <gtest/gtest.h>

#include "focus_float_test_helpers.h"
#include "focus_packed_test_helpers.h"
#include "focus_test_helpers.h"

#include "support/cpu_features.h"

#include <vector>

namespace avsut::test {
namespace {

constexpr std::size_t kBlurAmount = 16384;
constexpr std::size_t kSharpenAmount = 49152;

std::vector<FocusHorizontal8Case> focus_horizontal8_cases() {
  std::vector<FocusHorizontal8Case> cases;
  for (const auto amount : {kBlurAmount, kSharpenAmount}) {
    cases.push_back(make_focus_horizontal8_case(
        37, 5, 64, amount,
        Variant<FocusHorizontal8FuncPtr>{"sse2", af_horizontal_planar_sse2, IsaRequirement::Sse2},
        amount == kBlurAmount ? "96f6319cb1fc053f" : "5ce7eeb81c783d33"));
    cases.push_back(make_focus_horizontal8_case(
        67, 5, 96, amount,
        Variant<FocusHorizontal8FuncPtr>{"avx2", af_horizontal_planar_avx2, IsaRequirement::Avx2},
        amount == kBlurAmount ? "aadf7fda96287d75" : "c68060bea1c036ea"));
  }
  cases.push_back(make_focus_horizontal8_case(
      53, 7, 80, kBlurAmount,
      Variant<FocusHorizontal8FuncPtr>{"sse2", af_horizontal_planar_sse2, IsaRequirement::Sse2},
      "99a2f1d81fb25341", 0xF30F0C01U));
  cases.push_back(make_focus_horizontal8_case(
      53, 7, 96, kBlurAmount,
      Variant<FocusHorizontal8FuncPtr>{"avx2", af_horizontal_planar_avx2, IsaRequirement::Avx2},
      "99a2f1d81fb25341", 0xF30F0C01U));
  return cases;
}

std::vector<FocusHorizontal16Case> focus_horizontal16_cases() {
  std::vector<FocusHorizontal16Case> cases;
  for (const auto amount : {kBlurAmount, kSharpenAmount}) {
    cases.push_back(make_focus_horizontal16_case(
        19, 5, 64, amount, 16,
        Variant<FocusHorizontal16FuncPtr>{"sse2", af_horizontal_planar_uint16_t_sse2,
                                          IsaRequirement::Sse2},
        amount == kBlurAmount ? "db533c589a76d12c" : "362aacc9ba8a05ff"));
    cases.push_back(make_focus_horizontal16_case(
        19, 5, 64, amount, 16,
        Variant<FocusHorizontal16FuncPtr>{"sse41", af_horizontal_planar_uint16_t_sse41,
                                          IsaRequirement::Sse41},
        amount == kBlurAmount ? "db533c589a76d12c" : "362aacc9ba8a05ff"));
    cases.push_back(make_focus_horizontal16_case(
        35, 5, 96, amount, 16,
        Variant<FocusHorizontal16FuncPtr>{"avx2", af_horizontal_planar_uint16_t_avx2,
                                          IsaRequirement::Avx2},
        amount == kBlurAmount ? "7b8dd1fffa3dfcc9" : "b48673d2a3f16b46"));
  }
  cases.push_back(make_focus_horizontal16_case(
      27, 7, 80, kBlurAmount, 16,
      Variant<FocusHorizontal16FuncPtr>{"sse2", af_horizontal_planar_uint16_t_sse2,
                                        IsaRequirement::Sse2},
      "7d51bef45b4b595c", 0xF30F0C02U));
  cases.push_back(make_focus_horizontal16_case(
      27, 7, 80, kBlurAmount, 16,
      Variant<FocusHorizontal16FuncPtr>{"sse41", af_horizontal_planar_uint16_t_sse41,
                                        IsaRequirement::Sse41},
      "7d51bef45b4b595c", 0xF30F0C02U));
  cases.push_back(make_focus_horizontal16_case(
      27, 7, 96, kBlurAmount, 16,
      Variant<FocusHorizontal16FuncPtr>{"avx2", af_horizontal_planar_uint16_t_avx2,
                                        IsaRequirement::Avx2},
      "7d51bef45b4b595c", 0xF30F0C02U));
  return cases;
}

std::vector<FocusVertical8Case> focus_vertical8_cases() {
  std::vector<FocusVertical8Case> cases;
  for (const auto amount : {kBlurAmount, kSharpenAmount}) {
    cases.push_back(make_focus_vertical8_case(
        64, 5, 64, amount,
        Variant<FocusVertical8FuncPtr>{"sse2", af_vertical_sse2, IsaRequirement::Sse2},
        amount == kBlurAmount ? "cc86e5ffb527ad5c" : "85c6e277c2156823"));
    cases.push_back(make_focus_vertical8_case(
        64, 5, 64, amount,
        Variant<FocusVertical8FuncPtr>{"avx2", af_vertical_avx2, IsaRequirement::Avx2},
        amount == kBlurAmount ? "cc86e5ffb527ad5c" : "85c6e277c2156823"));
  }
  cases.push_back(make_focus_vertical8_case(
      64, 7, 80, kBlurAmount,
      Variant<FocusVertical8FuncPtr>{"sse2", af_vertical_sse2, IsaRequirement::Sse2},
      "72dcbb0b2bef45b8",
      0xF30F0C03U));
  cases.push_back(make_focus_vertical8_case(
      64, 7, 96, kBlurAmount,
      Variant<FocusVertical8FuncPtr>{"avx2", af_vertical_avx2, IsaRequirement::Avx2},
      "72dcbb0b2bef45b8",
      0xF30F0C03U));
  return cases;
}

std::vector<FocusVertical16Case> focus_vertical16_cases() {
  std::vector<FocusVertical16Case> cases;
  for (const auto amount : {kBlurAmount, kSharpenAmount}) {
    cases.push_back(make_focus_vertical16_case(
        32, 5, 64, amount,
        Variant<FocusVertical16FuncPtr>{"sse2", af_vertical_uint16_t_sse2, IsaRequirement::Sse2},
        amount == kBlurAmount ? "34d727de54e3bf82" : "ac8dcc0705230d02"));
    cases.push_back(make_focus_vertical16_case(
        32, 5, 64, amount,
        Variant<FocusVertical16FuncPtr>{"sse41", af_vertical_uint16_t_sse41, IsaRequirement::Sse41},
        amount == kBlurAmount ? "34d727de54e3bf82" : "ac8dcc0705230d02"));
    cases.push_back(make_focus_vertical16_case(
        32, 5, 64, amount,
        Variant<FocusVertical16FuncPtr>{"avx2", af_vertical_uint16_t_avx2, IsaRequirement::Avx2},
        amount == kBlurAmount ? "34d727de54e3bf82" : "ac8dcc0705230d02"));
  }
  cases.push_back(make_focus_vertical16_case(
      32, 7, 80, kBlurAmount,
      Variant<FocusVertical16FuncPtr>{"sse2", af_vertical_uint16_t_sse2, IsaRequirement::Sse2},
      "a85909e830dc36b2", 0xF30F0C04U));
  cases.push_back(make_focus_vertical16_case(
      32, 7, 80, kBlurAmount,
      Variant<FocusVertical16FuncPtr>{"sse41", af_vertical_uint16_t_sse41, IsaRequirement::Sse41},
      "a85909e830dc36b2", 0xF30F0C04U));
  cases.push_back(make_focus_vertical16_case(
      32, 7, 96, kBlurAmount,
      Variant<FocusVertical16FuncPtr>{"avx2", af_vertical_uint16_t_avx2, IsaRequirement::Avx2},
      "a85909e830dc36b2", 0xF30F0C04U));
  return cases;
}

std::vector<FocusHorizontalFloatCase> focus_horizontal_float_cases() {
  return {
      make_focus_horizontal_float_case(
          7, 5, 32, 0.5F, "Half",
          Variant<FocusHorizontalFloatFuncPtr>{"sse2", af_horizontal_planar_float_sse2,
                                               IsaRequirement::Sse2}),
      make_focus_horizontal_float_case(
          7, 5, 32, 1.5F, "OneHalf",
          Variant<FocusHorizontalFloatFuncPtr>{"sse2", af_horizontal_planar_float_sse2,
                                               IsaRequirement::Sse2}),
  };
}

std::vector<FocusVerticalFloatCase> focus_vertical_float_cases() {
  return {
      make_focus_vertical_float_case(
          8, 5, 32, 0.5F, "Half",
          Variant<FocusVerticalFloatFuncPtr>{"sse2", af_vertical_sse2_float, IsaRequirement::Sse2}),
      make_focus_vertical_float_case(
          8, 5, 32, 1.5F, "OneHalf",
          Variant<FocusVerticalFloatFuncPtr>{"sse2", af_vertical_sse2_float, IsaRequirement::Sse2}),
  };
}

std::vector<FocusRgb32Case> focus_rgb32_cases() {
  return {
      make_focus_rgb32_case(
          7, 5, 32, 32, kBlurAmount,
          Variant<FocusRgb32FuncPtr>{"sse2", af_horizontal_rgb32_sse2, IsaRequirement::Sse2},
          "2dd17d4de21c9c2b"),
      make_focus_rgb32_case(
          7, 5, 32, 32, kSharpenAmount,
          Variant<FocusRgb32FuncPtr>{"sse2", af_horizontal_rgb32_sse2, IsaRequirement::Sse2},
          "9c9ee4d13a416a64"),
  };
}

std::vector<FocusRgb64Case> focus_rgb64_cases() {
  std::vector<FocusRgb64Case> cases;
  for (const auto amount : {kBlurAmount, kSharpenAmount}) {
    cases.push_back(make_focus_rgb64_case(
        5, 5, 64, 64, amount,
        Variant<FocusRgb64FuncPtr>{"sse2", af_horizontal_rgb64_sse2, IsaRequirement::Sse2},
        amount == kBlurAmount ? "9a939ecde59bb7f4" : "23061c7dde79a0b2"));
    cases.push_back(make_focus_rgb64_case(
        5, 5, 64, 64, amount,
        Variant<FocusRgb64FuncPtr>{"sse41", af_horizontal_rgb64_sse41, IsaRequirement::Sse41},
        amount == kBlurAmount ? "9a939ecde59bb7f4" : "23061c7dde79a0b2"));
  }
  return cases;
}

std::vector<FocusYuy2Case> focus_yuy2_cases() {
  return {
      make_focus_yuy2_case(
          12, 5, 32, 32, kBlurAmount,
          Variant<FocusYuy2FuncPtr>{"sse2", af_horizontal_yuy2_sse2, IsaRequirement::Sse2},
          "c828b6733ff912ef"),
      make_focus_yuy2_case(
          12, 5, 32, 32, kSharpenAmount,
          Variant<FocusYuy2FuncPtr>{"sse2", af_horizontal_yuy2_sse2, IsaRequirement::Sse2},
          "aef10b7de6d41c09"),
  };
}

class FocusHorizontal8Kernels : public ::testing::TestWithParam<FocusHorizontal8Case> {};

TEST_P(FocusHorizontal8Kernels, MatchesThreeTapReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_focus_horizontal8_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusHorizontal8Kernels,
                         ::testing::ValuesIn(focus_horizontal8_cases()),
                         [](const ::testing::TestParamInfo<FocusHorizontal8Case>& info) {
                           return info.param.name;
                         });

class FocusHorizontal16Kernels : public ::testing::TestWithParam<FocusHorizontal16Case> {};

TEST_P(FocusHorizontal16Kernels, MatchesThreeTapReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_focus_horizontal16_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusHorizontal16Kernels,
                         ::testing::ValuesIn(focus_horizontal16_cases()),
                         [](const ::testing::TestParamInfo<FocusHorizontal16Case>& info) {
                           return info.param.name;
                         });

class FocusVertical8Kernels : public ::testing::TestWithParam<FocusVertical8Case> {};

TEST_P(FocusVertical8Kernels, MatchesThreeTapReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_focus_vertical_case<std::uint8_t>(test_case, test_case.variant.function);
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusVertical8Kernels,
                         ::testing::ValuesIn(focus_vertical8_cases()),
                         [](const ::testing::TestParamInfo<FocusVertical8Case>& info) {
                           return info.param.name;
                         });

class FocusVertical16Kernels : public ::testing::TestWithParam<FocusVertical16Case> {};

TEST_P(FocusVertical16Kernels, MatchesThreeTapReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_focus_vertical_case<std::uint16_t>(test_case, test_case.variant.function);
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusVertical16Kernels,
                         ::testing::ValuesIn(focus_vertical16_cases()),
                         [](const ::testing::TestParamInfo<FocusVertical16Case>& info) {
                           return info.param.name;
                         });

class FocusHorizontalFloatKernels : public ::testing::TestWithParam<FocusHorizontalFloatCase> {};

TEST_P(FocusHorizontalFloatKernels, MatchesThreeTapReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_focus_horizontal_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusHorizontalFloatKernels,
                         ::testing::ValuesIn(focus_horizontal_float_cases()),
                         [](const ::testing::TestParamInfo<FocusHorizontalFloatCase>& info) {
                           return info.param.name;
                         });

class FocusVerticalFloatKernels : public ::testing::TestWithParam<FocusVerticalFloatCase> {};

TEST_P(FocusVerticalFloatKernels, MatchesThreeTapReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_focus_vertical_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusVerticalFloatKernels,
                         ::testing::ValuesIn(focus_vertical_float_cases()),
                         [](const ::testing::TestParamInfo<FocusVerticalFloatCase>& info) {
                           return info.param.name;
                         });

class FocusRgb32Kernels : public ::testing::TestWithParam<FocusRgb32Case> {};

TEST_P(FocusRgb32Kernels, MatchesThreeTapReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_focus_rgb_case<std::uint8_t>(test_case, test_case.variant.function);
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusRgb32Kernels, ::testing::ValuesIn(focus_rgb32_cases()),
                         [](const ::testing::TestParamInfo<FocusRgb32Case>& info) {
                           return info.param.name;
                         });

class FocusRgb64Kernels : public ::testing::TestWithParam<FocusRgb64Case> {};

TEST_P(FocusRgb64Kernels, MatchesThreeTapReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  FocusRgb32Case adapted{
      test_case.width_pixels,
      test_case.height,
      test_case.source_pitch,
      test_case.destination_pitch,
      test_case.amount,
      Variant<FocusRgb32FuncPtr>{test_case.variant.name, test_case.variant.function,
                                 test_case.variant.requirement},
      test_case.expected_hash,
      test_case.name};
  run_focus_rgb_case<std::uint16_t>(adapted, test_case.variant.function);
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusRgb64Kernels, ::testing::ValuesIn(focus_rgb64_cases()),
                         [](const ::testing::TestParamInfo<FocusRgb64Case>& info) {
                           return info.param.name;
                         });

class FocusYuy2Kernels : public ::testing::TestWithParam<FocusYuy2Case> {};

TEST_P(FocusYuy2Kernels, MatchesThreeTapReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_focus_yuy2_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusYuy2Kernels, ::testing::ValuesIn(focus_yuy2_cases()),
                         [](const ::testing::TestParamInfo<FocusYuy2Case>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
