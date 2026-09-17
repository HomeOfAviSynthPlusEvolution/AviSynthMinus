#include <gtest/gtest.h>

#include <vector>

#include "focus_float_test_helpers.h"
#include "focus_packed_test_helpers.h"
#include "focus_test_helpers.h"
#include "support/kernel_cpu_profiles.h"

namespace avsut::test {
namespace {

constexpr std::size_t kBlurAmount = 16384;
constexpr std::size_t kSharpenAmount = 49152;

std::vector<FocusHorizontal8Case> focus_horizontal8_cases() {
  std::vector<FocusHorizontal8Case> cases;
  for (const auto amount : {kBlurAmount, kSharpenAmount}) {
    cases.push_back(
        make_focus_horizontal8_case(37, 5, 64, amount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                    amount == kBlurAmount ? "96f6319cb1fc053f" : "5ce7eeb81c783d33"));
    cases.push_back(
        make_focus_horizontal8_case(67, 5, 96, amount, KernelCpuProfile{"avx2", AIF_FOCUS_AVX2},
                                    amount == kBlurAmount ? "aadf7fda96287d75" : "c68060bea1c036ea"));
  }
  cases.push_back(make_focus_horizontal8_case(
      53, 7, 80, kBlurAmount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2}, "99a2f1d81fb25341", 0xF30F0C01U));
  cases.push_back(make_focus_horizontal8_case(
      53, 7, 96, kBlurAmount, KernelCpuProfile{"avx2", AIF_FOCUS_AVX2}, "99a2f1d81fb25341", 0xF30F0C01U));
  return cases;
}

std::vector<FocusHorizontal16Case> focus_horizontal16_cases() {
  std::vector<FocusHorizontal16Case> cases;
  for (const auto amount : {kBlurAmount, kSharpenAmount}) {
    cases.push_back(
        make_focus_horizontal16_case(19, 5, 64, amount, 16, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                     amount == kBlurAmount ? "db533c589a76d12c" : "362aacc9ba8a05ff"));
    cases.push_back(
        make_focus_horizontal16_case(19, 5, 64, amount, 16, KernelCpuProfile{"sse41", AIF_FOCUS_SSE41},
                                     amount == kBlurAmount ? "db533c589a76d12c" : "362aacc9ba8a05ff"));
    cases.push_back(
        make_focus_horizontal16_case(35, 5, 96, amount, 16, KernelCpuProfile{"avx2", AIF_FOCUS_AVX2},
                                     amount == kBlurAmount ? "7b8dd1fffa3dfcc9" : "b48673d2a3f16b46"));
  }
  cases.push_back(make_focus_horizontal16_case(
      27, 7, 80, kBlurAmount, 16, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2}, "7d51bef45b4b595c", 0xF30F0C02U));
  cases.push_back(make_focus_horizontal16_case(27, 7, 80, kBlurAmount, 16,
                                               KernelCpuProfile{"sse41", AIF_FOCUS_SSE41}, "7d51bef45b4b595c",
                                               0xF30F0C02U));
  cases.push_back(make_focus_horizontal16_case(
      27, 7, 96, kBlurAmount, 16, KernelCpuProfile{"avx2", AIF_FOCUS_AVX2}, "7d51bef45b4b595c", 0xF30F0C02U));
  return cases;
}

std::vector<FocusVertical8Case> focus_vertical8_cases() {
  std::vector<FocusVertical8Case> cases;
  for (const auto amount : {kBlurAmount, kSharpenAmount}) {
    cases.push_back(
        make_focus_vertical8_case(64, 5, 64, amount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                  amount == kBlurAmount ? "cc86e5ffb527ad5c" : "85c6e277c2156823"));
    cases.push_back(
        make_focus_vertical8_case(64, 5, 64, amount, KernelCpuProfile{"avx2", AIF_FOCUS_AVX2},
                                  amount == kBlurAmount ? "cc86e5ffb527ad5c" : "85c6e277c2156823"));
  }
  cases.push_back(make_focus_vertical8_case(64, 7, 80, kBlurAmount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                            "72dcbb0b2bef45b8", 0xF30F0C03U));
  cases.push_back(make_focus_vertical8_case(64, 7, 96, kBlurAmount, KernelCpuProfile{"avx2", AIF_FOCUS_AVX2},
                                            "72dcbb0b2bef45b8", 0xF30F0C03U));
  return cases;
}

std::vector<FocusVertical16Case> focus_vertical16_cases() {
  std::vector<FocusVertical16Case> cases;
  for (const auto amount : {kBlurAmount, kSharpenAmount}) {
    cases.push_back(
        make_focus_vertical16_case(32, 5, 64, amount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                   amount == kBlurAmount ? "34d727de54e3bf82" : "ac8dcc0705230d02"));
    cases.push_back(
        make_focus_vertical16_case(32, 5, 64, amount, KernelCpuProfile{"sse41", AIF_FOCUS_SSE41},
                                   amount == kBlurAmount ? "34d727de54e3bf82" : "ac8dcc0705230d02"));
    cases.push_back(
        make_focus_vertical16_case(32, 5, 64, amount, KernelCpuProfile{"avx2", AIF_FOCUS_AVX2},
                                   amount == kBlurAmount ? "34d727de54e3bf82" : "ac8dcc0705230d02"));
  }
  cases.push_back(make_focus_vertical16_case(32, 7, 80, kBlurAmount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                             "a85909e830dc36b2", 0xF30F0C04U));
  cases.push_back(make_focus_vertical16_case(
      32, 7, 80, kBlurAmount, KernelCpuProfile{"sse41", AIF_FOCUS_SSE41}, "a85909e830dc36b2", 0xF30F0C04U));
  cases.push_back(make_focus_vertical16_case(32, 7, 96, kBlurAmount, KernelCpuProfile{"avx2", AIF_FOCUS_AVX2},
                                             "a85909e830dc36b2", 0xF30F0C04U));
  return cases;
}

std::vector<FocusHorizontalFloatCase> focus_horizontal_float_cases() {
  return {
      make_focus_horizontal_float_case(7, 5, 32, 0.5F, "Half", KernelCpuProfile{"sse2", AIF_FOCUS_SSE2}),
      make_focus_horizontal_float_case(7, 5, 32, 1.5F, "OneHalf", KernelCpuProfile{"sse2", AIF_FOCUS_SSE2}),
  };
}

std::vector<FocusVerticalFloatCase> focus_vertical_float_cases() {
  return {
      make_focus_vertical_float_case(8, 5, 32, 0.5F, "Half", KernelCpuProfile{"sse2", AIF_FOCUS_SSE2}),
      make_focus_vertical_float_case(8, 5, 32, 1.5F, "OneHalf", KernelCpuProfile{"sse2", AIF_FOCUS_SSE2}),
  };
}

std::vector<FocusRgb32Case> focus_rgb32_cases() {
  return {
      make_focus_rgb32_case(7, 5, 32, 32, kBlurAmount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                            "2dd17d4de21c9c2b"),
      make_focus_rgb32_case(7, 5, 32, 32, kSharpenAmount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                            "9c9ee4d13a416a64"),
  };
}

std::vector<FocusRgb64Case> focus_rgb64_cases() {
  std::vector<FocusRgb64Case> cases;
  for (const auto amount : {kBlurAmount, kSharpenAmount}) {
    cases.push_back(make_focus_rgb64_case(5, 5, 64, 64, amount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                                          amount == kBlurAmount ? "9a939ecde59bb7f4" : "23061c7dde79a0b2"));
    cases.push_back(make_focus_rgb64_case(5, 5, 64, 64, amount, KernelCpuProfile{"sse41", AIF_FOCUS_SSE41},
                                          amount == kBlurAmount ? "9a939ecde59bb7f4" : "23061c7dde79a0b2"));
  }
  return cases;
}

std::vector<FocusYuy2Case> focus_yuy2_cases() {
  return {
      make_focus_yuy2_case(12, 5, 32, 32, kBlurAmount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                           "c828b6733ff912ef"),
      make_focus_yuy2_case(12, 5, 32, 32, kSharpenAmount, KernelCpuProfile{"sse2", AIF_FOCUS_SSE2},
                           "aef10b7de6d41c09"),
  };
}

class FocusHorizontal8Kernels : public ::testing::TestWithParam<FocusHorizontal8Case> {};

TEST_P(FocusHorizontal8Kernels, MatchesThreeTapReference) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_focus_horizontal8_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusHorizontal8Kernels, ::testing::ValuesIn(focus_horizontal8_cases()),
                         [](const ::testing::TestParamInfo<FocusHorizontal8Case>& info) {
                           return info.param.name;
                         });

class FocusHorizontal16Kernels : public ::testing::TestWithParam<FocusHorizontal16Case> {};

TEST_P(FocusHorizontal16Kernels, MatchesThreeTapReference) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_focus_horizontal16_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusHorizontal16Kernels, ::testing::ValuesIn(focus_horizontal16_cases()),
                         [](const ::testing::TestParamInfo<FocusHorizontal16Case>& info) {
                           return info.param.name;
                         });

class FocusVertical8Kernels : public ::testing::TestWithParam<FocusVertical8Case> {};

TEST_P(FocusVertical8Kernels, MatchesThreeTapReference) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_focus_vertical_case<std::uint8_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusVertical8Kernels, ::testing::ValuesIn(focus_vertical8_cases()),
                         [](const ::testing::TestParamInfo<FocusVertical8Case>& info) {
                           return info.param.name;
                         });

class FocusVertical16Kernels : public ::testing::TestWithParam<FocusVertical16Case> {};

TEST_P(FocusVertical16Kernels, MatchesThreeTapReference) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_focus_vertical_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusVertical16Kernels, ::testing::ValuesIn(focus_vertical16_cases()),
                         [](const ::testing::TestParamInfo<FocusVertical16Case>& info) {
                           return info.param.name;
                         });

class FocusHorizontalFloatKernels : public ::testing::TestWithParam<FocusHorizontalFloatCase> {};

TEST_P(FocusHorizontalFloatKernels, MatchesThreeTapReference) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_focus_horizontal_float_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusHorizontalFloatKernels,
                         ::testing::ValuesIn(focus_horizontal_float_cases()),
                         [](const ::testing::TestParamInfo<FocusHorizontalFloatCase>& info) {
                           return info.param.name;
                         });

class FocusVerticalFloatKernels : public ::testing::TestWithParam<FocusVerticalFloatCase> {};

TEST_P(FocusVerticalFloatKernels, MatchesThreeTapReference) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_focus_vertical_float_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusVerticalFloatKernels,
                         ::testing::ValuesIn(focus_vertical_float_cases()),
                         [](const ::testing::TestParamInfo<FocusVerticalFloatCase>& info) {
                           return info.param.name;
                         });

class FocusRgb32Kernels : public ::testing::TestWithParam<FocusRgb32Case> {};

TEST_P(FocusRgb32Kernels, MatchesThreeTapReference) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_focus_rgb_case<std::uint8_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusRgb32Kernels, ::testing::ValuesIn(focus_rgb32_cases()),
                         [](const ::testing::TestParamInfo<FocusRgb32Case>& info) {
                           return info.param.name;
                         });

class FocusRgb64Kernels : public ::testing::TestWithParam<FocusRgb64Case> {};

TEST_P(FocusRgb64Kernels, MatchesThreeTapReference) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_focus_rgb_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusRgb64Kernels, ::testing::ValuesIn(focus_rgb64_cases()),
                         [](const ::testing::TestParamInfo<FocusRgb64Case>& info) {
                           return info.param.name;
                         });

class FocusYuy2Kernels : public ::testing::TestWithParam<FocusYuy2Case> {};

TEST_P(FocusYuy2Kernels, MatchesThreeTapReference) {
  auto test_case = GetParam();
  for (const auto cpu : kernel_cpu_profiles(test_case.variant.cpu, aif_focus_supported_cpu())) {
    SCOPED_TRACE(cpu);
    test_case.variant.cpu = cpu;
    run_focus_yuy2_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, FocusYuy2Kernels, ::testing::ValuesIn(focus_yuy2_cases()),
                         [](const ::testing::TestParamInfo<FocusYuy2Case>& info) { return info.param.name; });

}  // namespace
}  // namespace avsut::test
