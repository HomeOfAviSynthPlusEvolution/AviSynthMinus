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

} // namespace

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

} // namespace avsut::test
