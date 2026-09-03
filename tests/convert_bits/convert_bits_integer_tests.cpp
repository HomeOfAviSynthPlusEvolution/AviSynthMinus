#include <gtest/gtest.h>

#include "convert_bits_integer_test_helpers.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

std::vector<ConvertBitsIntegerCase> convert_bits_integer_cases() {
  std::vector<ConvertBitsIntegerCase> cases;
  add_convert_bits_integer_variants<std::uint8_t, std::uint16_t, false, false, false>(
      cases, IntegerStorage::UInt8ToUInt16, 8, 10, 64, 96, "3278b527745fd959");
  add_convert_bits_integer_variants<std::uint16_t, std::uint8_t, false, false, false>(
      cases, IntegerStorage::UInt16ToUInt8, 14, 8, 96, 64, "ff476d4b8d488cac");
  add_convert_bits_integer_variants<std::uint8_t, std::uint16_t, false, true, true>(
      cases, IntegerStorage::UInt8ToUInt16, 8, 16, 64, 96, "e42e639c7934d541");
  add_convert_bits_integer_variants<std::uint8_t, std::uint16_t, false, true, false>(
      cases, IntegerStorage::UInt8ToUInt16, 8, 10, 64, 96, "d823c68d9c27c31b");
  add_convert_bits_integer_variants<std::uint16_t, std::uint8_t, false, false, true>(
      cases, IntegerStorage::UInt16ToUInt8, 10, 8, 96, 64, "ed18711677eb3213");
  add_convert_bits_integer_variants<std::uint16_t, std::uint16_t, true, true, false>(
      cases, IntegerStorage::UInt16ToUInt16, 12, 10, 96, 96, "672fae69485b7457");
  add_convert_bits_integer_variants<std::uint16_t, std::uint16_t, true, false, true>(
      cases, IntegerStorage::UInt16ToUInt16, 10, 12, 96, 96, "1fc847aea34fe10a");
  add_convert_bits_integer_variants<std::uint8_t, std::uint8_t, true, true, false>(
      cases, IntegerStorage::UInt8ToUInt8, 8, 8, 64, 64, "59749a530dee16d9");
  add_convert_bits_integer_variants<std::uint8_t, std::uint16_t, false, true, false>(
      cases, IntegerStorage::UInt8ToUInt16, 8, 10, 96, 128, "3dd59beb0f68b56a", 0xF30F2405U);
  add_convert_bits_integer_variants<std::uint16_t, std::uint8_t, false, false, true>(
      cases, IntegerStorage::UInt16ToUInt8, 10, 8, 128, 96, "65ab473e92272d50", 0xF30F2406U);
  add_convert_bits_integer_variants<std::uint16_t, std::uint16_t, true, true, false>(
      cases, IntegerStorage::UInt16ToUInt16, 12, 10, 128, 128, "d43bec624eaf2b0d", 0xF30F2407U);
  add_convert_bits_integer_variants<std::uint16_t, std::uint8_t, false, true, false>(
      cases, IntegerStorage::UInt16ToUInt8, 14, 8, 96, 64, "e29f426768ae7380", 0xF30B2410U, 29, 7,
      32);
  return cases;
}

class ConvertBitsIntegerKernels : public ::testing::TestWithParam<ConvertBitsIntegerCase> {};

TEST_P(ConvertBitsIntegerKernels, MatchesIndependentRangeReferenceAcrossInstructionSets) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_convert_bits_integer_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, ConvertBitsIntegerKernels,
                         ::testing::ValuesIn(convert_bits_integer_cases()),
                         [](const ::testing::TestParamInfo<ConvertBitsIntegerCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
