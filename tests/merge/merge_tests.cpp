#include <gtest/gtest.h>

#include "merge_test_helpers.h"

#include "filters/overlay/intel/blend_common_avx2.h"
#include "filters/overlay/intel/blend_common_sse.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

void add_merge_cases(std::vector<MergeCase>& cases, const char* format, int bits_per_pixel,
                     std::size_t width_pixels, std::size_t height_pixels,
                     std::size_t destination_pitch, std::size_t other_pitch, int weight,
                     std::uint32_t seed, const char* expected_hash, MergeFuncPtr scalar,
                     MergeFuncPtr sse2, MergeFuncPtr avx2) {
  cases.push_back(make_merge_case(
      format, bits_per_pixel, width_pixels, height_pixels, destination_pitch, other_pitch, weight,
      seed, scalar, Variant<MergeFuncPtr>{"c", scalar, IsaRequirement::Scalar}, expected_hash));
  cases.push_back(make_merge_case(
      format, bits_per_pixel, width_pixels, height_pixels, destination_pitch, other_pitch, weight,
      seed, scalar, Variant<MergeFuncPtr>{"sse2", sse2, IsaRequirement::Sse2}, expected_hash));
  cases.push_back(make_merge_case(
      format, bits_per_pixel, width_pixels, height_pixels, destination_pitch, other_pitch, weight,
      seed, scalar, Variant<MergeFuncPtr>{"avx2", avx2, IsaRequirement::Avx2}, expected_hash));
}

std::vector<MergeCase> merge_cases() {
  std::vector<MergeCase> cases;
  add_merge_cases(cases, "Plane", 8, 37, 13, 48, 56, 8192, 0x0800BEEFU, "0bfd45f11b093a11",
                  weighted_merge_c, weighted_merge_sse2, weighted_merge_avx2);
  add_merge_cases(cases, "Plane", 8, 29, 5, 64, 72, 23456, 0xF30A11CEU, "",
                  weighted_merge_c, weighted_merge_sse2, weighted_merge_avx2);
  add_merge_cases(cases, "Plane", 10, 19, 11, 48, 56, 12345, 0x0A00BEEFU, "e6589674b8fcdcc1",
                  weighted_merge_c, weighted_merge_sse2, weighted_merge_avx2);
  add_merge_cases(cases, "Plane", 12, 17, 9, 48, 64, 21845, 0x0C00BEEFU, "f3e059b967e7222c",
                  weighted_merge_c, weighted_merge_sse2, weighted_merge_avx2);
  add_merge_cases(cases, "Plane", 14, 23, 7, 64, 72, 24576, 0x0E00BEEFU, "52a1934f97a6ef3d",
                  weighted_merge_c, weighted_merge_sse2, weighted_merge_avx2);
  add_merge_cases(cases, "Plane", 16, 15, 13, 48, 64, 16384, 0x1000BEEFU, "2fa29a72cbd44451",
                  weighted_merge_c, weighted_merge_sse2, weighted_merge_avx2);
  return cases;
}

class MergeKernels : public ::testing::TestWithParam<MergeCase> {};

TEST_P(MergeKernels, MatchesReferenceAndScalar) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  run_merge_case(test_case);
}

INSTANTIATE_TEST_SUITE_P(Kernels, MergeKernels, ::testing::ValuesIn(merge_cases()),
                         [](const ::testing::TestParamInfo<MergeCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
