#include <gtest/gtest.h>

#include "merge_float_test_helpers.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

void add_merge_float_case(std::vector<MergeFloatCase>& cases, FloatInputPattern input_pattern,
                          std::size_t width_pixels, std::size_t height_pixels,
                          std::size_t destination_pitch, std::size_t other_pitch, float weight,
                          const char* weight_label, std::uint32_t seed, MergeFloatFuncPtr scalar,
                          MergeFloatFuncPtr sse2) {
  cases.push_back(make_merge_float_case(
      input_pattern, width_pixels, height_pixels, destination_pitch, other_pitch, weight,
      weight_label, seed, scalar, Variant<MergeFloatFuncPtr>{"c", scalar, IsaRequirement::Scalar}));
  cases.push_back(make_merge_float_case(
      input_pattern, width_pixels, height_pixels, destination_pitch, other_pitch, weight,
      weight_label, seed, scalar, Variant<MergeFloatFuncPtr>{"sse2", sse2, IsaRequirement::Sse2}));

}

std::vector<MergeFloatCase> merge_float_cases() {
  std::vector<MergeFloatCase> cases;
  add_merge_float_case(cases, FloatInputPattern::RandomBounded, 13, 5, 64, 96, 0.37F, "37Pct",
                       0xF37A00C1U,
                       legacy_merge_float<LegacyMergeIsa::Scalar>,
                       legacy_merge_float<LegacyMergeIsa::Sse2>);
  add_merge_float_case(cases, FloatInputPattern::RandomBounded, 17, 3, 96, 96, 0.5F, "50Pct",
                       0xF50A00C2U,
                       legacy_merge_float<LegacyMergeIsa::Scalar>,
                       legacy_merge_float<LegacyMergeIsa::Sse2>);
  add_merge_float_case(cases, FloatInputPattern::RandomBounded, 29, 4, 128, 160, 0.19F,
                       "19Pct", 0xF30F00D1U,
                       legacy_merge_float<LegacyMergeIsa::Scalar>,
                       legacy_merge_float<LegacyMergeIsa::Sse2>);
  add_merge_float_case(cases, FloatInputPattern::MixedMagnitudeCancellation, 13, 7, 64, 96, 0.73F,
                       "73Pct", 0xF73A00C3U,
                       legacy_merge_float<LegacyMergeIsa::Scalar>,
                       legacy_merge_float<LegacyMergeIsa::Sse2>);
  return cases;
}

class MergeFloatKernels : public ::testing::TestWithParam<MergeFloatCase> {};

TEST_P(MergeFloatKernels, MatchesDoubleReferenceAndScalarWithinTolerance) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_merge_float_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, MergeFloatKernels, ::testing::ValuesIn(merge_float_cases()),
                         [](const ::testing::TestParamInfo<MergeFloatCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
