#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B11_SOURCE_UNDEF_AVS_UNUSED
#endif
#include "core/parser/script.h"
#ifdef AVSUT_FINDING_B11_SOURCE_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B11_SOURCE_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <ostream>

namespace avsut::test {
namespace {

AVSValue script_generated_nan(IScriptEnvironment* environment) {
  const AVSValue negative_one(-1.0F);
  return Sqrt(AVSValue(&negative_one, 1), nullptr, environment);
}

PClip invoke_blank_clip(IScriptEnvironment* environment, const AVSValue* arguments,
                        int argument_count, const char* const* argument_names) {
  return environment->Invoke("BlankClip", AVSValue(arguments, argument_count), argument_names)
      .AsClip();
}

bool blank_clip_rejects_too_many_colors() {
  try {
    AviSynthEnvironment environment;
    const std::array<AVSValue, 5> colors{1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
    const std::array<AVSValue, 4> arguments{
        4,
        4,
        "RGB32",
        AVSValue(colors.data(), static_cast<int>(colors.size())),
    };
    constexpr std::array<const char*, 4> names{"width", "height", "pixel_type", "colors"};
    const PClip clip = invoke_blank_clip(environment.get(), arguments.data(),
                                         static_cast<int>(arguments.size()), names.data());
    return false;
  } catch (const AvisynthError&) {
    return true;
  } catch (...) {
    return false;
  }
}

TEST(BlankClipFactory, RejectsColorArraysBeyondFourComponents) {
  EXPECT_EXIT(
      { std::_Exit(blank_clip_rejects_too_many_colors() ? EXIT_SUCCESS : EXIT_FAILURE); },
      ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

bool blank_clip_rejects_non_finite_fps() {
  try {
    AviSynthEnvironment environment;
    const AVSValue fps = script_generated_nan(environment.get());
    if (!std::isnan(fps.AsFloat())) {
      return false;
    }
    const std::array<AVSValue, 1> arguments{fps};
    constexpr std::array<const char*, 1> names{"fps"};
    const PClip clip = invoke_blank_clip(environment.get(), arguments.data(),
                                         static_cast<int>(arguments.size()), names.data());
    return false;
  } catch (const AvisynthError&) {
    return true;
  } catch (...) {
    return false;
  }
}

TEST(BlankClipFactory, RejectsScriptGeneratedNanFps) {
  EXPECT_EXIT(
      { std::_Exit(blank_clip_rejects_non_finite_fps() ? EXIT_SUCCESS : EXIT_FAILURE); },
      ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

struct ToneMetadataCase {
  const char* name;
  const char* parameter_name;
  int value;
};

void PrintTo(const ToneMetadataCase& test_case, std::ostream* output) { *output << test_case.name; }

bool tone_rejects_invalid_metadata(const ToneMetadataCase& test_case) {
  try {
    AviSynthEnvironment environment;
    const std::array<AVSValue, 1> arguments{test_case.value};
    const std::array<const char*, 1> names{test_case.parameter_name};
    const PClip clip =
        environment.get()
            ->Invoke("Tone", AVSValue(arguments.data(), static_cast<int>(arguments.size())),
                     names.data())
            .AsClip();
    return false;
  } catch (const AvisynthError&) {
    return true;
  } catch (...) {
    return false;
  }
}

class ToneFactory : public ::testing::TestWithParam<ToneMetadataCase> {};

TEST_P(ToneFactory, RejectsZeroRateOrChannelCount) {
  const auto& test_case = GetParam();
  EXPECT_EXIT(
      { std::_Exit(tone_rejects_invalid_metadata(test_case) ? EXIT_SUCCESS : EXIT_FAILURE); },
      ::testing::ExitedWithCode(EXIT_SUCCESS), "")
      << "B11 Tone parameter=" << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(B11, ToneFactory,
                         ::testing::Values(ToneMetadataCase{"ZeroSampleRate", "samplerate", 0},
                                           ToneMetadataCase{"ZeroChannelCount", "channels", 0}),
                         [](const ::testing::TestParamInfo<ToneMetadataCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
