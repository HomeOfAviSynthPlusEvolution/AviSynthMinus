#include "convert/convert_helper.h"
#include "support/compat_375.h"

#include "support/avisynth_environment.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace avsut::test {
namespace {

class OwnedPropertyMap {
 public:
  explicit OwnedPropertyMap(IScriptEnvironment* environment) : environment_(environment) {
    map_ = environment_->createMap();
    if (map_ == nullptr) {
      throw std::runtime_error("createMap returned null");
    }
  }

  ~OwnedPropertyMap() {
    if (map_ != nullptr) {
      environment_->freeMap(map_);
    }
  }

  OwnedPropertyMap(const OwnedPropertyMap&) = delete;
  OwnedPropertyMap& operator=(const OwnedPropertyMap&) = delete;

  void set_int(const char* key, int value) {
    if (environment_->propSetInt(map_, key, value, PROPAPPENDMODE_REPLACE) != 0) {
      throw std::runtime_error(std::string("propSetInt failed for ") + key);
    }
  }

  const AVSMap* get() const noexcept { return map_; }

 private:
  IScriptEnvironment* environment_{};
  AVSMap* map_{};
};

struct MatrixParseCase {
  std::string name;
  bool rgb_in{};
  bool rgb_out{};
  std::string matrix_name;
  bool use_null_name{};
  bool has_matrix_property{};
  int matrix_property{};
  bool has_color_range_property{};
  int color_range_property{};
  int expected_matrix{};
  int expected_color_range{};
  int expected_color_range_out{};
};

void PrintTo(const MatrixParseCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

MatrixParseCase make_matrix_parse_case(
    std::string name, bool rgb_in, bool rgb_out, const char* matrix_name, bool use_null_name,
    int expected_matrix, int expected_color_range, int expected_color_range_out,
    bool has_matrix_property = false, int matrix_property = 0,
    bool has_color_range_property = false, int color_range_property = 0) {
  return MatrixParseCase{std::move(name),
                         rgb_in,
                         rgb_out,
                         matrix_name == nullptr ? std::string{} : std::string(matrix_name),
                         use_null_name,
                         has_matrix_property,
                         matrix_property,
                         has_color_range_property,
                         color_range_property,
                         expected_matrix,
                         expected_color_range,
                         expected_color_range_out};
}

std::vector<MatrixParseCase> matrix_parse_cases() {
  constexpr int full = AVS_COLORRANGE_FULL;
  constexpr int limited = AVS_COLORRANGE_LIMITED;
  return {
      make_matrix_parse_case("RgbToYuv_709Limited", true, false, "709:limited", false,
                             AVS_MATRIX_BT709, full, limited),
      make_matrix_parse_case("RgbToYuv_709Full", true, false, "709:full", false,
                             AVS_MATRIX_BT709, full, full),
      make_matrix_parse_case("RgbToYuv_709Same", true, false, "709:same", false,
                             AVS_MATRIX_BT709, full, full),
      make_matrix_parse_case("YuvToRgb_2020Full", false, true, "2020:f", false,
                             AVS_MATRIX_BT2020_NCL, full, full),
      make_matrix_parse_case("YuvToRgb_AutoUsesProperties", false, true, "auto", false,
                             AVS_MATRIX_BT709, full, full, true, AVS_MATRIX_BT709, true, full),
      make_matrix_parse_case("YuvToRgb_AutoUnspecifiedPropertyUsesDefault", false, true, "auto",
                             false, AVS_MATRIX_ST170_M, full, full, true,
                             AVS_MATRIX_UNSPECIFIED, true, full),
      make_matrix_parse_case("RgbToYuv_AutoUsesRangeProperty", true, false, "auto", false,
                             AVS_MATRIX_ST170_M, limited, limited, false, 0, true, limited),
      make_matrix_parse_case("RgbToYuv_ExplicitRangePreservesPropertyInput", true, false,
                             "709:full", false, AVS_MATRIX_BT709, limited, full, false, 0, true,
                             limited),
      make_matrix_parse_case("YuvToRgb_ExplicitRangeOverridesPropertyInput", false, true,
                             "709:full", false, AVS_MATRIX_BT709, full, full, false, 0, true,
                             limited),
      make_matrix_parse_case("RgbToYuv_NullNameUsesDefaults", true, false, nullptr, true,
                             AVS_MATRIX_ST170_M, full, limited),
      make_matrix_parse_case("YuvToRgb_EmptyNameUsesDefaults", false, true, "", false,
                             AVS_MATRIX_ST170_M, limited, full),
      make_matrix_parse_case("YuvToRgb_Rec601", false, true, "Rec601", false,
                             AVS_MATRIX_ST170_M, limited, full),
      make_matrix_parse_case("YuvToRgb_PC601", false, true, "PC.601", false,
                             AVS_MATRIX_ST170_M, limited, limited),
      make_matrix_parse_case("RgbToYuv_Rec709", true, false, "Rec709", false,
                             AVS_MATRIX_BT709, full, limited),
      make_matrix_parse_case("RgbToYuv_PC709", true, false, "PC.709", false,
                             AVS_MATRIX_BT709, full, full),
      make_matrix_parse_case("YuvToRgb_Rec2020", false, true, "Rec2020", false,
                             AVS_MATRIX_BT2020_NCL, limited, full),
      make_matrix_parse_case("RgbToYuv_PC2020", true, false, "PC.2020", false,
                             AVS_MATRIX_BT2020_NCL, full, full),
      make_matrix_parse_case("YuvToRgb_YcgcoParserAcceptance", false, true, "ycgco", false,
                             AVS_MATRIX_YCGCO, limited, full),
      make_matrix_parse_case("YuvToRgb_IctcpParserAcceptance", false, true, "ictcp", false,
                             AVS_MATRIX_ICTCP, limited, full),
  };
}

class MatrixParser : public ::testing::TestWithParam<MatrixParseCase> {};

TEST_P(MatrixParser, ResolvesMatrixAndIndependentInputOutputRanges) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;

  OwnedPropertyMap* owned_properties = nullptr;
  std::unique_ptr<OwnedPropertyMap> properties;
  if (test_case.has_matrix_property || test_case.has_color_range_property) {
    properties = std::make_unique<OwnedPropertyMap>(environment.get());
    owned_properties = properties.get();
    if (test_case.has_matrix_property) {
      owned_properties->set_int("_Matrix", test_case.matrix_property);
    }
    if (test_case.has_color_range_property) {
      owned_properties->set_int("_ColorRange", test_case.color_range_property);
    }
  }

  int actual_matrix = -1;
  int actual_color_range = -1;
  int actual_color_range_out = -1;
  matrix_parse_merge_with_props(
      test_case.rgb_in, test_case.rgb_out,
      test_case.use_null_name ? nullptr : test_case.matrix_name.c_str(),
      owned_properties == nullptr ? nullptr : owned_properties->get(), actual_matrix,
      actual_color_range, actual_color_range_out, environment.get());

  EXPECT_EQ(actual_matrix, test_case.expected_matrix) << test_case.name;
  EXPECT_EQ(actual_color_range, test_case.expected_color_range) << test_case.name;
  EXPECT_EQ(actual_color_range_out, test_case.expected_color_range_out) << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(
    NamesAndProperties,
    MatrixParser,
    ::testing::ValuesIn(matrix_parse_cases()),
    [](const ::testing::TestParamInfo<MatrixParseCase>& info) { return info.param.name; });

TEST(MatrixParserDefaults, UsesCallerMatrixDefaultForEmptyName) {
  AviSynthEnvironment environment;
  int actual_matrix = -1;
  int actual_color_range = -1;
  int actual_color_range_out = -1;

  matrix_parse_merge_with_props_def(false, true, nullptr, nullptr, actual_matrix,
                                    actual_color_range, actual_color_range_out,
                                    AVS_MATRIX_BT2020_CL, AVS_COLORRANGE_LIMITED,
                                    environment.get());

  EXPECT_EQ(actual_matrix, AVS_MATRIX_BT2020_CL);
  EXPECT_EQ(actual_color_range, AVS_COLORRANGE_LIMITED);
  EXPECT_EQ(actual_color_range_out, AVS_COLORRANGE_FULL);
}

struct MatrixParserErrorCase {
  std::string name;
  bool rgb_in{};
  bool rgb_out{};
  std::string matrix_name;
  std::string expected_message;
};

void PrintTo(const MatrixParserErrorCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

std::vector<MatrixParserErrorCase> matrix_parser_error_cases() {
  return {
      {"UnknownMatrixName", false, true, "not-a-matrix", "Unknown matrix"},
      {"UnknownColorRangeName", true, false, "709:studio", "Unknown color range"},
      {"TooManyColonParts", false, true, "709:limited:extra", "too many parts"},
      {"OldStyleRejectsExplicitRange", false, true, "Rec709:full", "old-style"},
  };
}

class MatrixParserErrors : public ::testing::TestWithParam<MatrixParserErrorCase> {};

TEST_P(MatrixParserErrors, RejectsInvalidSpecifierWithDiagnostic) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  int actual_matrix = -1;
  int actual_color_range = -1;
  int actual_color_range_out = -1;

  try {
    matrix_parse_merge_with_props(test_case.rgb_in, test_case.rgb_out,
                                  test_case.matrix_name.c_str(), nullptr, actual_matrix,
                                  actual_color_range, actual_color_range_out, environment.get());
    ADD_FAILURE() << test_case.name << " was accepted";
  } catch (const AvisynthError& error) {
    ASSERT_NE(error.msg, nullptr) << test_case.name;
    EXPECT_NE(std::string(error.msg).find(test_case.expected_message), std::string::npos)
        << test_case.name << " diagnostic=" << error.msg;
  } catch (...) {
    ADD_FAILURE() << test_case.name << " threw a non-Avisynth exception";
  }
}

INSTANTIATE_TEST_SUITE_P(
    InvalidSpecifiers,
    MatrixParserErrors,
    ::testing::ValuesIn(matrix_parser_error_cases()),
    [](const ::testing::TestParamInfo<MatrixParserErrorCase>& info) { return info.param.name; });

}  // namespace
}  // namespace avsut::test
