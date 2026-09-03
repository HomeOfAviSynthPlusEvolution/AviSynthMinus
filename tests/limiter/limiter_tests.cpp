#include <gtest/gtest.h>

#include "limiter_test_helpers.h"

#include "support/cpu_features.h"

#include <vector>

namespace avsut::test {
namespace {

std::vector<Limiter8Case> limiter8_cases() {
  return {
      make_limiter8_case(37, 5, 48, 16, 235, limit_plane_sse2,
                         Variant<Limiter8FuncPtr>{"sse2", limit_plane_sse2, IsaRequirement::Sse2},
                         "bafa5e1c8ef8ad16")};
}

std::vector<Limiter16Case> limiter16_cases() {
  return {
      make_limiter16_case(
          23, 5, 64, 64, 60000, limit_plane_uint16_sse2,
          Variant<Limiter16FuncPtr>{"sse2", limit_plane_uint16_sse2, IsaRequirement::Sse2},
          "8c0d91bd6aab6687"),
      make_limiter16_case(
          23, 5, 64, 64, 60000, limit_plane_uint16_sse4,
          Variant<Limiter16FuncPtr>{"sse4.1", limit_plane_uint16_sse4, IsaRequirement::Sse41},
          "8c0d91bd6aab6687"),
  };
}

class Limiter8Kernels : public ::testing::TestWithParam<Limiter8Case> {};

TEST_P(Limiter8Kernels, ClampsBoundaryValuesInPlace) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_limiter8_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, Limiter8Kernels, ::testing::ValuesIn(limiter8_cases()),
                         [](const ::testing::TestParamInfo<Limiter8Case>& info) {
                           return info.param.name;
                         });

class Limiter16Kernels : public ::testing::TestWithParam<Limiter16Case> {};

TEST_P(Limiter16Kernels, ClampsBoundaryValuesInPlace) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_limiter16_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, Limiter16Kernels, ::testing::ValuesIn(limiter16_cases()),
                         [](const ::testing::TestParamInfo<Limiter16Case>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
