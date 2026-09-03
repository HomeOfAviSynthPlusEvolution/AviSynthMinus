#include <gtest/gtest.h>

#include "layer_genericplane_fast_test_helpers.h"

#include "support/cpu_features.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

template <typename T>
Variant<LayerGenericPlaneFastFunction> layer_genericplane_fast_sse_variant() {
  return {"sse2", layer_genericplane_fast_sse2<T>, IsaRequirement::Sse2};
}

template <typename T>
Variant<LayerGenericPlaneFastFunction> layer_genericplane_fast_avx_variant() {
  return {"avx2", layer_genericplane_fast_avx2<T>, IsaRequirement::Avx2};
}

std::vector<LayerGenericPlaneFastCase> layer_genericplane_fast_cases() {
  constexpr std::size_t u8_width = 35;
  constexpr std::size_t u16_width = 17;
  return {
      make_layer_genericplane_fast_case(8, u8_width, 3, 48, 64, 3, 9,
                                        layer_genericplane_fast_sse_variant<std::uint8_t>(),
                                        "272f3047773ff963"),
      make_layer_genericplane_fast_case(8, u8_width, 3, 48, 64, 3, 9,
                                        layer_genericplane_fast_avx_variant<std::uint8_t>(),
                                        "272f3047773ff963"),
      make_layer_genericplane_fast_case(16, u16_width, 3, 48, 64, 4, 10,
                                        layer_genericplane_fast_sse_variant<std::uint16_t>(),
                                        "ce7e7f6a92b2143a"),
      make_layer_genericplane_fast_case(16, u16_width, 3, 48, 64, 4, 10,
                                        layer_genericplane_fast_avx_variant<std::uint16_t>(),
                                        "ce7e7f6a92b2143a"),
      make_layer_genericplane_fast_case(
          8, 47, 7, 80, 96, 7, 13, layer_genericplane_fast_sse_variant<std::uint8_t>(),
          "7842f4686c885a10",
          0xF30F1A01U),
      make_layer_genericplane_fast_case(
          8, 47, 7, 80, 96, 7, 13, layer_genericplane_fast_avx_variant<std::uint8_t>(),
          "7842f4686c885a10",
          0xF30F1A01U),
      make_layer_genericplane_fast_case(
          16, 23, 7, 64, 80, 10, 18, layer_genericplane_fast_sse_variant<std::uint16_t>(),
          "bd61ffb166e6a46f",
          0xF30F1A02U),
      make_layer_genericplane_fast_case(
          16, 23, 7, 64, 80, 10, 18, layer_genericplane_fast_avx_variant<std::uint16_t>(),
          "bd61ffb166e6a46f",
          0xF30F1A02U),
  };
}

class LayerGenericPlaneFastKernels : public ::testing::TestWithParam<LayerGenericPlaneFastCase> {};

TEST_P(LayerGenericPlaneFastKernels, MatchesIndependentAverageReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bits_per_sample == 8) {
    run_layer_genericplane_fast_case<std::uint8_t>(test_case);
  } else {
    run_layer_genericplane_fast_case<std::uint16_t>(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(Kernels, LayerGenericPlaneFastKernels,
                         ::testing::ValuesIn(layer_genericplane_fast_cases()),
                         [](const ::testing::TestParamInfo<LayerGenericPlaneFastCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
