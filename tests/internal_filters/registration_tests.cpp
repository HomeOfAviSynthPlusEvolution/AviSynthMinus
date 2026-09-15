#include "support/video_filter_test_support.h"
#include <gtest/gtest.h>
#include <map>
#include <sstream>
#include <string>

TEST(InternalFilters, ExportsOriginalNamesAndOverloadsExactlyOnce) {
  avsut::test::AviSynthEnvironment environment;
  auto* env = environment.get();
  std::istringstream list(env->GetVar("$InternalFunctions$").AsString());
  std::map<std::string, int> actual;
  std::string name;
  while (list >> name)
    ++actual[name];
  const std::map<std::string, int> expected = {
      {"BlankClip", 4},
      {"Blackness", 2},
      {"ShowAlpha", 1},
      {"ShowRed", 1},
      {"ShowGreen", 1},
      {"ShowBlue", 1},
      {"ShowY", 1},
      {"ShowU", 1},
      {"ShowV", 1},
      {"Levels", 1},
      {"RGBAdjust", 1},
      {"Tweak", 1},
      {"ColorYUV", 1},
      {"ColorBars", 1},
      {"ColorBarsHD", 1},
      {"GeneralConvolution", 1},
      {"Crop", 1},
      {"CropBottom", 1},
      {"AddBorders", 1},
      {"Letterbox", 1},
      {"Blur", 1},
      {"Sharpen", 1},
      {"TemporalSoften", 1},
      {"SpatialSoften", 1},
      {"Greyscale", 1},
      {"Grayscale", 1},
      {"Invert", 1},
      {"Limiter", 1},
      {"Merge", 1},
      {"MergeChroma", 2},
      {"MergeLuma", 2},
      {"SwapUV", 1},
      {"UToY", 1},
      {"VToY", 1},
      {"UToY8", 1},
      {"VToY8", 1},
      {"ExtractY", 1},
      {"ExtractU", 1},
      {"ExtractV", 1},
      {"ExtractA", 1},
      {"ExtractR", 1},
      {"ExtractG", 1},
      {"ExtractB", 1},
      {"YToUV", 3},
      {"PlaneToY", 1},
      {"CombinePlanes", 4},
      {"MergeRGB", 1},
      {"MergeARGB", 1},
      {"TurnLeft", 1},
      {"TurnRight", 1},
      {"Turn180", 1},
      {"FlipHorizontal", 1},
      {"FlipVertical", 1},
      {"SeparateColumns", 1},
      {"WeaveColumns", 1},
      {"SeparateRows", 1},
      {"WeaveRows", 1},
      {"StackVertical", 1},
      {"StackHorizontal", 1},
      {"ShowFiveVersions", 1},
  };
  for (const auto& entry : expected) {
    EXPECT_EQ(actual[entry.first], entry.second) << entry.first;
    EXPECT_FALSE(env->FunctionExists(("IF" + entry.first).c_str())) << entry.first;
  }
}

TEST(InternalFilters, InvokesEveryFamilyThroughBuiltinRegistration) {
  avsut::test::AviSynthEnvironment environment;
  auto* env = environment.get();
  const char* expressions[] = {
      R"avs(Blur(BlankClip(width=32,height=16,pixel_type="Y8"),0.3))avs",
      R"avs(TurnRight(BlankClip(width=32,height=16)))avs",
      R"avs(Crop(BlankClip(width=32,height=16),2,2,24,12))avs",
      R"avs(ColorBars(width=640,height=480))avs",
      R"avs(Greyscale(BlankClip(width=32,height=16,pixel_type="RGB32")))avs",
      R"avs(Invert(BlankClip(width=32,height=16)))avs",
      R"avs(Limiter(BlankClip(width=32,height=16,pixel_type="YV12")))avs",
      R"avs(ExtractY(BlankClip(width=32,height=16,pixel_type="YV12")))avs",
      R"avs(ShowRed(BlankClip(width=32,height=16,pixel_type="RGB32")))avs",
      R"avs(MergeRGB(BlankClip(pixel_type="Y8"),BlankClip(pixel_type="Y8"),BlankClip(pixel_type="Y8")))avs",
      R"avs(StackVertical(BlankClip(width=32,height=16),BlankClip(width=32,height=16)))avs",
      R"avs(SeparateColumns(BlankClip(width=32,height=16),2))avs",
      R"avs(Merge(BlankClip(width=32,height=16),BlankClip(width=32,height=16,color=$ffffff),0.25))avs",
      R"avs(Levels(BlankClip(width=32,height=16),0,1,255,16,235))avs",
      R"avs(GeneralConvolution(BlankClip(width=32,height=16,pixel_type="RGB32")))avs",
  };
  for (const char* expression : expressions) {
    SCOPED_TRACE(expression);
    try {
      auto clip = env->Invoke("Eval", expression).AsClip();
      auto frame = clip->GetFrame(0, env);
      EXPECT_GT(frame->GetRowSize(), 0);
    } catch (const AvisynthError& error) {
      FAIL() << error.msg;
    }
  }
}
