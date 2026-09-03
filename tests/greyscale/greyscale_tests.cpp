#include <gtest/gtest.h>

#include "greyscale_test_helpers.h"

#include "support/cpu_features.h"

#include <vector>

namespace avsut::test {
namespace {

std::vector<GreyscaleYuy2Case> greyscale_yuy2_cases() {
  return {
      make_greyscale_yuy2_case(
          64, 5, 64,
          Variant<GreyscaleYuy2FuncPtr>{"sse2", greyscale_yuy2_sse2, IsaRequirement::Sse2},
          "456f296978afdfbb"),
      make_greyscale_yuy2_case(
          80, 7, 80,
          Variant<GreyscaleYuy2FuncPtr>{"sse2", greyscale_yuy2_sse2, IsaRequirement::Sse2},
          "76c36c2259282a6d", 0xF30A5B01U),
  };
}

std::vector<GreyscaleRgb32Case> greyscale_rgb32_cases() {
  const int limited = ColorRange_Compat_e::AVS_COLORRANGE_LIMITED;
  const int full = ColorRange_Compat_e::AVS_COLORRANGE_FULL;
  const int rec601 = Matrix_e::AVS_MATRIX_BT470_BG;
  const int rec709 = Matrix_e::AVS_MATRIX_BT709;
  std::vector<GreyscaleRgb32Case> cases;
  for (const auto& item : {std::tuple{"Rec601", "Limited", rec601, limited},
                           std::tuple{"Rec709", "Full", rec709, full}}) {
    cases.push_back(make_greyscale_rgb32_case(
        std::get<0>(item), std::get<1>(item), std::get<2>(item), std::get<3>(item), 12, 5, 48,
        Variant<GreyscaleRgb32FuncPtr>{"sse2", greyscale_rgb32_sse2, IsaRequirement::Sse2},
        std::get<2>(item) == rec601 ? "599e57d273252700" : "5bb635aba3c3792a"));
  }
  cases.push_back(make_greyscale_rgb32_case(
      "Rec601", "Limited", rec601, limited, 20, 7, 80,
      Variant<GreyscaleRgb32FuncPtr>{"sse2", greyscale_rgb32_sse2, IsaRequirement::Sse2},
      "dde95d0ed1198f58", 0xF30A5B02U));
  cases.push_back(make_greyscale_rgb32_case(
      "Rec709", "Full", rec709, full, 20, 7, 80,
      Variant<GreyscaleRgb32FuncPtr>{"sse2", greyscale_rgb32_sse2, IsaRequirement::Sse2},
      "8fd32512e20ad8b7", 0xF30A5B03U));
  return cases;
}

std::vector<GreyscaleRgb64Case> greyscale_rgb64_cases() {
  const int limited = ColorRange_Compat_e::AVS_COLORRANGE_LIMITED;
  const int full = ColorRange_Compat_e::AVS_COLORRANGE_FULL;
  const int rec601 = Matrix_e::AVS_MATRIX_BT470_BG;
  const int rec709 = Matrix_e::AVS_MATRIX_BT709;
  std::vector<GreyscaleRgb64Case> cases;
  for (const auto& item : {std::tuple{"Rec601", "Limited", rec601, limited},
                           std::tuple{"Rec709", "Full", rec709, full}}) {
    cases.push_back(make_greyscale_rgb64_case(
        std::get<0>(item), std::get<1>(item), std::get<2>(item), std::get<3>(item), 8, 5, 64,
        Variant<GreyscaleRgb64FuncPtr>{"sse41", greyscale_rgb64_sse41, IsaRequirement::Sse41},
        std::get<2>(item) == rec601 ? "8e86848b4bcc9fb5" : "5a58fd5f9ff4b1ac"));
  }
  cases.push_back(make_greyscale_rgb64_case(
      "Rec601", "Limited", rec601, limited, 10, 7, 80,
      Variant<GreyscaleRgb64FuncPtr>{"sse41", greyscale_rgb64_sse41, IsaRequirement::Sse41},
      "efb79d6177cc1897", 0xF30A5B04U));
  cases.push_back(make_greyscale_rgb64_case(
      "Rec709", "Full", rec709, full, 10, 7, 80,
      Variant<GreyscaleRgb64FuncPtr>{"sse41", greyscale_rgb64_sse41, IsaRequirement::Sse41},
      "8c059caf49835393", 0xF30A5B05U));
  return cases;
}

class GreyscaleYuy2Kernels : public ::testing::TestWithParam<GreyscaleYuy2Case> {};

TEST_P(GreyscaleYuy2Kernels, SetsChromaToNeutral) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_greyscale_yuy2_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, GreyscaleYuy2Kernels, ::testing::ValuesIn(greyscale_yuy2_cases()),
                         [](const ::testing::TestParamInfo<GreyscaleYuy2Case>& info) {
                           return info.param.name;
                         });

class GreyscaleRgb32Kernels : public ::testing::TestWithParam<GreyscaleRgb32Case> {};

TEST_P(GreyscaleRgb32Kernels, AppliesMatrixAndPreservesAlpha) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_greyscale_rgb_case<std::uint8_t>(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, GreyscaleRgb32Kernels,
                         ::testing::ValuesIn(greyscale_rgb32_cases()),
                         [](const ::testing::TestParamInfo<GreyscaleRgb32Case>& info) {
                           return info.param.name;
                         });

class GreyscaleRgb64Kernels : public ::testing::TestWithParam<GreyscaleRgb64Case> {};

TEST_P(GreyscaleRgb64Kernels, AppliesMatrixAndPreservesAlpha) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_greyscale_rgb_case<std::uint16_t>(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, GreyscaleRgb64Kernels,
                         ::testing::ValuesIn(greyscale_rgb64_cases()),
                         [](const ::testing::TestParamInfo<GreyscaleRgb64Case>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
