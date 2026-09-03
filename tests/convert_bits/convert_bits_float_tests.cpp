#include <gtest/gtest.h>

#include "convert_bits_float_test_helpers.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

std::vector<ConvertBitsFloatCase> convert_bits_float_cases() {
  std::vector<ConvertBitsFloatCase> cases;
  add_float_to_integer_variants<std::uint8_t, false, true, true>(
      cases, FloatConversion::FloatToUInt8, 8, "cf598ce5195e6d41");
  add_float_to_integer_variants<std::uint16_t, false, false, true>(
      cases, FloatConversion::FloatToUInt16, 10, "7594f94a03511f77");
  add_float_to_integer_variants<std::uint16_t, true, true, false>(
      cases, FloatConversion::FloatToUInt16, 10, "c186b634e598194e");
  add_float_to_integer_variants<std::uint16_t, true, false, true>(
      cases, FloatConversion::FloatToUInt16, 16, "8b3b6d4629122a90");
  cases.push_back(make_convert_bits_float_case(
      FloatConversion::FloatToUInt8, false, false, true, 32, 8, 29, 4, 192, 96,
      Variant<ConvertBitsIntegerFunc>{
          "sse41", convert_32_to_uintN_sse41<std::uint8_t, false, false, true>,
          IsaRequirement::Sse41},
      "a164d8b0bc78e986", 0xF30B1402U));
  cases.push_back(make_convert_bits_float_case(
      FloatConversion::FloatToUInt8, false, false, true, 32, 8, 29, 4, 192, 96,
      Variant<ConvertBitsIntegerFunc>{
          "avx2-fma", convert_32_to_uintN_avx2<std::uint8_t, false, false, true>,
          IsaRequirement::Avx2Fma},
      "a164d8b0bc78e986", 0xF30B1402U));
  cases.push_back(make_convert_bits_float_case(
      FloatConversion::FloatToUInt16, false, true, false, 32, 10, 29, 4, 192, 128,
      Variant<ConvertBitsIntegerFunc>{
          "sse41", convert_32_to_uintN_sse41<std::uint16_t, false, true, false>,
          IsaRequirement::Sse41},
      "42df97d9eae3ace0", 0xF30B1403U));
  cases.push_back(make_convert_bits_float_case(
      FloatConversion::FloatToUInt16, false, true, false, 32, 10, 29, 4, 192, 128,
      Variant<ConvertBitsIntegerFunc>{
          "avx2-fma", convert_32_to_uintN_avx2<std::uint16_t, false, true, false>,
          IsaRequirement::Avx2Fma},
      "42df97d9eae3ace0", 0xF30B1403U));
  return cases;
}

class ConvertBitsFloatKernels : public ::testing::TestWithParam<ConvertBitsFloatCase> {};

TEST_P(ConvertBitsFloatKernels, MatchesIndependentNumericalReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_convert_bits_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, ConvertBitsFloatKernels,
                         ::testing::ValuesIn(convert_bits_float_cases()),
                         [](const ::testing::TestParamInfo<ConvertBitsFloatCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
