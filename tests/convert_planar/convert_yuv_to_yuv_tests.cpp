#include <gtest/gtest.h>

#include "convert_yuv_to_yuv_test_helpers.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace avsut::test {
namespace {

void add_yuv_to_yuv_variants(std::vector<YuvToYuvCase>& cases, int source_matrix,
                             int destination_matrix, bool source_full, bool destination_full,
                             int bit_depth, std::size_t width) {
  constexpr std::size_t height = 3;
  constexpr std::size_t pitch = 256;
  cases.push_back(make_yuv_to_yuv_case(
      source_matrix, destination_matrix, source_full, destination_full, bit_depth, width, height,
      pitch, pitch, Variant<YuvToYuvVariant>{"c", YuvToYuvVariant::C, IsaRequirement::Scalar}));
  cases.push_back(make_yuv_to_yuv_case(
      source_matrix, destination_matrix, source_full, destination_full, bit_depth, width, height,
      pitch, pitch, Variant<YuvToYuvVariant>{"sse2", YuvToYuvVariant::Sse2, IsaRequirement::Sse2}));
  cases.push_back(make_yuv_to_yuv_case(
      source_matrix, destination_matrix, source_full, destination_full, bit_depth, width, height,
      pitch, pitch, Variant<YuvToYuvVariant>{"avx2", YuvToYuvVariant::Avx2, IsaRequirement::Avx2}));
}

std::vector<YuvToYuvCase> yuv_to_yuv_cases() {
  std::vector<YuvToYuvCase> cases;
  add_yuv_to_yuv_variants(cases, AVS_MATRIX_BT470_BG, AVS_MATRIX_BT709, false, true, 8, 17);
  add_yuv_to_yuv_variants(cases, AVS_MATRIX_BT470_BG, AVS_MATRIX_BT709, false, true, 16, 17);
  add_yuv_to_yuv_variants(cases, AVS_MATRIX_BT470_BG, AVS_MATRIX_BT2020_NCL, true, false, 16, 17);
  add_yuv_to_yuv_variants(cases, AVS_MATRIX_BT470_BG, AVS_MATRIX_BT2020_NCL, false, true, 32, 9);
  return cases;
}

class YuvToYuvKernels : public ::testing::TestWithParam<YuvToYuvCase> {};

template <typename T>
void expect_tail_memory(const YuvToYuvCase& test_case, const GuardedVideoBuffer<T>& buffer,
                        const char* plane_name) {
  EXPECT_TRUE(buffer.guards_intact()) << test_case.name << " " << plane_name << " guards";
  if (test_case.variant.function == YuvToYuvVariant::C) {
    EXPECT_TRUE(buffer.padding_intact()) << test_case.name << " " << plane_name << " padding";
  } else {
    const auto allowed_end = yuv_to_yuv_allowed_output_end(test_case, sizeof(T));
    EXPECT_TRUE(buffer.padding_intact_from(allowed_end))
        << test_case.name << " " << plane_name << " padding after allowed " << allowed_end;
  }
}

template <typename T, bool LessThan16Bit>
void run_yuv_to_yuv_integer_case(const YuvToYuvCase& test_case) {
  GuardedVideoBuffer<T> source_y(test_case.width, test_case.height, test_case.source_pitch);
  GuardedVideoBuffer<T> source_u(test_case.width, test_case.height, test_case.source_pitch);
  GuardedVideoBuffer<T> source_v(test_case.width, test_case.height, test_case.source_pitch);
  GuardedVideoBuffer<T> expected_y(test_case.width, test_case.height, test_case.destination_pitch);
  GuardedVideoBuffer<T> expected_u(test_case.width, test_case.height, test_case.destination_pitch);
  GuardedVideoBuffer<T> expected_v(test_case.width, test_case.height, test_case.destination_pitch);
  GuardedVideoBuffer<T> actual_y(test_case.width, test_case.height, test_case.destination_pitch);
  GuardedVideoBuffer<T> actual_u(test_case.width, test_case.height, test_case.destination_pitch);
  GuardedVideoBuffer<T> actual_v(test_case.width, test_case.height, test_case.destination_pitch);
  fill_yuv_to_yuv_integer_inputs(source_y.view(), source_u.view(), source_v.view(),
                                 test_case.source_full, test_case.bit_depth);

  const auto source_y_before = source_y.snapshot_active();
  const auto source_u_before = source_u.snapshot_active();
  const auto source_v_before = source_v.snapshot_active();
  const auto fused = make_fused_yuv_matrix(
      test_case.source_matrix, test_case.destination_matrix, test_case.source_full,
      test_case.destination_full, test_case.bit_depth);

  for (std::size_t row = 0; row < test_case.height; ++row) {
    for (std::size_t column = 0; column < test_case.width; ++column) {
      const auto y = source_y.view().row(row)[column];
      const auto u = source_u.view().row(row)[column];
      const auto v = source_v.view().row(row)[column];
      expected_y.view().row(row)[column] =
          static_cast<T>(apply_yuv_to_yuv_integer(fused, 0, y, u, v, test_case.bit_depth));
      expected_u.view().row(row)[column] =
          static_cast<T>(apply_yuv_to_yuv_integer(fused, 1, y, u, v, test_case.bit_depth));
      expected_v.view().row(row)[column] =
          static_cast<T>(apply_yuv_to_yuv_integer(fused, 2, y, u, v, test_case.bit_depth));
    }
  }

  BYTE* destination[3]{reinterpret_cast<BYTE*>(actual_y.view().data()),
                       reinterpret_cast<BYTE*>(actual_u.view().data()),
                       reinterpret_cast<BYTE*>(actual_v.view().data())};
  int destination_pitch[3]{static_cast<int>(actual_y.view().pitch_bytes()),
                           static_cast<int>(actual_u.view().pitch_bytes()),
                           static_cast<int>(actual_v.view().pitch_bytes())};
  const BYTE* source[3]{reinterpret_cast<const BYTE*>(source_y.view().data()),
                        reinterpret_cast<const BYTE*>(source_u.view().data()),
                        reinterpret_cast<const BYTE*>(source_v.view().data())};
  const int source_pitch[3]{static_cast<int>(source_y.view().pitch_bytes()),
                            static_cast<int>(source_u.view().pitch_bytes()),
                            static_cast<int>(source_v.view().pitch_bytes())};
  call_yuv_to_yuv_kernel<T, LessThan16Bit>(test_case, destination, destination_pitch, source,
                                           source_pitch, fused.matrix);

  for (std::size_t row = 0; row < test_case.height; ++row) {
    for (std::size_t column = 0; column < test_case.width; ++column) {
      ASSERT_EQ(actual_y.view().row(row)[column], expected_y.view().row(row)[column])
          << test_case.name << " plane=Y row=" << row << " column=" << column;
      ASSERT_EQ(actual_u.view().row(row)[column], expected_u.view().row(row)[column])
          << test_case.name << " plane=U row=" << row << " column=" << column;
      ASSERT_EQ(actual_v.view().row(row)[column], expected_v.view().row(row)[column])
          << test_case.name << " plane=V row=" << row << " column=" << column;
    }
  }

  EXPECT_TRUE(source_y.active_matches(source_y_before));
  EXPECT_TRUE(source_u.active_matches(source_u_before));
  EXPECT_TRUE(source_v.active_matches(source_v_before));
  EXPECT_TRUE(source_y.memory_intact());
  EXPECT_TRUE(source_u.memory_intact());
  EXPECT_TRUE(source_v.memory_intact());
  EXPECT_TRUE(expected_y.memory_intact());
  EXPECT_TRUE(expected_u.memory_intact());
  EXPECT_TRUE(expected_v.memory_intact());
  expect_tail_memory(test_case, actual_y, "Y");
  expect_tail_memory(test_case, actual_u, "U");
  expect_tail_memory(test_case, actual_v, "V");
}

void run_yuv_to_yuv_float_case(const YuvToYuvCase& test_case) {
  GuardedVideoBuffer<float> source_y(test_case.width, test_case.height, test_case.source_pitch);
  GuardedVideoBuffer<float> source_u(test_case.width, test_case.height, test_case.source_pitch);
  GuardedVideoBuffer<float> source_v(test_case.width, test_case.height, test_case.source_pitch);
  GuardedVideoBuffer<float> expected_y(test_case.width, test_case.height,
                                        test_case.destination_pitch);
  GuardedVideoBuffer<float> expected_u(test_case.width, test_case.height,
                                       test_case.destination_pitch);
  GuardedVideoBuffer<float> expected_v(test_case.width, test_case.height,
                                       test_case.destination_pitch);
  GuardedVideoBuffer<float> actual_y(test_case.width, test_case.height, test_case.destination_pitch);
  GuardedVideoBuffer<float> actual_u(test_case.width, test_case.height, test_case.destination_pitch);
  GuardedVideoBuffer<float> actual_v(test_case.width, test_case.height, test_case.destination_pitch);
  fill_yuv_to_yuv_float_inputs(source_y.view(), source_u.view(), source_v.view(),
                               test_case.source_full);

  const auto source_y_before = source_y.snapshot_active();
  const auto source_u_before = source_u.snapshot_active();
  const auto source_v_before = source_v.snapshot_active();
  const auto fused = make_fused_yuv_matrix(
      test_case.source_matrix, test_case.destination_matrix, test_case.source_full,
      test_case.destination_full, test_case.bit_depth);
  for (std::size_t row = 0; row < test_case.height; ++row) {
    for (std::size_t column = 0; column < test_case.width; ++column) {
      const auto y = source_y.view().row(row)[column];
      const auto u = source_u.view().row(row)[column];
      const auto v = source_v.view().row(row)[column];
      expected_y.view().row(row)[column] = apply_yuv_to_yuv_float(fused, 0, y, u, v);
      expected_u.view().row(row)[column] = apply_yuv_to_yuv_float(fused, 1, y, u, v);
      expected_v.view().row(row)[column] = apply_yuv_to_yuv_float(fused, 2, y, u, v);
    }
  }

  BYTE* destination[3]{reinterpret_cast<BYTE*>(actual_y.view().data()),
                       reinterpret_cast<BYTE*>(actual_u.view().data()),
                       reinterpret_cast<BYTE*>(actual_v.view().data())};
  int destination_pitch[3]{static_cast<int>(actual_y.view().pitch_bytes()),
                           static_cast<int>(actual_u.view().pitch_bytes()),
                           static_cast<int>(actual_v.view().pitch_bytes())};
  const BYTE* source[3]{reinterpret_cast<const BYTE*>(source_y.view().data()),
                        reinterpret_cast<const BYTE*>(source_u.view().data()),
                        reinterpret_cast<const BYTE*>(source_v.view().data())};
  const int source_pitch[3]{static_cast<int>(source_y.view().pitch_bytes()),
                            static_cast<int>(source_u.view().pitch_bytes()),
                            static_cast<int>(source_v.view().pitch_bytes())};
  call_yuv_to_yuv_kernel<float, false>(test_case, destination, destination_pitch, source,
                                       source_pitch, fused.matrix);

  constexpr float tolerance = 4.0e-6F;
  for (std::size_t row = 0; row < test_case.height; ++row) {
    for (std::size_t column = 0; column < test_case.width; ++column) {
      EXPECT_NEAR(actual_y.view().row(row)[column], expected_y.view().row(row)[column], tolerance)
          << test_case.name << " plane=Y row=" << row << " column=" << column;
      EXPECT_NEAR(actual_u.view().row(row)[column], expected_u.view().row(row)[column], tolerance)
          << test_case.name << " plane=U row=" << row << " column=" << column;
      EXPECT_NEAR(actual_v.view().row(row)[column], expected_v.view().row(row)[column], tolerance)
          << test_case.name << " plane=V row=" << row << " column=" << column;
    }
  }

  EXPECT_TRUE(source_y.active_matches(source_y_before));
  EXPECT_TRUE(source_u.active_matches(source_u_before));
  EXPECT_TRUE(source_v.active_matches(source_v_before));
  EXPECT_TRUE(source_y.memory_intact());
  EXPECT_TRUE(source_u.memory_intact());
  EXPECT_TRUE(source_v.memory_intact());
  EXPECT_TRUE(expected_y.memory_intact());
  EXPECT_TRUE(expected_u.memory_intact());
  EXPECT_TRUE(expected_v.memory_intact());
  expect_tail_memory(test_case, actual_y, "Y");
  expect_tail_memory(test_case, actual_u, "U");
  expect_tail_memory(test_case, actual_v, "V");
}

TEST_P(YuvToYuvKernels, MatchesIndependentFusedMatrixReference) {
  const auto& test_case = GetParam();
  if (!variant_supported(test_case.variant, CpuFeatures::detect())) {
    GTEST_SKIP() << "host does not support " << test_case.variant.name;
  }
  if (test_case.bit_depth == 8) {
    run_yuv_to_yuv_integer_case<std::uint8_t, true>(test_case);
  } else if (test_case.bit_depth == 16) {
    run_yuv_to_yuv_integer_case<std::uint16_t, false>(test_case);
  } else {
    run_yuv_to_yuv_float_case(test_case);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Kernels,
    YuvToYuvKernels,
    ::testing::ValuesIn(yuv_to_yuv_cases()),
    [](const ::testing::TestParamInfo<YuvToYuvCase>& info) { return info.param.name; });

TEST(FusedYuvMatrix, UsesChromaDiagonalsAndIndependentRangeOffsets) {
  const auto limited_to_full = make_fused_yuv_matrix(
      AVS_MATRIX_BT470_BG, AVS_MATRIX_BT709, false, true, 8);
  const auto full_to_limited = make_fused_yuv_matrix(
      AVS_MATRIX_BT470_BG, AVS_MATRIX_BT2020_NCL, true, false, 16);

  EXPECT_EQ(limited_to_full.matrix.offset_in, -16);
  EXPECT_EQ(limited_to_full.matrix.offset_out, 0);
  EXPECT_EQ(full_to_limited.matrix.offset_in, 0);
  EXPECT_EQ(full_to_limited.matrix.offset_out, 4096);
  EXPECT_EQ(limited_to_full.matrix.u_b,
            yuv_to_yuv_round_symmetric((1 << 14) * limited_to_full.coefficients[1][1]));
  EXPECT_EQ(limited_to_full.matrix.v_r,
            yuv_to_yuv_round_symmetric((1 << 14) * limited_to_full.coefficients[2][2]));
  EXPECT_NE(limited_to_full.matrix.u_b, limited_to_full.matrix.y_b);
  EXPECT_NE(limited_to_full.matrix.v_r, limited_to_full.matrix.y_r);

  for (const int y : {16, 64, 128, 235}) {
    const auto u = apply_yuv_to_yuv_integer(limited_to_full, 1, static_cast<std::uint16_t>(y),
                                            128, 128, 8);
    const auto v = apply_yuv_to_yuv_integer(limited_to_full, 2, static_cast<std::uint16_t>(y),
                                            128, 128, 8);
    EXPECT_EQ(u, 128U) << "neutral gray Y=" << y;
    EXPECT_EQ(v, 128U) << "neutral gray Y=" << y;
  }
}

}  // namespace
}  // namespace avsut::test
