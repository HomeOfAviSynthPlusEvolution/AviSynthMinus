#pragma once

#include "convert_planar_matrix_test_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace avsut::test {

enum class YuvToYuvVariant { C, Sse2, Avx2 };

struct YuvToYuvCase {
  int source_matrix{};
  int destination_matrix{};
  bool source_full{};
  bool destination_full{};
  int bit_depth{};
  std::size_t width{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  Variant<YuvToYuvVariant> variant;
  std::string name;
};

inline void PrintTo(const YuvToYuvCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline const char* yuv_to_yuv_matrix_name(int matrix) {
  if (matrix == AVS_MATRIX_BT709) {
    return "Bt709";
  }
  if (matrix == AVS_MATRIX_BT2020_NCL) {
    return "Bt2020Ncl";
  }
  return "Bt601";
}

inline std::string yuv_to_yuv_variant_name(const std::string& name) {
  std::string result = "Variant";
  bool capitalize = true;
  for (const char character : name) {
    result.push_back(capitalize && character >= 'a' && character <= 'z'
                         ? static_cast<char>(character - ('a' - 'A'))
                         : character);
    capitalize = false;
  }
  return result;
}

inline YuvToYuvCase make_yuv_to_yuv_case(int source_matrix, int destination_matrix,
                                         bool source_full, bool destination_full, int bit_depth,
                                         std::size_t width, std::size_t height,
                                         std::size_t source_pitch, std::size_t destination_pitch,
                                         Variant<YuvToYuvVariant> variant) {
  YuvToYuvCase result{source_matrix, destination_matrix, source_full, destination_full, bit_depth,
                      width, height, source_pitch, destination_pitch, std::move(variant), {}};
  std::ostringstream name;
  name << yuv_to_yuv_matrix_name(source_matrix) << "To"
       << yuv_to_yuv_matrix_name(destination_matrix) << "_Src"
       << (source_full ? "Full" : "Limited") << "_Dst"
       << (destination_full ? "Full" : "Limited") << "_BitDepth" << bit_depth << "_Width"
       << width << "_Height" << height << "_SrcPitch" << source_pitch << "_DstPitch"
       << destination_pitch << "_PatternRangeAnchors_" << yuv_to_yuv_variant_name(result.variant.name);
  result.name = name.str();
  return result;
}

struct YuvRangeReference {
  double luma_offset{};
  double luma_span{};
  double chroma_span{};
  int luma_offset_i{};
  int luma_span_i{};
  int chroma_span_i{};
  int chroma_center_i{};
};

inline YuvRangeReference make_yuv_range_reference(bool full, int bit_depth) {
  if (bit_depth == 32) {
    return {full ? 0.0 : 16.0 / 255.0,
            full ? 1.0 : 219.0 / 255.0,
            full ? 0.5 : 112.0 / 255.0,
            0,
            0,
            0,
            0};
  }

  const int sample_scale = 1 << (bit_depth - 8);
  const int maximum = (1 << bit_depth) - 1;
  return {static_cast<double>(full ? 0 : 16 * sample_scale),
          static_cast<double>(full ? maximum : 219 * sample_scale),
          static_cast<double>(full ? maximum / 2.0 : 112 * sample_scale),
          full ? 0 : 16 * sample_scale,
          full ? maximum : 219 * sample_scale,
          full ? static_cast<int>((maximum) / 2.0) : 112 * sample_scale,
          1 << (bit_depth - 1)};
}

inline void yuv_to_yuv_kr_kb(int matrix, double& kr, double& kb) {
  if (matrix == AVS_MATRIX_BT709) {
    kr = 0.2126;
    kb = 0.0722;
  } else if (matrix == AVS_MATRIX_BT2020_NCL) {
    kr = 0.2627;
    kb = 0.0593;
  } else {
    kr = 0.299;
    kb = 0.114;
  }
}

inline int yuv_to_yuv_round_symmetric(double value) {
  return static_cast<int>(value >= 0.0 ? value + 0.5 : value - 0.5);
}

struct FusedYuvMatrix {
  std::array<std::array<double, 3>, 3> coefficients{};
  ConversionMatrix matrix{};
  YuvRangeReference source_range;
  YuvRangeReference destination_range;
  int shift{14};
};

inline std::array<double, 3> compose_source_rgb_row(double kr, double kb, int component,
                                                    const YuvRangeReference& source) {
  const double kg = 1.0 - kr - kb;
  const double y_scale = 1.0 / source.luma_span;
  const double uv_scale = 1.0 / source.chroma_span;
  if (component == 0) {
    return {y_scale, 0.0, (1.0 - kr) * uv_scale};
  }
  if (component == 1) {
    return {y_scale, -(1.0 - kb) * kb / kg * uv_scale,
            -(1.0 - kr) * kr / kg * uv_scale};
  }
  return {y_scale, (1.0 - kb) * uv_scale, 0.0};
}

inline std::array<double, 3> combine_rows(const std::array<double, 3>& first, double first_scale,
                                           const std::array<double, 3>& second,
                                           double second_scale,
                                           const std::array<double, 3>& third,
                                           double third_scale) {
  return {first[0] * first_scale + second[0] * second_scale + third[0] * third_scale,
          first[1] * first_scale + second[1] * second_scale + third[1] * third_scale,
          first[2] * first_scale + second[2] * second_scale + third[2] * third_scale};
}

inline FusedYuvMatrix make_fused_yuv_matrix(int source_matrix, int destination_matrix,
                                            bool source_full, bool destination_full,
                                            int bit_depth) {
  FusedYuvMatrix result;
  result.source_range = make_yuv_range_reference(source_full, bit_depth);
  result.destination_range = make_yuv_range_reference(destination_full, bit_depth);

  double source_kr{};
  double source_kb{};
  double destination_kr{};
  double destination_kb{};
  yuv_to_yuv_kr_kb(source_matrix, source_kr, source_kb);
  yuv_to_yuv_kr_kb(destination_matrix, destination_kr, destination_kb);
  const double destination_kg = 1.0 - destination_kr - destination_kb;
  const auto source_r = compose_source_rgb_row(source_kr, source_kb, 0, result.source_range);
  const auto source_g = compose_source_rgb_row(source_kr, source_kb, 1, result.source_range);
  const auto source_b = compose_source_rgb_row(source_kr, source_kb, 2, result.source_range);

  result.coefficients[0] = combine_rows(source_r, destination_kr, source_g, destination_kg,
                                        source_b, destination_kb);
  result.coefficients[1] = combine_rows(
      source_b, 1.0, combine_rows(source_r, destination_kr, source_g, destination_kg,
                                  {0.0, 0.0, 0.0}, 0.0),
      -1.0 / (1.0 - destination_kb), {0.0, 0.0, 0.0}, 0.0);
  result.coefficients[2] = combine_rows(
      source_r, 1.0, source_g, -destination_kg / (1.0 - destination_kr), source_b,
      -destination_kb / (1.0 - destination_kr));
  result.coefficients[1][0] *= result.destination_range.chroma_span;
  result.coefficients[1][1] *= result.destination_range.chroma_span;
  result.coefficients[1][2] *= result.destination_range.chroma_span;
  result.coefficients[2][0] *= result.destination_range.chroma_span;
  result.coefficients[2][1] *= result.destination_range.chroma_span;
  result.coefficients[2][2] *= result.destination_range.chroma_span;
  for (double& coefficient : result.coefficients[0]) {
    coefficient *= result.destination_range.luma_span;
  }

  const bool float_matrix = bit_depth == 32;
  result.matrix.offset_in_f = static_cast<float>(-result.source_range.luma_offset);
  result.matrix.offset_out_f = static_cast<float>(result.destination_range.luma_offset);
  result.matrix.target_span_f = static_cast<float>(result.destination_range.luma_span);
  result.matrix.target_span_f_32 = static_cast<float>(result.destination_range.luma_span);
  result.matrix.offset_out_f_32 = static_cast<float>(result.destination_range.luma_offset);
  result.matrix.offset_in = float_matrix ? 0 : -result.source_range.luma_offset_i;
  result.matrix.offset_out = float_matrix ? 0 : result.destination_range.luma_offset_i;

  const auto set_matrix_row = [&](int row, int& y, int& u, int& v, float& y_f, float& u_f,
                                  float& v_f) {
    y_f = static_cast<float>(result.coefficients[row][0]);
    u_f = static_cast<float>(result.coefficients[row][1]);
    v_f = static_cast<float>(result.coefficients[row][2]);
    y = yuv_to_yuv_round_symmetric((1 << result.shift) * result.coefficients[row][0]);
    u = yuv_to_yuv_round_symmetric((1 << result.shift) * result.coefficients[row][1]);
    v = yuv_to_yuv_round_symmetric((1 << result.shift) * result.coefficients[row][2]);
  };
  set_matrix_row(0, result.matrix.y_g, result.matrix.u_g, result.matrix.v_g,
                 result.matrix.y_g_f, result.matrix.u_g_f, result.matrix.v_g_f);
  set_matrix_row(1, result.matrix.y_b, result.matrix.u_b, result.matrix.v_b,
                 result.matrix.y_b_f, result.matrix.u_b_f, result.matrix.v_b_f);
  set_matrix_row(2, result.matrix.y_r, result.matrix.u_r, result.matrix.v_r,
                 result.matrix.y_r_f, result.matrix.u_r_f, result.matrix.v_r_f);
  return result;
}

template <typename T>
inline void fill_yuv_to_yuv_integer_inputs(PlaneView<T> y_plane, PlaneView<T> u_plane,
                                           PlaneView<T> v_plane, bool full, int bit_depth) {
  const auto range = make_yuv_range_reference(full, bit_depth);
  const std::uint32_t scale = std::uint32_t{1} << (bit_depth - 8);
  const std::uint32_t y_first = static_cast<std::uint32_t>(range.luma_offset_i);
  const std::uint32_t y_span = static_cast<std::uint32_t>(range.luma_span_i);
  const std::uint32_t y_last = y_first + y_span;
  const std::array<std::uint32_t, 8> y_anchors{
      y_first, y_first + scale, y_first + y_span / 4, y_first + y_span / 2,
      y_first + (3 * y_span) / 4, y_last, y_first + y_span / 3, y_first + (2 * y_span) / 3};
  const std::uint32_t maximum = (std::uint32_t{1} << bit_depth) - 1U;
  const std::uint32_t center = std::uint32_t{1} << (bit_depth - 1);
  const std::array<std::uint32_t, 8> chroma_anchors =
      full ? std::array<std::uint32_t, 8>{0U, 1U, center - 1U, center, center + 1U, maximum,
                                         center / 2U, center + center / 2U}
           : std::array<std::uint32_t, 8>{16U * scale, 32U * scale, 64U * scale, center,
                                          192U * scale, 224U * scale, 239U * scale, 240U * scale};
  for (std::size_t row = 0; row < y_plane.height(); ++row) {
    for (std::size_t column = 0; column < y_plane.width(); ++column) {
      y_plane.row(row)[column] = static_cast<T>(y_anchors[(column + 3 * row) % y_anchors.size()]);
      u_plane.row(row)[column] =
          static_cast<T>(chroma_anchors[(2 * column + 5 * row + 1) % chroma_anchors.size()]);
      v_plane.row(row)[column] =
          static_cast<T>(chroma_anchors[(3 * column + 7 * row + 2) % chroma_anchors.size()]);
    }
  }
}

inline void fill_yuv_to_yuv_float_inputs(PlaneView<float> y_plane, PlaneView<float> u_plane,
                                         PlaneView<float> v_plane, bool full) {
  const auto range = make_yuv_range_reference(full, 32);
  const std::array<float, 8> y_anchors{
      static_cast<float>(range.luma_offset),
      static_cast<float>(range.luma_offset + range.luma_span * 0.125),
      static_cast<float>(range.luma_offset + range.luma_span * 0.25),
      static_cast<float>(range.luma_offset + range.luma_span * 0.5),
      static_cast<float>(range.luma_offset + range.luma_span * 0.75),
      static_cast<float>(range.luma_offset + range.luma_span),
      static_cast<float>(range.luma_offset + range.luma_span / 3.0),
      static_cast<float>(range.luma_offset + range.luma_span * 2.0 / 3.0)};
  const std::array<float, 8> chroma_anchors{
      static_cast<float>(-range.chroma_span), static_cast<float>(-range.chroma_span * 0.5), 0.0F,
      static_cast<float>(range.chroma_span * 0.5), static_cast<float>(range.chroma_span),
      static_cast<float>(-range.chroma_span * 0.25), static_cast<float>(range.chroma_span * 0.25),
      0.0F};
  for (std::size_t row = 0; row < y_plane.height(); ++row) {
    for (std::size_t column = 0; column < y_plane.width(); ++column) {
      y_plane.row(row)[column] = y_anchors[(column + 3 * row) % y_anchors.size()];
      u_plane.row(row)[column] = chroma_anchors[(2 * column + 5 * row + 1) % chroma_anchors.size()];
      v_plane.row(row)[column] = chroma_anchors[(3 * column + 7 * row + 2) % chroma_anchors.size()];
    }
  }
}

inline std::uint16_t apply_yuv_to_yuv_integer(const FusedYuvMatrix& fused, int output_plane,
                                              std::uint16_t y, std::uint16_t u, std::uint16_t v,
                                              int bit_depth) {
  const std::int64_t input_y = static_cast<std::int64_t>(y) + fused.matrix.offset_in;
  const std::int64_t input_u = static_cast<std::int64_t>(u) - (std::int64_t{1} << (bit_depth - 1));
  const std::int64_t input_v = static_cast<std::int64_t>(v) - (std::int64_t{1} << (bit_depth - 1));
  const std::array<std::int64_t, 3> input{input_y, input_u, input_v};
  const int row = output_plane;
  std::int64_t total = static_cast<std::int64_t>(
      fused.matrix.y_g * input[0] + fused.matrix.u_g * input[1] + fused.matrix.v_g * input[2]);
  if (row == 1) {
    total = static_cast<std::int64_t>(fused.matrix.y_b) * input[0] +
            static_cast<std::int64_t>(fused.matrix.u_b) * input[1] +
            static_cast<std::int64_t>(fused.matrix.v_b) * input[2];
  } else if (row == 2) {
    total = static_cast<std::int64_t>(fused.matrix.y_r) * input[0] +
            static_cast<std::int64_t>(fused.matrix.u_r) * input[1] +
            static_cast<std::int64_t>(fused.matrix.v_r) * input[2];
  }
  const int offset = row == 0 ? fused.matrix.offset_out : (1 << (bit_depth - 1));
  total += (std::int64_t{1} << (fused.shift - 1)) + (static_cast<std::int64_t>(offset) << fused.shift);
  const std::int64_t maximum = (std::int64_t{1} << bit_depth) - 1;
  return static_cast<std::uint16_t>(std::clamp(total >> fused.shift, std::int64_t{0}, maximum));
}

inline float apply_yuv_to_yuv_float(const FusedYuvMatrix& fused, int output_plane, float y,
                                    float u, float v) {
  const std::array<double, 3> input{static_cast<double>(y) + fused.matrix.offset_in_f,
                                    static_cast<double>(u), static_cast<double>(v)};
  const auto& coefficients = fused.coefficients[output_plane];
  const double output = coefficients[0] * input[0] + coefficients[1] * input[1] +
                        coefficients[2] * input[2] +
                        (output_plane == 0 ? fused.destination_range.luma_offset : 0.0);
  return static_cast<float>(output);
}

template <typename T, bool LessThan16Bit>
inline void call_yuv_to_yuv_kernel(const YuvToYuvCase& test_case, BYTE* (&destination)[3],
                                   int (&destination_pitch)[3], const BYTE* (&source)[3],
                                   const int (&source_pitch)[3], const ConversionMatrix& matrix) {
  if (test_case.variant.function == YuvToYuvVariant::C) {
    convert_yuv_to_planarrgb_c<ConversionDirection::YUV_TO_YUV, T, LessThan16Bit>(
        destination, destination_pitch, source, source_pitch, static_cast<int>(test_case.width),
        static_cast<int>(test_case.height), matrix, test_case.bit_depth, test_case.bit_depth,
        false);
  } else if (test_case.variant.function == YuvToYuvVariant::Sse2) {
    convert_yuv_to_planarrgb_sse2<ConversionDirection::YUV_TO_YUV, T, LessThan16Bit>(
        destination, destination_pitch, source, source_pitch, static_cast<int>(test_case.width),
        static_cast<int>(test_case.height), matrix, test_case.bit_depth, test_case.bit_depth,
        false);
  } else {
    convert_yuv_to_planarrgb_avx2<ConversionDirection::YUV_TO_YUV, T, LessThan16Bit>(
        destination, destination_pitch, source, source_pitch, static_cast<int>(test_case.width),
        static_cast<int>(test_case.height), matrix, test_case.bit_depth, test_case.bit_depth,
        false);
  }
}

inline std::size_t yuv_to_yuv_allowed_output_end(const YuvToYuvCase& test_case,
                                                  std::size_t element_size) {
  const std::size_t active_bytes = test_case.width * element_size;
  if (test_case.variant.function == YuvToYuvVariant::C) {
    return active_bytes;
  }
  const std::size_t block_bytes =
      test_case.variant.function == YuvToYuvVariant::Sse2
          ? 8 * element_size
          : (test_case.bit_depth == 32 ? 64 : 32 * element_size);
  return ((active_bytes + block_bytes - 1) / block_bytes) * block_bytes;
}

}  // namespace avsut::test
