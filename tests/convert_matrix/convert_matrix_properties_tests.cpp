#include "convert/convert_helper.h"

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

void export_frame_props(VideoInfo& vi, AVSMap* props, int matrix, int color_range,
                        IScriptEnvironment* environment);

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
  AVSMap* get_writable() const noexcept { return map_; }

 private:
  IScriptEnvironment* environment_{};
  AVSMap* map_{};
};

struct ExportFramePropsCase {
  std::string name;
  int matrix{};
  int color_range{};
  bool seed_matrix{};
  int seed_matrix_value{};
  bool expect_matrix{};
  int expected_matrix{};
};

void PrintTo(const ExportFramePropsCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

std::vector<ExportFramePropsCase> export_frame_props_cases() {
  return {
      {"ReplacesExistingMatrix", AVS_MATRIX_BT709, AVS_COLORRANGE_FULL, true,
       AVS_MATRIX_ST170_M, true, AVS_MATRIX_BT709},
      {"CreatesMatrixWhenAbsent", AVS_MATRIX_BT2020_CL, AVS_COLORRANGE_LIMITED, false, 0, true,
       AVS_MATRIX_BT2020_CL},
      {"DeletesAverageMatrix", AVS_MATRIX_AVERAGE, AVS_COLORRANGE_LIMITED, true,
       AVS_MATRIX_BT709, false, 0},
      {"DeletesNegativeMatrix", -1, AVS_COLORRANGE_FULL, true, AVS_MATRIX_BT709, false, 0},
  };
}

class ExportFrameProps : public ::testing::TestWithParam<ExportFramePropsCase> {};

TEST_P(ExportFrameProps, ReplacesOrDeletesMatrixAndAlwaysExportsRange) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  OwnedPropertyMap properties(environment.get());
  if (test_case.seed_matrix) {
    properties.set_int("_Matrix", test_case.seed_matrix_value);
  }
  properties.set_int("_ColorRange", AVS_COLORRANGE_FULL);
  auto video_info = make_video_info(VideoInfoSpec{8, 4, VideoInfo::CS_YV24, 1, 25, 1});

  export_frame_props(video_info, properties.get_writable(), test_case.matrix,
                     test_case.color_range, environment.get());

  ASSERT_EQ(environment.get()->propNumElements(properties.get(), "_ColorRange"), 1)
      << test_case.name;
  int error = 0;
  EXPECT_EQ(environment.get()->propGetInt(properties.get(), "_ColorRange", 0, &error),
            test_case.color_range)
      << test_case.name;
  EXPECT_EQ(error, 0) << test_case.name;
  const int matrix_elements = environment.get()->propNumElements(properties.get(), "_Matrix");
  if (test_case.expect_matrix) {
    EXPECT_EQ(matrix_elements, 1) << test_case.name;
  } else {
    EXPECT_LE(matrix_elements, 0) << test_case.name;
  }
  if (test_case.expect_matrix) {
    error = 0;
    EXPECT_EQ(environment.get()->propGetInt(properties.get(), "_Matrix", 0, &error),
              test_case.expected_matrix)
        << test_case.name;
    EXPECT_EQ(error, 0) << test_case.name;
  }
}

INSTANTIATE_TEST_SUITE_P(
    MatrixPropertyActions,
    ExportFrameProps,
    ::testing::ValuesIn(export_frame_props_cases()),
    [](const ::testing::TestParamInfo<ExportFramePropsCase>& info) { return info.param.name; });

struct ChromaLocationCase {
  std::string name;
  int pixel_type{};
  std::string chromaloc_name;
  bool use_null_name{};
  bool has_property{};
  int property_value{};
  int default_value{};
  int expected_value{};
};

void PrintTo(const ChromaLocationCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

std::vector<ChromaLocationCase> chroma_location_cases() {
  return {
      {"Yv12_AutoUsesProperty", VideoInfo::CS_YV12, "auto", false, true,
       AVS_CHROMA_CENTER, AVS_CHROMA_LEFT, AVS_CHROMA_CENTER},
      {"Yv16_EmptyUsesProperty", VideoInfo::CS_YV16, "", false, true, AVS_CHROMA_TOP_LEFT,
       AVS_CHROMA_LEFT, AVS_CHROMA_TOP_LEFT},
      {"Yv411_AutoUsesProperty", VideoInfo::CS_YV411, "auto", false, true, AVS_CHROMA_LEFT,
       AVS_CHROMA_CENTER, AVS_CHROMA_LEFT},
      {"Yv12_ExplicitOverridesProperty", VideoInfo::CS_YV12, "left", false, true,
       AVS_CHROMA_CENTER, AVS_CHROMA_TOP_LEFT, AVS_CHROMA_LEFT},
      {"Yv12_CompatibilityAliasMapsToCenter", VideoInfo::CS_YV12, "mpeg1", false, false, 0,
       AVS_CHROMA_LEFT, AVS_CHROMA_CENTER},
      {"Yv12_NullNameUsesDefault", VideoInfo::CS_YV12, "", true, false, 0, AVS_CHROMA_TOP_LEFT,
       AVS_CHROMA_TOP_LEFT},
      {"Yv24_AutoIgnoresProperty", VideoInfo::CS_YV24, "auto", false, true,
       AVS_CHROMA_CENTER, AVS_CHROMA_LEFT, AVS_CHROMA_LEFT},
      {"Yv24_ExplicitUsesArgument", VideoInfo::CS_YV24, "center", false, true,
       AVS_CHROMA_LEFT, AVS_CHROMA_TOP_LEFT, AVS_CHROMA_CENTER},
  };
}

class ChromaLocationParser : public ::testing::TestWithParam<ChromaLocationCase> {};

TEST_P(ChromaLocationParser, AppliesSupportedFormatPropertyAndExplicitPrecedence) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  auto video_info = make_video_info(VideoInfoSpec{8, 4, test_case.pixel_type, 1, 25, 1});
  std::unique_ptr<OwnedPropertyMap> properties;
  if (test_case.has_property) {
    properties = std::make_unique<OwnedPropertyMap>(environment.get());
    properties->set_int("_ChromaLocation", test_case.property_value);
  }

  int actual = -1;
  chromaloc_parse_merge_with_props(video_info,
                                   test_case.use_null_name ? nullptr
                                                           : test_case.chromaloc_name.c_str(),
                                   properties == nullptr ? nullptr : properties->get(), actual,
                                   test_case.default_value, environment.get());

  EXPECT_EQ(actual, test_case.expected_value) << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(
    FormatAndPrecedence,
    ChromaLocationParser,
    ::testing::ValuesIn(chroma_location_cases()),
    [](const ::testing::TestParamInfo<ChromaLocationCase>& info) { return info.param.name; });

TEST(ChromaLocationParser, RejectsUnknownExplicitLocation) {
  AviSynthEnvironment environment;
  auto video_info = make_video_info(VideoInfoSpec{8, 4, VideoInfo::CS_YV12, 1, 25, 1});
  int actual = -1;

  EXPECT_THROW(
      chromaloc_parse_merge_with_props(video_info, "not-a-location", nullptr, actual,
                                       AVS_CHROMA_LEFT, environment.get()),
      AvisynthError);
}

}  // namespace
}  // namespace avsut::test
