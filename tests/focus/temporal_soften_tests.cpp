#include <gtest/gtest.h>

#include "temporal_soften_test_helpers.h"

#include "support/cpu_features.h"

#include <vector>

namespace avsut::test {
namespace {

std::vector<TemporalSoften8Case> temporal_soften8_cases() {
  return {
      make_temporal_soften8_case(48, 64, 2, 12U,
                                 Variant<TemporalSoften8Function>{
                                     "sse2", accumulate_line_sse2<false>, IsaRequirement::Sse2},
                                 "ba587cc6b944c933"),
      make_temporal_soften8_case(48, 64, 2, 12U,
                                 Variant<TemporalSoften8Function>{
                                     "ssse3", accumulate_line_ssse3<false>, IsaRequirement::Ssse3},
                                 "ba587cc6b944c933"),
      make_temporal_soften8_case(48, 64, 3, 255U,
                                 Variant<TemporalSoften8Function>{
                                     "sse2", accumulate_line_sse2<true>, IsaRequirement::Sse2},
                                 "504f4bf90a8bbb1a"),
      make_temporal_soften8_case(48, 64, 3, 255U,
                                 Variant<TemporalSoften8Function>{
                                     "ssse3", accumulate_line_ssse3<true>, IsaRequirement::Ssse3},
                                 "504f4bf90a8bbb1a"),
  };
}

std::vector<TemporalSoften16Case> temporal_soften16_cases() {
  return {
      make_temporal_soften16_case(
          48, 128, 2, 12U, 10,
          Variant<TemporalSoften16Function>{"sse2", accumulate_line_16_sse2<false, true>,
                                            IsaRequirement::Sse2},
          "9ba32b12dab060ff"),
      make_temporal_soften16_case(
          48, 128, 2, 12U, 10,
          Variant<TemporalSoften16Function>{"sse41", accumulate_line_16_sse41<false, true>,
                                            IsaRequirement::Sse41},
          "9ba32b12dab060ff"),
      make_temporal_soften16_case(
          48, 128, 3, 255U, 10,
          Variant<TemporalSoften16Function>{"sse2", accumulate_line_16_sse2<true, true>,
                                            IsaRequirement::Sse2},
          "71c2d93a089f302b"),
      make_temporal_soften16_case(
          48, 128, 3, 255U, 10,
          Variant<TemporalSoften16Function>{"sse41", accumulate_line_16_sse41<true, true>,
                                            IsaRequirement::Sse41},
          "71c2d93a089f302b"),
      make_temporal_soften16_case(
          48, 128, 2, 12U, 16,
          Variant<TemporalSoften16Function>{"sse2", accumulate_line_16_sse2<false, false>,
                                            IsaRequirement::Sse2},
          "8fcdbf8e0d206de1"),
      make_temporal_soften16_case(
          48, 128, 2, 12U, 16,
          Variant<TemporalSoften16Function>{"sse41", accumulate_line_16_sse41<false, false>,
                                            IsaRequirement::Sse41},
          "8fcdbf8e0d206de1"),
      make_temporal_soften16_case(
          48, 128, 2, 255U, 16,
          Variant<TemporalSoften16Function>{"sse2", accumulate_line_16_sse2<true, false>,
                                            IsaRequirement::Sse2},
          "6ad6b30d924503fd"),
      make_temporal_soften16_case(
          48, 128, 2, 255U, 16,
          Variant<TemporalSoften16Function>{"sse41", accumulate_line_16_sse41<true, false>,
                                            IsaRequirement::Sse41},
          "6ad6b30d924503fd"),
  };
}

class TemporalSoften8Kernels : public ::testing::TestWithParam<TemporalSoften8Case> {};

TEST_P(TemporalSoften8Kernels, MatchesIndependentThresholdedAverage) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_temporal_soften8_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, TemporalSoften8Kernels,
                         ::testing::ValuesIn(temporal_soften8_cases()),
                         [](const ::testing::TestParamInfo<TemporalSoften8Case>& info) {
                           return info.param.name;
                         });

class TemporalSoften16Kernels : public ::testing::TestWithParam<TemporalSoften16Case> {};

TEST_P(TemporalSoften16Kernels, MatchesIndependentThresholdedAverage) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_temporal_soften16_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, TemporalSoften16Kernels,
                         ::testing::ValuesIn(temporal_soften16_cases()),
                         [](const ::testing::TestParamInfo<TemporalSoften16Case>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
