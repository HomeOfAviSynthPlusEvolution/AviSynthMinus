#include "convert/convert_helper.h"
#include "convert/convert_matrix.h"

#ifndef AVS_UNUSED
#define AVSUT_MATRIX_DEFINED_AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#endif
#include "convert/convert_planar.h"
#ifdef AVSUT_MATRIX_DEFINED_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_MATRIX_DEFINED_AVS_UNUSED
#endif

#include "support/guarded_video_buffer.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace avsut::test {
namespace {

struct KrKbCase {
  const char* name;
  int matrix;
  double kr;
  double kb;
};

void PrintTo(const KrKbCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

constexpr std::array<KrKbCase, 8> kr_kb_cases{{
    {"Bt470Bg", AVS_MATRIX_BT470_BG, 0.299, 0.114},
    {"St170M", AVS_MATRIX_ST170_M, 0.299, 0.114},
    {"Bt709", AVS_MATRIX_BT709, 0.2126, 0.0722},
    {"Bt2020Ncl", AVS_MATRIX_BT2020_NCL, 0.2627, 0.0593},
    {"Bt2020Cl", AVS_MATRIX_BT2020_CL, 0.2627, 0.0593},
    {"Bt470M", AVS_MATRIX_BT470_M, 0.3, 0.11},
    {"St240M", AVS_MATRIX_ST240_M, 0.212, 0.087},
    {"Average", AVS_MATRIX_AVERAGE, 1.0 / 3.0, 1.0 / 3.0},
}};

class KrKbTable : public ::testing::TestWithParam<KrKbCase> {};

TEST_P(KrKbTable, ReturnsIndependentMatrixTableEntry) {
  const auto& test_case = GetParam();
  double kr = -1.0;
  double kb = -1.0;

  ASSERT_TRUE(GetKrKb(test_case.matrix, kr, kb)) << test_case.name;
  EXPECT_DOUBLE_EQ(kr, test_case.kr) << test_case.name;
  EXPECT_DOUBLE_EQ(kb, test_case.kb) << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(
    MatrixIds,
    KrKbTable,
    ::testing::ValuesIn(kr_kb_cases),
    [](const ::testing::TestParamInfo<KrKbCase>& info) { return info.param.name; });

TEST(MatrixIds, RejectsIdentityAndUnsupportedMatrixIds) {
  constexpr std::array<int, 8> unsupported{
      AVS_MATRIX_RGB,
      AVS_MATRIX_UNSPECIFIED,
      AVS_MATRIX_YCGCO,
      AVS_MATRIX_ICTCP,
      AVS_MATRIX_CHROMATICITY_DERIVED_NCL,
      AVS_MATRIX_CHROMATICITY_DERIVED_CL,
      -1,
      12345,
  };

  for (const int matrix : unsupported) {
    double kr = 0.0;
    double kb = 0.0;
    EXPECT_FALSE(GetKrKb(matrix, kr, kb)) << "matrix=" << matrix;
  }
}

enum class MatrixDirection { RgbToYuv, YuvToRgb };

const char* direction_name(MatrixDirection direction) {
  return direction == MatrixDirection::RgbToYuv ? "RgbToYuv" : "YuvToRgb";
}

struct MatrixBuildCase {
  std::string name;
  int matrix;
  MatrixDirection direction;
  bool source_full;
  bool destination_full;
  int bit_depth;
};

void PrintTo(const MatrixBuildCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

std::vector<MatrixBuildCase> matrix_build_cases() {
  std::vector<MatrixBuildCase> cases;
  constexpr std::array<bool, 2> ranges{false, true};
  for (const auto& matrix_case : kr_kb_cases) {
    for (const auto direction : {MatrixDirection::RgbToYuv, MatrixDirection::YuvToRgb}) {
      for (const bool source_full : ranges) {
        for (const bool destination_full : ranges) {
          cases.push_back(MatrixBuildCase{
              std::string(matrix_case.name) + "_" + direction_name(direction) + "_Src" +
                  (source_full ? "Full" : "Limited") + "_Dst" +
                  (destination_full ? "Full" : "Limited") + "_Bit8",
              matrix_case.matrix,
              direction,
              source_full,
              destination_full,
              8});
        }
      }
    }
  }
  for (const auto direction : {MatrixDirection::RgbToYuv, MatrixDirection::YuvToRgb}) {
    cases.push_back(MatrixBuildCase{
        std::string("RgbIdentity_") + direction_name(direction) + "_SrcFull_DstFull_Bit8",
        AVS_MATRIX_RGB,
        direction,
        true,
        true,
        8});
  }
  return cases;
}

struct RangeValues {
  double offset;
  double span;
};

RangeValues luma_range(bool full, int bit_depth) {
  const double scale = static_cast<double>(std::uint32_t{1} << (bit_depth - 8));
  return full ? RangeValues{0.0, static_cast<double>((std::uint32_t{1} << bit_depth) - 1U)}
              : RangeValues{16.0 * scale, 219.0 * scale};
}

RangeValues chroma_range(bool full, int bit_depth) {
  const double scale = static_cast<double>(std::uint32_t{1} << (bit_depth - 8));
  return full ? RangeValues{static_cast<double>(std::uint32_t{1} << (bit_depth - 1)),
                            static_cast<double>((std::uint32_t{1} << bit_depth) - 1U) / 2.0}
              : RangeValues{static_cast<double>(std::uint32_t{1} << (bit_depth - 1)),
                            112.0 * scale};
}

RangeValues float_luma_range(bool full) {
  return full ? RangeValues{0.0, 1.0} : RangeValues{16.0 / 255.0, 219.0 / 255.0};
}

int round_symmetric(double value) {
  return static_cast<int>(value >= 0.0 ? value + 0.5 : value - 0.5);
}

bool matrix_kr_kb(int matrix, double& kr, double& kb) {
  if (matrix == AVS_MATRIX_RGB) {
    kr = 0.0;
    kb = 0.0;
    return true;
  }
  for (const auto& test_case : kr_kb_cases) {
    if (matrix == test_case.matrix) {
      kr = test_case.kr;
      kb = test_case.kb;
      return true;
    }
  }
  return false;
}

struct ExpectedMatrix {
  int y_r{};
  int y_g{};
  int y_b{};
  int u_r{};
  int u_g{};
  int u_b{};
  int v_r{};
  int v_g{};
  int v_b{};
  float y_r_f{};
  float y_g_f{};
  float y_b_f{};
  float u_r_f{};
  float u_g_f{};
  float u_b_f{};
  float v_r_f{};
  float v_g_f{};
  float v_b_f{};
  int offset_y{};
  float offset_y_f{};
  int offset_rgb{};
  float offset_rgb_f{};
  int offset_in{};
  float offset_in_f{};
  int offset_out{};
  float offset_out_f{};
  float target_span_f{};
  float target_span_f_32{};
  float offset_out_f_32{};
};

ExpectedMatrix expected_matrix(const MatrixBuildCase& test_case) {
  ExpectedMatrix expected{};
  double kr = 0.0;
  double kb = 0.0;
  EXPECT_TRUE(matrix_kr_kb(test_case.matrix, kr, kb));
  const double kg = 1.0 - kr - kb;
  const auto source_luma = luma_range(test_case.source_full, test_case.bit_depth);
  const auto destination_luma = luma_range(test_case.destination_full, test_case.bit_depth);
  const auto source_chroma = chroma_range(test_case.source_full, test_case.bit_depth);
  const auto destination_chroma = chroma_range(test_case.destination_full, test_case.bit_depth);
  const auto destination_luma_32 = float_luma_range(test_case.destination_full);
  const int shift = test_case.direction == MatrixDirection::RgbToYuv ? 15 : 13;
  const double scale = static_cast<double>(1 << shift);

  if (test_case.direction == MatrixDirection::RgbToYuv) {
    const double y_b = destination_luma.span * kb / source_luma.span;
    const double y_g = destination_luma.span * kg / source_luma.span;
    const double y_r = destination_luma.span * kr / source_luma.span;
    const double u_b = destination_chroma.span / source_luma.span;
    const double u_g = destination_chroma.span * kg / (kb - 1.0) / source_luma.span;
    const double u_r = destination_chroma.span * kr / (kb - 1.0) / source_luma.span;
    const double v_b = destination_chroma.span * kb / (kr - 1.0) / source_luma.span;
    const double v_g = destination_chroma.span * kg / (kr - 1.0) / source_luma.span;
    const double v_r = destination_chroma.span / source_luma.span;

    expected.y_b_f = static_cast<float>(y_b);
    expected.y_g_f = static_cast<float>(y_g);
    expected.y_r_f = static_cast<float>(y_r);
    expected.u_b_f = static_cast<float>(u_b);
    expected.u_g_f = static_cast<float>(u_g);
    expected.u_r_f = static_cast<float>(u_r);
    expected.v_b_f = static_cast<float>(v_b);
    expected.v_g_f = static_cast<float>(v_g);
    expected.v_r_f = static_cast<float>(v_r);
    expected.offset_y_f = static_cast<float>(destination_luma.offset);
    expected.offset_rgb_f = static_cast<float>(-source_luma.offset);

    expected.y_b = round_symmetric(scale * y_b);
    expected.y_g = round_symmetric(scale * y_g);
    expected.y_r = round_symmetric(scale * y_r);
    expected.u_b = round_symmetric(scale * u_b);
    expected.u_g = round_symmetric(scale * u_g);
    expected.u_r = round_symmetric(scale * u_r);
    expected.v_b = round_symmetric(scale * v_b);
    expected.v_g = round_symmetric(scale * v_g);
    expected.v_r = round_symmetric(scale * v_r);
    if (test_case.source_full && test_case.destination_full) {
      expected.y_g += static_cast<int>(scale) - expected.y_b - expected.y_g - expected.y_r;
    }
    expected.u_g -= expected.u_b + expected.u_g + expected.u_r;
    expected.v_g -= expected.v_b + expected.v_g + expected.v_r;
    expected.offset_y = static_cast<int>(destination_luma.offset);
    expected.offset_rgb = -static_cast<int>(source_luma.offset);
    expected.offset_in = expected.offset_rgb;
    expected.offset_in_f = expected.offset_rgb_f;
    expected.offset_out = expected.offset_y;
    expected.offset_out_f = expected.offset_y_f;
  } else {
    const double y = destination_luma.span / source_luma.span;
    const double u_b = destination_luma.span * (1.0 - kb) / source_chroma.span;
    const double u_g = destination_luma.span * (kb - 1.0) * kb / kg / source_chroma.span;
    const double u_r = 0.0;
    const double v_b = 0.0;
    const double v_g = destination_luma.span * (kr - 1.0) * kr / kg / source_chroma.span;
    const double v_r = destination_luma.span * (1.0 - kr) / source_chroma.span;

    expected.y_b_f = static_cast<float>(y);
    expected.y_g_f = static_cast<float>(y);
    expected.y_r_f = static_cast<float>(y);
    expected.u_b_f = static_cast<float>(u_b);
    expected.u_g_f = static_cast<float>(u_g);
    expected.u_r_f = static_cast<float>(u_r);
    expected.v_b_f = static_cast<float>(v_b);
    expected.v_g_f = static_cast<float>(v_g);
    expected.v_r_f = static_cast<float>(v_r);
    expected.offset_y_f = static_cast<float>(-source_luma.offset);
    expected.offset_rgb_f = static_cast<float>(destination_luma.offset);

    expected.y_b = round_symmetric(scale * y);
    expected.y_g = expected.y_b;
    expected.y_r = expected.y_b;
    expected.u_b = round_symmetric(scale * u_b);
    expected.u_g = round_symmetric(scale * u_g);
    expected.u_r = round_symmetric(scale * u_r);
    expected.v_b = round_symmetric(scale * v_b);
    expected.v_g = round_symmetric(scale * v_g);
    expected.v_r = round_symmetric(scale * v_r);
    expected.offset_y = round_symmetric(-source_luma.offset);
    expected.offset_rgb = round_symmetric(destination_luma.offset);
    expected.offset_in = expected.offset_y;
    expected.offset_in_f = expected.offset_y_f;
    expected.offset_out = expected.offset_rgb;
    expected.offset_out_f = expected.offset_rgb_f;
    expected.target_span_f = static_cast<float>(destination_luma.span);
  }

  expected.target_span_f_32 = static_cast<float>(destination_luma_32.span);
  expected.offset_out_f_32 = static_cast<float>(destination_luma_32.offset);
  return expected;
}

void expect_matrix_matches(const ConversionMatrix& actual, const ExpectedMatrix& expected,
                           const std::string& name) {
  EXPECT_EQ(actual.y_r, expected.y_r) << name;
  EXPECT_EQ(actual.y_g, expected.y_g) << name;
  EXPECT_EQ(actual.y_b, expected.y_b) << name;
  EXPECT_EQ(actual.u_r, expected.u_r) << name;
  EXPECT_EQ(actual.u_g, expected.u_g) << name;
  EXPECT_EQ(actual.u_b, expected.u_b) << name;
  EXPECT_EQ(actual.v_r, expected.v_r) << name;
  EXPECT_EQ(actual.v_g, expected.v_g) << name;
  EXPECT_EQ(actual.v_b, expected.v_b) << name;
  EXPECT_NEAR(actual.y_r_f, expected.y_r_f, 1.0e-6F) << name;
  EXPECT_NEAR(actual.y_g_f, expected.y_g_f, 1.0e-6F) << name;
  EXPECT_NEAR(actual.y_b_f, expected.y_b_f, 1.0e-6F) << name;
  EXPECT_NEAR(actual.u_r_f, expected.u_r_f, 1.0e-6F) << name;
  EXPECT_NEAR(actual.u_g_f, expected.u_g_f, 1.0e-6F) << name;
  EXPECT_NEAR(actual.u_b_f, expected.u_b_f, 1.0e-6F) << name;
  EXPECT_NEAR(actual.v_r_f, expected.v_r_f, 1.0e-6F) << name;
  EXPECT_NEAR(actual.v_g_f, expected.v_g_f, 1.0e-6F) << name;
  EXPECT_NEAR(actual.v_b_f, expected.v_b_f, 1.0e-6F) << name;
  EXPECT_EQ(actual.offset_y, expected.offset_y) << name;
  EXPECT_NEAR(actual.offset_y_f, expected.offset_y_f, 1.0e-6F) << name;
  EXPECT_EQ(actual.offset_rgb, expected.offset_rgb) << name;
  EXPECT_NEAR(actual.offset_rgb_f, expected.offset_rgb_f, 1.0e-6F) << name;
  EXPECT_EQ(actual.offset_in, expected.offset_in) << name;
  EXPECT_NEAR(actual.offset_in_f, expected.offset_in_f, 1.0e-6F) << name;
  EXPECT_EQ(actual.offset_out, expected.offset_out) << name;
  EXPECT_NEAR(actual.offset_out_f, expected.offset_out_f, 1.0e-6F) << name;
  EXPECT_NEAR(actual.target_span_f, expected.target_span_f, 1.0e-6F) << name;
  EXPECT_NEAR(actual.target_span_f_32, expected.target_span_f_32, 1.0e-6F) << name;
  EXPECT_NEAR(actual.offset_out_f_32, expected.offset_out_f_32, 1.0e-6F) << name;
}

struct BitsConstantsCase {
  std::string name;
  bool chroma{};
  bool source_full{};
  bool destination_full{};
  int source_bit_depth{};
  int destination_bit_depth{};
};

void PrintTo(const BitsConstantsCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

float expected_bits_span(bool chroma, bool full, int bit_depth) {
  if (chroma) {
    if (bit_depth == 32) {
      return full ? 0.5F : 112.0F / 255.0F;
    }
    return full ? static_cast<float>((std::uint32_t{1} << bit_depth) - 1U) / 2.0F
                : static_cast<float>(112U << (bit_depth - 8));
  }
  if (bit_depth == 32) {
    return full ? 1.0F : 219.0F / 255.0F;
  }
  return full ? static_cast<float>((std::uint32_t{1} << bit_depth) - 1U)
              : static_cast<float>(219U << (bit_depth - 8));
}

float expected_bits_offset(bool chroma, bool full, int bit_depth) {
  if (chroma) {
    return bit_depth == 32 ? 0.0F : static_cast<float>(std::uint32_t{1} << (bit_depth - 1));
  }
  if (bit_depth == 32) {
    return full ? 0.0F : 16.0F / 255.0F;
  }
  return full ? 0.0F : static_cast<float>(16U << (bit_depth - 8));
}

std::vector<BitsConstantsCase> bits_constants_cases() {
  std::vector<BitsConstantsCase> cases;
  constexpr std::array<int, 5> integer_bit_depths{8, 10, 12, 14, 16};
  for (const bool chroma : {false, true}) {
    for (const int bit_depth : integer_bit_depths) {
      for (const bool source_full : {false, true}) {
        for (const bool destination_full : {false, true}) {
          BitsConstantsCase test_case;
          test_case.chroma = chroma;
          test_case.source_full = source_full;
          test_case.destination_full = destination_full;
          test_case.source_bit_depth = bit_depth;
          test_case.destination_bit_depth = bit_depth;
          test_case.name = std::string(chroma ? "Chroma" : "Luma") + "_Src" +
                           (source_full ? "Full" : "Limited") + "_Dst" +
                           (destination_full ? "Full" : "Limited") + "_BitDepth" +
                           std::to_string(bit_depth);
          cases.push_back(std::move(test_case));
        }
      }
    }
  }

  for (const bool chroma : {false, true}) {
    for (const bool source_full : {false, true}) {
      for (const bool destination_full : {false, true}) {
        for (const auto [source_bit_depth, destination_bit_depth] :
             {std::pair{32, 32}, std::pair{16, 32}, std::pair{32, 16}}) {
          BitsConstantsCase test_case;
          test_case.chroma = chroma;
          test_case.source_full = source_full;
          test_case.destination_full = destination_full;
          test_case.source_bit_depth = source_bit_depth;
          test_case.destination_bit_depth = destination_bit_depth;
          test_case.name = std::string(chroma ? "Chroma" : "Luma") + "_Src" +
                           (source_full ? "Full" : "Limited") + "_Dst" +
                           (destination_full ? "Full" : "Limited") + "_SrcBits" +
                           std::to_string(source_bit_depth) + "_DstBits" +
                           std::to_string(destination_bit_depth);
          cases.push_back(std::move(test_case));
        }
      }
    }
  }
  return cases;
}

class BitsConstants : public ::testing::TestWithParam<BitsConstantsCase> {};

TEST_P(BitsConstants, UsesRangeSpecificSpansOffsetsAndScale) {
  const auto& test_case = GetParam();
  bits_conv_constants actual;
  get_bits_conv_constants(actual, test_case.chroma, test_case.source_full,
                          test_case.destination_full, test_case.source_bit_depth,
                          test_case.destination_bit_depth);

  const float expected_source_span =
      expected_bits_span(test_case.chroma, test_case.source_full, test_case.source_bit_depth);
  const float expected_destination_span = expected_bits_span(
      test_case.chroma, test_case.destination_full, test_case.destination_bit_depth);
  const float expected_source_offset =
      expected_bits_offset(test_case.chroma, test_case.source_full, test_case.source_bit_depth);
  const float expected_destination_offset = expected_bits_offset(
      test_case.chroma, test_case.destination_full, test_case.destination_bit_depth);

  EXPECT_FLOAT_EQ(actual.src_span, expected_source_span) << test_case.name;
  EXPECT_FLOAT_EQ(actual.dst_span, expected_destination_span) << test_case.name;
  EXPECT_FLOAT_EQ(actual.src_offset, expected_source_offset) << test_case.name;
  EXPECT_FLOAT_EQ(actual.dst_offset, expected_destination_offset) << test_case.name;
  EXPECT_FLOAT_EQ(actual.mul_factor, expected_destination_span / expected_source_span)
      << test_case.name;
  EXPECT_EQ(actual.src_offset_i, static_cast<int>(expected_source_offset)) << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(
    RangeConstants,
    BitsConstants,
    ::testing::ValuesIn(bits_constants_cases()),
    [](const ::testing::TestParamInfo<BitsConstantsCase>& info) { return info.param.name; });

struct MatrixPrecisionCase {
  std::string name;
  MatrixDirection direction{};
  bool source_full{};
  bool destination_full{};
  int bit_depth{};
  int arithmetic_shift{};
};

void PrintTo(const MatrixPrecisionCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

std::vector<MatrixPrecisionCase> matrix_precision_cases() {
  std::vector<MatrixPrecisionCase> cases;
  constexpr std::array<int, 5> bit_depths{8, 10, 12, 14, 16};
  for (const auto direction : {MatrixDirection::RgbToYuv, MatrixDirection::YuvToRgb}) {
    for (const bool source_full : {false, true}) {
      for (const bool destination_full : {false, true}) {
        for (const int bit_depth : bit_depths) {
          const int arithmetic_shift =
              direction == MatrixDirection::RgbToYuv ? 15 : 13;
          MatrixPrecisionCase test_case;
          test_case.direction = direction;
          test_case.source_full = source_full;
          test_case.destination_full = destination_full;
          test_case.bit_depth = bit_depth;
          test_case.arithmetic_shift = arithmetic_shift;
          test_case.name = "Bt709_" + std::string(direction_name(direction)) + "_Src" +
                           (source_full ? "Full" : "Limited") + "_Dst" +
                           (destination_full ? "Full" : "Limited") + "_BitDepth" +
                           std::to_string(bit_depth) + "_Shift" +
                           std::to_string(arithmetic_shift);
          cases.push_back(std::move(test_case));
        }
      }
    }
  }
  return cases;
}

class MatrixPrecision : public ::testing::TestWithParam<MatrixPrecisionCase> {};

TEST_P(MatrixPrecision, AppliesDirectionScaleAndNeutralityCorrections) {
  const auto& test_case = GetParam();
  const int source_range = test_case.source_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED;
  const int destination_range =
      test_case.destination_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED;
  ConversionMatrix actual{};
  const bool built = test_case.direction == MatrixDirection::RgbToYuv
                         ? do_BuildMatrix_Rgb2Yuv(AVS_MATRIX_BT709, source_range,
                                                  destination_range, test_case.arithmetic_shift,
                                                  test_case.bit_depth, actual)
                         : do_BuildMatrix_Yuv2Rgb(AVS_MATRIX_BT709, source_range,
                                                  destination_range, test_case.arithmetic_shift,
                                                  test_case.bit_depth, actual);
  ASSERT_TRUE(built) << test_case.name;

  const MatrixBuildCase reference_case{
      test_case.name, AVS_MATRIX_BT709, test_case.direction, test_case.source_full,
      test_case.destination_full, test_case.bit_depth};
  expect_matrix_matches(actual, expected_matrix(reference_case), test_case.name);

  const int scale = 1 << test_case.arithmetic_shift;
  if (test_case.direction == MatrixDirection::RgbToYuv) {
    if (test_case.source_full && test_case.destination_full) {
      EXPECT_EQ(actual.y_b + actual.y_g + actual.y_r, scale) << test_case.name;
    }
    EXPECT_EQ(actual.u_b + actual.u_g + actual.u_r, 0) << test_case.name;
    EXPECT_EQ(actual.v_b + actual.v_g + actual.v_r, 0) << test_case.name;

    const int rounded_u_b = round_symmetric(scale * actual.u_b_f);
    const int rounded_u_g = round_symmetric(scale * actual.u_g_f);
    const int rounded_u_r = round_symmetric(scale * actual.u_r_f);
    EXPECT_EQ(actual.u_g, rounded_u_g - rounded_u_b - rounded_u_g - rounded_u_r)
        << test_case.name;
  } else {
    EXPECT_EQ(actual.y_b, actual.y_g) << test_case.name;
    EXPECT_EQ(actual.y_g, actual.y_r) << test_case.name;
    EXPECT_EQ(actual.u_r, 0) << test_case.name;
    EXPECT_EQ(actual.v_b, 0) << test_case.name;
    EXPECT_LT(actual.u_g, 0) << test_case.name;
    EXPECT_LT(actual.v_g, 0) << test_case.name;
    EXPECT_EQ(actual.u_g, round_symmetric(scale * actual.u_g_f)) << test_case.name;
    EXPECT_EQ(actual.v_g, round_symmetric(scale * actual.v_g_f)) << test_case.name;
  }
}

INSTANTIATE_TEST_SUITE_P(
    DirectionScales,
    MatrixPrecision,
    ::testing::ValuesIn(matrix_precision_cases()),
    [](const ::testing::TestParamInfo<MatrixPrecisionCase>& info) { return info.param.name; });

TEST(MatrixIdentity, RetainsSixteenBitRgbCoefficientBoundary) {
  ConversionMatrix actual{};
  ASSERT_TRUE(do_BuildMatrix_Rgb2Yuv(AVS_MATRIX_RGB, AVS_COLORRANGE_FULL, AVS_COLORRANGE_FULL,
                                     15, 16, actual));

  EXPECT_EQ(actual.y_b, 0);
  EXPECT_EQ(actual.y_g, 1 << 15);
  EXPECT_EQ(actual.y_r, 0);
  EXPECT_EQ(actual.u_b, 1 << 14);
  EXPECT_EQ(actual.u_g, -(1 << 14));
  EXPECT_EQ(actual.u_r, 0);
  EXPECT_EQ(actual.v_b, 0);
  EXPECT_EQ(actual.v_g, -(1 << 14));
  EXPECT_EQ(actual.v_r, 1 << 14);
}

TEST(PlanarMatrix, PreservesSixteenBitIdentityWithFourteenBitScale) {
  constexpr std::size_t width = 7;
  constexpr std::size_t height = 3;
  constexpr std::size_t pitch = 20;
  constexpr int scale = 1 << 14;

  GuardedVideoBuffer<std::uint16_t> source_y(width, height, pitch, 64, 2);
  GuardedVideoBuffer<std::uint16_t> source_u(width, height, pitch, 64, 2);
  GuardedVideoBuffer<std::uint16_t> source_v(width, height, pitch, 64, 2);
  GuardedVideoBuffer<std::uint16_t> output_y(width, height, pitch);
  GuardedVideoBuffer<std::uint16_t> output_u(width, height, pitch);
  GuardedVideoBuffer<std::uint16_t> output_v(width, height, pitch);

  for (std::size_t row = 0; row < height; ++row) {
    for (std::size_t column = 0; column < width; ++column) {
      source_y.view().row(row)[column] = static_cast<std::uint16_t>(row * 20000 + column * 997);
      source_u.view().row(row)[column] = static_cast<std::uint16_t>(65535 - row * 17000 - column * 431);
      source_v.view().row(row)[column] = static_cast<std::uint16_t>((row * 31000 + column * 1234) & 0xffffU);
    }
  }

  const auto source_y_before = source_y.snapshot_active();
  const auto source_u_before = source_u.snapshot_active();
  const auto source_v_before = source_v.snapshot_active();

  ConversionMatrix matrix{};
  matrix.y_g = scale;
  matrix.u_b = scale;
  matrix.v_r = scale;

  BYTE* destination[3]{reinterpret_cast<BYTE*>(output_y.view().data()),
                       reinterpret_cast<BYTE*>(output_u.view().data()),
                       reinterpret_cast<BYTE*>(output_v.view().data())};
  int destination_pitch[3]{static_cast<int>(output_y.view().pitch_bytes()),
                           static_cast<int>(output_u.view().pitch_bytes()),
                           static_cast<int>(output_v.view().pitch_bytes())};
  const BYTE* source[3]{reinterpret_cast<const BYTE*>(source_y.view().data()),
                        reinterpret_cast<const BYTE*>(source_u.view().data()),
                        reinterpret_cast<const BYTE*>(source_v.view().data())};
  const int source_pitch[3]{static_cast<int>(source_y.view().pitch_bytes()),
                            static_cast<int>(source_u.view().pitch_bytes()),
                            static_cast<int>(source_v.view().pitch_bytes())};

  convert_yuv_to_planarrgb_c<ConversionDirection::YUV_TO_YUV, std::uint16_t, false>(
      destination, destination_pitch, source, source_pitch, static_cast<int>(width),
      static_cast<int>(height), matrix, 16, 16, false);

  EXPECT_EQ(output_y.snapshot_active(), source_y_before);
  EXPECT_EQ(output_u.snapshot_active(), source_u_before);
  EXPECT_EQ(output_v.snapshot_active(), source_v_before);
  EXPECT_TRUE(source_y.active_matches(source_y_before));
  EXPECT_TRUE(source_u.active_matches(source_u_before));
  EXPECT_TRUE(source_v.active_matches(source_v_before));
  EXPECT_TRUE(source_y.memory_intact());
  EXPECT_TRUE(source_u.memory_intact());
  EXPECT_TRUE(source_v.memory_intact());
  EXPECT_TRUE(output_y.memory_intact());
  EXPECT_TRUE(output_u.memory_intact());
  EXPECT_TRUE(output_v.memory_intact());
}

double expected_rgb_to_yuv_luma(double kr, double kb, bool destination_full, double g,
                                double b, double r) {
  const double kg = 1.0 - kr - kb;
  const double destination_span = destination_full ? 1.0 : 219.0 / 255.0;
  const double destination_offset = destination_full ? 0.0 : 16.0 / 255.0;
  return destination_offset + destination_span * (kg * g + kb * b + kr * r);
}

double expected_rgb_to_yuv_u(double kr, double kb, bool destination_full, double g, double b,
                             double r) {
  const double kg = 1.0 - kr - kb;
  const double destination_span = destination_full ? 0.5 : 112.0 / 255.0;
  return destination_span * (b - (kr * r + kg * g) / (1.0 - kb));
}

double expected_rgb_to_yuv_v(double kr, double kb, bool destination_full, double g, double b,
                             double r) {
  const double kg = 1.0 - kr - kb;
  const double destination_span = destination_full ? 0.5 : 112.0 / 255.0;
  return destination_span * (r - (kg * g + kb * b) / (1.0 - kr));
}

TEST(PlanarMatrix, UsesSixteenBitRgbFloatFallbackWithoutTouchingSource) {
  constexpr std::size_t width = 5;
  constexpr std::size_t height = 2;
  constexpr std::size_t source_pitch = 16;
  constexpr std::size_t destination_pitch = 32;
  constexpr std::array<std::array<std::uint16_t, 3>, 10> samples{{
      {{0, 0, 0}},
      {{65535, 65535, 65535}},
      {{65535, 0, 0}},
      {{0, 65535, 0}},
      {{0, 0, 65535}},
      {{32768, 16384, 49152}},
      {{49152, 32768, 16384}},
      {{16384, 49152, 32768}},
      {{1000, 64000, 32000}},
      {{64000, 1000, 32000}},
  }};

  GuardedVideoBuffer<std::uint16_t> source_g(width, height, source_pitch, 64, 2);
  GuardedVideoBuffer<std::uint16_t> source_b(width, height, source_pitch, 64, 2);
  GuardedVideoBuffer<std::uint16_t> source_r(width, height, source_pitch, 64, 2);
  GuardedVideoBuffer<float> output_y_integer_workflow(width, height, destination_pitch);
  GuardedVideoBuffer<float> output_u_integer_workflow(width, height, destination_pitch);
  GuardedVideoBuffer<float> output_v_integer_workflow(width, height, destination_pitch);
  GuardedVideoBuffer<float> output_y_float_workflow(width, height, destination_pitch);
  GuardedVideoBuffer<float> output_u_float_workflow(width, height, destination_pitch);
  GuardedVideoBuffer<float> output_v_float_workflow(width, height, destination_pitch);

  for (std::size_t row = 0; row < height; ++row) {
    for (std::size_t column = 0; column < width; ++column) {
      const auto& sample = samples[row * width + column];
      source_g.view().row(row)[column] = sample[0];
      source_b.view().row(row)[column] = sample[1];
      source_r.view().row(row)[column] = sample[2];
    }
  }
  const auto source_g_before = source_g.snapshot_active();
  const auto source_b_before = source_b.snapshot_active();
  const auto source_r_before = source_r.snapshot_active();

  ConversionMatrix matrix{};
  ASSERT_TRUE(do_BuildMatrix_Rgb2Yuv(AVS_MATRIX_BT709, AVS_COLORRANGE_FULL,
                                     AVS_COLORRANGE_LIMITED, 15, 16, matrix));

  const auto run_conversion = [&](GuardedVideoBuffer<float>& output_y,
                                  GuardedVideoBuffer<float>& output_u,
                                  GuardedVideoBuffer<float>& output_v, bool force_float) {
    BYTE* destination[3]{reinterpret_cast<BYTE*>(output_y.view().data()),
                         reinterpret_cast<BYTE*>(output_u.view().data()),
                         reinterpret_cast<BYTE*>(output_v.view().data())};
    int destination_pitch_values[3]{static_cast<int>(output_y.view().pitch_bytes()),
                                    static_cast<int>(output_u.view().pitch_bytes()),
                                    static_cast<int>(output_v.view().pitch_bytes())};
    const BYTE* source[3]{reinterpret_cast<const BYTE*>(source_g.view().data()),
                          reinterpret_cast<const BYTE*>(source_b.view().data()),
                          reinterpret_cast<const BYTE*>(source_r.view().data())};
    const int source_pitch_values[3]{static_cast<int>(source_g.view().pitch_bytes()),
                                     static_cast<int>(source_b.view().pitch_bytes()),
                                     static_cast<int>(source_r.view().pitch_bytes())};
    convert_yuv_to_planarrgb_c<ConversionDirection::RGB_TO_YUV, std::uint16_t, false>(
        destination, destination_pitch_values, source, source_pitch_values,
        static_cast<int>(width), static_cast<int>(height), matrix, 16, 32, force_float);
  };

  run_conversion(output_y_integer_workflow, output_u_integer_workflow,
                 output_v_integer_workflow, false);
  run_conversion(output_y_float_workflow, output_u_float_workflow, output_v_float_workflow, true);

  constexpr double kr = 0.2126;
  constexpr double kb = 0.0722;
  for (std::size_t row = 0; row < height; ++row) {
    for (std::size_t column = 0; column < width; ++column) {
      const auto& sample = samples[row * width + column];
      const double g = static_cast<double>(sample[0]) / 65535.0;
      const double b = static_cast<double>(sample[1]) / 65535.0;
      const double r = static_cast<double>(sample[2]) / 65535.0;
      const std::array<float, 3> expected{
          static_cast<float>(expected_rgb_to_yuv_luma(kr, kb, false, g, b, r)),
          static_cast<float>(expected_rgb_to_yuv_u(kr, kb, false, g, b, r)),
          static_cast<float>(expected_rgb_to_yuv_v(kr, kb, false, g, b, r))};
      const std::array<const GuardedVideoBuffer<float>*, 3> integer_output{
          &output_y_integer_workflow, &output_u_integer_workflow, &output_v_integer_workflow};
      const std::array<const GuardedVideoBuffer<float>*, 3> float_output{
          &output_y_float_workflow, &output_u_float_workflow, &output_v_float_workflow};
      for (std::size_t plane = 0; plane < 3; ++plane) {
        EXPECT_NEAR(integer_output[plane]->view().row(row)[column], expected[plane], 3.0e-5F)
            << "workflow=integer_float_output plane=" << plane << " row=" << row
            << " column=" << column;
        EXPECT_NEAR(float_output[plane]->view().row(row)[column], expected[plane], 3.0e-5F)
            << "workflow=force_float plane=" << plane << " row=" << row
            << " column=" << column;
        EXPECT_NEAR(integer_output[plane]->view().row(row)[column],
                    float_output[plane]->view().row(row)[column], 3.0e-5F)
            << "workflow_difference plane=" << plane << " row=" << row << " column="
            << column;
      }
    }
  }

  EXPECT_TRUE(source_g.active_matches(source_g_before));
  EXPECT_TRUE(source_b.active_matches(source_b_before));
  EXPECT_TRUE(source_r.active_matches(source_r_before));
  EXPECT_TRUE(source_g.memory_intact());
  EXPECT_TRUE(source_b.memory_intact());
  EXPECT_TRUE(source_r.memory_intact());
  EXPECT_TRUE(output_y_integer_workflow.memory_intact());
  EXPECT_TRUE(output_u_integer_workflow.memory_intact());
  EXPECT_TRUE(output_v_integer_workflow.memory_intact());
  EXPECT_TRUE(output_y_float_workflow.memory_intact());
  EXPECT_TRUE(output_u_float_workflow.memory_intact());
  EXPECT_TRUE(output_v_float_workflow.memory_intact());
}

class MatrixBuilder : public ::testing::TestWithParam<MatrixBuildCase> {};

TEST_P(MatrixBuilder, AcceptsSupportedIdsAndInitializesMatrixFields) {
  const auto& test_case = GetParam();
  const int source_range = test_case.source_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED;
  const int destination_range =
      test_case.destination_full ? AVS_COLORRANGE_FULL : AVS_COLORRANGE_LIMITED;
  ConversionMatrix actual{};
  const bool built = test_case.direction == MatrixDirection::RgbToYuv
                         ? do_BuildMatrix_Rgb2Yuv(test_case.matrix, source_range, destination_range,
                                                  15, test_case.bit_depth, actual)
                         : do_BuildMatrix_Yuv2Rgb(test_case.matrix, source_range, destination_range,
                                                  13, test_case.bit_depth, actual);

  ASSERT_TRUE(built) << test_case.name;
  expect_matrix_matches(actual, expected_matrix(test_case), test_case.name);
}

INSTANTIATE_TEST_SUITE_P(
    Supported,
    MatrixBuilder,
    ::testing::ValuesIn(matrix_build_cases()),
    [](const ::testing::TestParamInfo<MatrixBuildCase>& info) { return info.param.name; });

TEST(MatrixBuilder, RejectsUnsupportedIdsAndInvalidRanges) {
  constexpr std::array<int, 6> unsupported{
      AVS_MATRIX_UNSPECIFIED,
      AVS_MATRIX_YCGCO,
      AVS_MATRIX_ICTCP,
      AVS_MATRIX_CHROMATICITY_DERIVED_NCL,
      AVS_MATRIX_CHROMATICITY_DERIVED_CL,
      12345,
  };
  constexpr std::array<MatrixDirection, 2> directions{MatrixDirection::RgbToYuv,
                                                       MatrixDirection::YuvToRgb};
  for (const auto direction : directions) {
    for (const int matrix : unsupported) {
      ConversionMatrix actual{};
      const bool built = direction == MatrixDirection::RgbToYuv
                             ? do_BuildMatrix_Rgb2Yuv(matrix, AVS_COLORRANGE_FULL,
                                                      AVS_COLORRANGE_LIMITED, 15, 8, actual)
                             : do_BuildMatrix_Yuv2Rgb(matrix, AVS_COLORRANGE_FULL,
                                                      AVS_COLORRANGE_LIMITED, 13, 8, actual);
      EXPECT_FALSE(built) << "direction=" << direction_name(direction) << " matrix=" << matrix;
    }

    for (const int invalid_source : {-1, 2, 99}) {
      ConversionMatrix actual{};
      const bool built = direction == MatrixDirection::RgbToYuv
                             ? do_BuildMatrix_Rgb2Yuv(AVS_MATRIX_BT709, invalid_source,
                                                      AVS_COLORRANGE_FULL, 15, 8, actual)
                             : do_BuildMatrix_Yuv2Rgb(AVS_MATRIX_BT709, invalid_source,
                                                      AVS_COLORRANGE_FULL, 13, 8, actual);
      EXPECT_FALSE(built) << "direction=" << direction_name(direction)
                          << " invalid_source=" << invalid_source;
    }

    for (const int invalid_destination : {-1, 2, 99}) {
      ConversionMatrix actual{};
      const bool built = direction == MatrixDirection::RgbToYuv
                             ? do_BuildMatrix_Rgb2Yuv(AVS_MATRIX_BT709, AVS_COLORRANGE_LIMITED,
                                                      invalid_destination, 15, 8, actual)
                             : do_BuildMatrix_Yuv2Rgb(AVS_MATRIX_BT709, AVS_COLORRANGE_LIMITED,
                                                      invalid_destination, 13, 8, actual);
      EXPECT_FALSE(built) << "direction=" << direction_name(direction)
                          << " invalid_destination=" << invalid_destination;
    }
  }
}

}  // namespace
}  // namespace avsut::test
