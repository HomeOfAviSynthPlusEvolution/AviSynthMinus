#include <gtest/gtest.h>

#include "convert_bits_dither_test_helpers.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

std::vector<ConvertBitsDitherCase> convert_bits_dither_cases() {
  std::vector<ConvertBitsDitherCase> cases;
  add_convert_bits_dither_variants<std::uint16_t, std::uint8_t, false, true, true>(
      cases, IntegerStorage::UInt16ToUInt8, 10, 8, 8, 5, 96, 64, "e9c92365d00e8684");
  add_convert_bits_dither_variants<std::uint16_t, std::uint8_t, false, false, false>(
      cases, IntegerStorage::UInt16ToUInt8, 12, 8, 8, 7, 96, 64, "844942ec985238bb");
  add_convert_bits_dither_variants<std::uint16_t, std::uint16_t, true, true, false>(
      cases, IntegerStorage::UInt16ToUInt16, 14, 9, 9, 9, 96, 96, "2682b6ecfd8f9f59");
  add_convert_bits_dither_variants<std::uint16_t, std::uint16_t, false, true, true>(
      cases, IntegerStorage::UInt16ToUInt16, 16, 10, 8, 17, 96, 96, "521e1814024d78c5");
  add_convert_bits_dither_variants<std::uint8_t, std::uint8_t, false, true, true>(
      cases, IntegerStorage::UInt8ToUInt8, 8, 8, 5, 5, 64, 64, "b40e731855403cd1");
  return cases;
}

class ConvertBitsDitherKernels : public ::testing::TestWithParam<ConvertBitsDitherCase> {};

TEST_P(ConvertBitsDitherKernels, MatchesIndependentBayerReferenceAcrossInstructionSets) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_convert_bits_dither_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, ConvertBitsDitherKernels,
                         ::testing::ValuesIn(convert_bits_dither_cases()),
                         [](const ::testing::TestParamInfo<ConvertBitsDitherCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
