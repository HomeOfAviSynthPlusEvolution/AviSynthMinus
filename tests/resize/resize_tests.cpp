#include <gtest/gtest.h>

#include "resize_test_helpers.h"

#include "support/cpu_features.h"

#include <vector>

namespace avsut::test {
namespace {

std::vector<VerticalReduceCase> vertical_reduce_cases() {
  return {
      make_vertical_reduce_case(
          16, 4, 2, 32, 48,
          Variant<VerticalReduceFunction>{"sse2", vertical_reduce_sse2, IsaRequirement::Sse2},
          "a39538200823a02f"),
      make_vertical_reduce_case(
          48, 10, 5, 64, 64,
          Variant<VerticalReduceFunction>{"sse2", vertical_reduce_sse2, IsaRequirement::Sse2},
          "b85700f92c22f06d"),
  };
}

std::vector<ResizeVertical8Case> resize_vertical8_cases() {
  return {
      make_resize_vertical8_case(
          32, 7, 5, 64, 64,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar, IsaRequirement::Sse2},
          "6f9964479c013e1b"),
      make_resize_vertical8_case(
          32, 7, 5, 64, 64,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint8_t, IsaRequirement::Avx2},
          "6f9964479c013e1b"),
      make_resize_vertical8_case(
          64, 9, 6, 96, 128,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar, IsaRequirement::Sse2},
          "70ed9a0fd3bcf668",
          0xF30D0801U),
      make_resize_vertical8_case(
          64, 9, 6, 96, 128,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint8_t, IsaRequirement::Avx2},
          "70ed9a0fd3bcf668",
          0xF30D0801U),
      make_resize_vertical8_case(
          64, 9, 6, 96, 128,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar, IsaRequirement::Sse2},
          "36467e23ecf5f50c", 0xF30D0808U, ResizeFilter::Lanczos3),
      make_resize_vertical8_case(
          64, 9, 6, 96, 128,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint8_t, IsaRequirement::Avx2},
          "36467e23ecf5f50c", 0xF30D0808U, ResizeFilter::Lanczos3),
      make_resize_vertical8_case(
          64, 9, 6, 96, 128,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar, IsaRequirement::Sse2},
          "f78c70f9dab9cd8d", 0xF30D0806U, ResizeFilter::Lanczos6),
      make_resize_vertical8_case(
          64, 9, 6, 96, 128,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint8_t, IsaRequirement::Avx2},
          "f78c70f9dab9cd8d", 0xF30D0806U, ResizeFilter::Lanczos6),
      make_resize_vertical8_case(
          64, 9, 6, 96, 128,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar, IsaRequirement::Sse2},
          "369fa4270dfe3662", 0xF30D0807U, ResizeFilter::Lanczos10),
      make_resize_vertical8_case(
          64, 9, 6, 96, 128,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint8_t, IsaRequirement::Avx2},
          "369fa4270dfe3662", 0xF30D0807U, ResizeFilter::Lanczos10),
  };
}

std::vector<ResizeVertical16Case> resize_vertical16_cases() {
  return {
      make_resize_vertical16_case(
          10, 32, 7, 5, 128, 128,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar_uint16_t<true>,
                                  IsaRequirement::Sse2},
          "f7e598ccec48b4d1"),
      make_resize_vertical16_case(
          10, 32, 7, 5, 128, 128,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint16_t<true>,
                                  IsaRequirement::Avx2},
          "f7e598ccec48b4d1"),
      make_resize_vertical16_case(
          16, 32, 7, 5, 128, 128,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar_uint16_t<false>,
                                  IsaRequirement::Sse2},
          "b5418b1d30a8c90c"),
      make_resize_vertical16_case(
          16, 32, 7, 5, 128, 128,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint16_t<false>,
                                  IsaRequirement::Avx2},
          "b5418b1d30a8c90c"),
      make_resize_vertical16_case(
          10, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar_uint16_t<true>,
                                  IsaRequirement::Sse2},
          "3059ea0bddf8b584", 0xF30D1001U),
      make_resize_vertical16_case(
          10, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint16_t<true>,
                                  IsaRequirement::Avx2},
          "3059ea0bddf8b584", 0xF30D1001U),
      make_resize_vertical16_case(
          16, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar_uint16_t<false>,
                                  IsaRequirement::Sse2},
          "b9040768582f8c51", 0xF30D1601U, ResizeFilter::Lanczos3),
      make_resize_vertical16_case(
          16, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint16_t<false>,
                                  IsaRequirement::Avx2},
          "b9040768582f8c51", 0xF30D1601U, ResizeFilter::Lanczos3),
      make_resize_vertical16_case(
          16, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar_uint16_t<false>,
                                  IsaRequirement::Sse2},
          "59d2be6e7ccc641e", 0xF30D1602U, ResizeFilter::Lanczos6),
      make_resize_vertical16_case(
          16, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint16_t<false>,
                                  IsaRequirement::Avx2},
          "59d2be6e7ccc641e", 0xF30D1602U, ResizeFilter::Lanczos6),
      make_resize_vertical16_case(
          16, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar_uint16_t<false>,
                                  IsaRequirement::Sse2},
          "58d9269dcc704493", 0xF30D1603U, ResizeFilter::Lanczos10),
      make_resize_vertical16_case(
          16, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint16_t<false>,
                                  IsaRequirement::Avx2},
          "58d9269dcc704493", 0xF30D1603U, ResizeFilter::Lanczos10),
      make_resize_vertical16_case(
          10, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar_uint16_t<true>,
                                  IsaRequirement::Sse2},
          "2a02d0a1c77306e2", 0xF30D1006U, ResizeFilter::Lanczos6),
      make_resize_vertical16_case(
          10, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint16_t<true>,
                                  IsaRequirement::Avx2},
          "2a02d0a1c77306e2", 0xF30D1006U, ResizeFilter::Lanczos6),
      make_resize_vertical16_case(
          10, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar_uint16_t<true>,
                                  IsaRequirement::Sse2},
          "79f98f384c2ad039", 0xF30D1007U, ResizeFilter::Lanczos10),
      make_resize_vertical16_case(
          10, 48, 9, 6, 128, 192,
          Variant<ResizeFunction>{"avx2", resize_v_avx2_planar_uint16_t<true>,
                                  IsaRequirement::Avx2},
          "79f98f384c2ad039", 0xF30D1007U, ResizeFilter::Lanczos10),
  };
}

std::vector<ResizeHorizontal8Case> resize_horizontal8_cases() {
  return {
      make_resize_horizontal8_case(
          48, 32, 5, 64, 64,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint8_t, true>,
                                  IsaRequirement::Ssse3},
          "540645551755f4d6"),
      make_resize_horizontal8_case(
          48, 32, 5, 64, 64,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint8_t, IsaRequirement::Avx2},
          "540645551755f4d6"),
      make_resize_horizontal8_case(
          37, 24, 4, 96, 64,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint8_t, true>,
                                  IsaRequirement::Ssse3},
          "5a5ffa34ace2e2d4", std::nullopt, ResizeFilter::Triangle, 0xF30D0802U),
      make_resize_horizontal8_case(
          37, 24, 4, 96, 64,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint8_t, IsaRequirement::Avx2},
          "5a5ffa34ace2e2d4", std::nullopt, ResizeFilter::Triangle, 0xF30D0802U),
      make_resize_horizontal8_case(
          128, 192, 5, 192, 256,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint8_t, true>,
                                  IsaRequirement::Ssse3},
          "6641e00ace5dd4ab", std::nullopt, ResizeFilter::Lanczos3, 0xF30D0803U),
      make_resize_horizontal8_case(
          128, 192, 5, 192, 256,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint8_t, IsaRequirement::Avx2},
          "6641e00ace5dd4ab", std::nullopt, ResizeFilter::Lanczos3, 0xF30D0803U),
      make_resize_horizontal8_case(
          128, 192, 5, 192, 256,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint8_t, true>,
                                  IsaRequirement::Ssse3},
          "1acc3e7c4b57fc3f", std::nullopt, ResizeFilter::Lanczos6, 0xF30D0804U),
      make_resize_horizontal8_case(
          128, 192, 5, 192, 256,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint8_t, IsaRequirement::Avx2},
          "1acc3e7c4b57fc3f", std::nullopt, ResizeFilter::Lanczos6, 0xF30D0804U),
      make_resize_horizontal8_case(
          128, 192, 5, 192, 256,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint8_t, true>,
                                  IsaRequirement::Ssse3},
          "eb628a614b99a905", std::nullopt, ResizeFilter::Lanczos10, 0xF30D0805U),
      make_resize_horizontal8_case(
          128, 192, 5, 192, 256,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint8_t, IsaRequirement::Avx2},
          "eb628a614b99a905", std::nullopt, ResizeFilter::Lanczos10, 0xF30D0805U),
  };
}

std::vector<ResizeHorizontal16Case> resize_horizontal16_cases() {
  return {
      make_resize_horizontal16_case(
          10, 48, 32, 5, 128, 128,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint16_t, true>,
                                  IsaRequirement::Ssse3},
          "020442318cd5e6ee"),
      make_resize_horizontal16_case(
          10, 48, 32, 5, 128, 128,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint16_t<true>,
                                  IsaRequirement::Avx2},
          "020442318cd5e6ee"),
      make_resize_horizontal16_case(
          16, 48, 32, 5, 128, 128,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint16_t, false>,
                                  IsaRequirement::Ssse3},
          "fa53387644097a8d"),
      make_resize_horizontal16_case(
          16, 48, 32, 5, 128, 128,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint16_t<false>,
                                  IsaRequirement::Avx2},
          "fa53387644097a8d"),
      make_resize_horizontal16_case(
          16, 37, 24, 4, 128, 96,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint16_t, false>,
                                  IsaRequirement::Ssse3},
          "1872945cb8110531", std::nullopt, ResizeFilter::Triangle, 0xF30D1002U),
      make_resize_horizontal16_case(
          16, 37, 24, 4, 128, 96,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint16_t<false>,
                                  IsaRequirement::Avx2},
          "1872945cb8110531", std::nullopt, ResizeFilter::Triangle, 0xF30D1002U),
      make_resize_horizontal16_case(
          10, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint16_t, true>,
                                  IsaRequirement::Ssse3},
          "fb5784303564bab7", std::nullopt, ResizeFilter::Lanczos3, 0xF30D1003U),
      make_resize_horizontal16_case(
          10, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint16_t<true>,
                                  IsaRequirement::Avx2},
          "fb5784303564bab7", std::nullopt, ResizeFilter::Lanczos3, 0xF30D1003U),
      make_resize_horizontal16_case(
          10, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint16_t, true>,
                                  IsaRequirement::Ssse3},
          "9b017bead6b0c5a6", std::nullopt, ResizeFilter::Lanczos6, 0xF30D1004U),
      make_resize_horizontal16_case(
          10, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint16_t<true>,
                                  IsaRequirement::Avx2},
          "9b017bead6b0c5a6", std::nullopt, ResizeFilter::Lanczos6, 0xF30D1004U),
      make_resize_horizontal16_case(
          10, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint16_t, true>,
                                  IsaRequirement::Ssse3},
          "2b62a27318fd2468", std::nullopt, ResizeFilter::Lanczos10, 0xF30D1005U),
      make_resize_horizontal16_case(
          10, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint16_t<true>,
                                  IsaRequirement::Avx2},
          "2b62a27318fd2468", std::nullopt, ResizeFilter::Lanczos10, 0xF30D1005U),
      make_resize_horizontal16_case(
          16, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint16_t, false>,
                                  IsaRequirement::Ssse3},
          "558ab4cffc7d7641", std::nullopt, ResizeFilter::Lanczos3, 0xF30D1604U),
      make_resize_horizontal16_case(
          16, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint16_t<false>,
                                  IsaRequirement::Avx2},
          "558ab4cffc7d7641", std::nullopt, ResizeFilter::Lanczos3, 0xF30D1604U),
      make_resize_horizontal16_case(
          16, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint16_t, false>,
                                  IsaRequirement::Ssse3},
          "980cefd5577b0c24", std::nullopt, ResizeFilter::Lanczos6, 0xF30D1605U),
      make_resize_horizontal16_case(
          16, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint16_t<false>,
                                  IsaRequirement::Avx2},
          "980cefd5577b0c24", std::nullopt, ResizeFilter::Lanczos6, 0xF30D1605U),
      make_resize_horizontal16_case(
          16, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_uint8_16<std::uint16_t, false>,
                                  IsaRequirement::Ssse3},
          "2ceaf446499cb464", std::nullopt, ResizeFilter::Lanczos10, 0xF30D1606U),
      make_resize_horizontal16_case(
          16, 128, 192, 5, 384, 512,
          Variant<ResizeFunction>{"avx2", resizer_h_avx2_generic_uint16_t<false>,
                                  IsaRequirement::Avx2},
          "2ceaf446499cb464", std::nullopt, ResizeFilter::Lanczos10, 0xF30D1606U),
  };
}

std::vector<ResizeVerticalFloatCase> resize_vertical_float_cases() {
  return {
      make_resize_vertical_float_case(
          40, 7, 5, 192, 192,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar_float, IsaRequirement::Sse2}),
      make_resize_vertical_float_case(
          40, 7, 5, 192, 192,
          Variant<ResizeFunction>{"avx2_fma", resize_v_avx2_planar_float,
                                  IsaRequirement::Avx2Fma}),
      make_resize_vertical_float_case(
          37, 9, 6, 192, 192,
          Variant<ResizeFunction>{"sse2", resize_v_sse2_planar_float, IsaRequirement::Sse2},
          0xF30D3701U),
  };
}

std::vector<ResizeHorizontalFloatCase> resize_horizontal_float_cases() {
  return {
      make_resize_horizontal_float_case(
          48, 32, 5, 256, 128,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_float, IsaRequirement::Ssse3}),
      make_resize_horizontal_float_case(
          48, 32, 5, 256, 128,
          Variant<ResizeFunction>{"avx2_fma", resizer_h_avx2_generic_float,
                                  IsaRequirement::Avx2Fma}),
      make_resize_horizontal_float_case(
          37, 23, 4, 192, 128,
          Variant<ResizeFunction>{"ssse3", resizer_h_ssse3_generic_float, IsaRequirement::Ssse3},
          ResizeFilter::Triangle, 0, false, 0xF30D2302U),
      make_resize_horizontal_float_case(
          37, 23, 4, 192, 192,
          Variant<ResizeFunction>{"avx2_fma", resizer_h_avx2_generic_float,
                                  IsaRequirement::Avx2Fma},
          ResizeFilter::Triangle, 0, false, 0xF30D2302U, 64),
  };
}

class ResizeVertical8Kernels : public ::testing::TestWithParam<ResizeVertical8Case> {};

TEST_P(ResizeVertical8Kernels, MatchesFixedCoefficientReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_resize_vertical8_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, ResizeVertical8Kernels,
                         ::testing::ValuesIn(resize_vertical8_cases()),
                         [](const ::testing::TestParamInfo<ResizeVertical8Case>& info) {
                           return info.param.name;
                         });

class VerticalReduceKernels : public ::testing::TestWithParam<VerticalReduceCase> {};

TEST_P(VerticalReduceKernels, MatchesThreeTapReductionReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_vertical_reduce_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, VerticalReduceKernels,
                         ::testing::ValuesIn(vertical_reduce_cases()),
                         [](const ::testing::TestParamInfo<VerticalReduceCase>& info) {
                           return info.param.name;
                         });

class ResizeVertical16Kernels : public ::testing::TestWithParam<ResizeVertical16Case> {};

TEST_P(ResizeVertical16Kernels, MatchesFixedCoefficientReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_resize_vertical16_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, ResizeVertical16Kernels,
                         ::testing::ValuesIn(resize_vertical16_cases()),
                         [](const ::testing::TestParamInfo<ResizeVertical16Case>& info) {
                           return info.param.name;
                         });

class ResizeHorizontal8Kernels : public ::testing::TestWithParam<ResizeHorizontal8Case> {};

TEST_P(ResizeHorizontal8Kernels, MatchesFixedCoefficientReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_resize_horizontal8_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, ResizeHorizontal8Kernels,
                         ::testing::ValuesIn(resize_horizontal8_cases()),
                         [](const ::testing::TestParamInfo<ResizeHorizontal8Case>& info) {
                           return info.param.name;
                         });

class ResizeHorizontal16Kernels : public ::testing::TestWithParam<ResizeHorizontal16Case> {};

TEST_P(ResizeHorizontal16Kernels, MatchesFixedCoefficientReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_resize_horizontal16_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, ResizeHorizontal16Kernels,
                         ::testing::ValuesIn(resize_horizontal16_cases()),
                         [](const ::testing::TestParamInfo<ResizeHorizontal16Case>& info) {
                           return info.param.name;
                         });

class ResizeVerticalFloatKernels : public ::testing::TestWithParam<ResizeVerticalFloatCase> {};

TEST_P(ResizeVerticalFloatKernels, MatchesFixedCoefficientReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_resize_vertical_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, ResizeVerticalFloatKernels,
                         ::testing::ValuesIn(resize_vertical_float_cases()),
                         [](const ::testing::TestParamInfo<ResizeVerticalFloatCase>& info) {
                           return info.param.name;
                         });

class ResizeHorizontalFloatKernels : public ::testing::TestWithParam<ResizeHorizontalFloatCase> {};

TEST_P(ResizeHorizontalFloatKernels, MatchesFixedCoefficientReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_resize_horizontal_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, ResizeHorizontalFloatKernels,
                         ::testing::ValuesIn(resize_horizontal_float_cases()),
                         [](const ::testing::TestParamInfo<ResizeHorizontalFloatCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
