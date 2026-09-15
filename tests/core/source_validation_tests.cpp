#include <gtest/gtest.h>
#include "support/avisynth_environment.h"
#include <array>
#include <cmath>
namespace avsut::test {
namespace {
TEST(BlankClipFactory, RejectsColorArraysBeyondFourComponents) {
  AviSynthEnvironment environment;
  const std::array<AVSValue, 5> colors{1.0F, 2.0F, 3.0F, 4.0F, 5.0F};
  const AVSValue args[] = {4, 4, "RGB32", AVSValue(colors.data(), int(colors.size()))};
  const char* names[] = {"width", "height", "pixel_type", "colors"};
  EXPECT_THROW(environment.get()->Invoke("BlankClip", AVSValue(args, 4), names), AvisynthError);
}
TEST(BlankClipFactory, RejectsScriptGeneratedNanFps) {
  AviSynthEnvironment environment;
  const auto nan = environment.get()->Invoke("Eval", "Sqrt(-1.0)");
  ASSERT_TRUE(std::isnan(nan.AsFloat()));
  const char* names[] = {"fps"};
  const AVSValue args[] = {nan};
  EXPECT_THROW(environment.get()->Invoke("BlankClip", AVSValue(args, 1), names), AvisynthError);
}
TEST(ToneFactory, RejectsZeroRateOrChannelCount) {
  AviSynthEnvironment environment;
  for (const char* parameter : {"samplerate", "channels"}) {
    SCOPED_TRACE(parameter);
    const char* names[] = {parameter};
    const AVSValue args[] = {0};
    EXPECT_THROW(environment.get()->Invoke("Tone", AVSValue(args, 1), names), AvisynthError);
  }
}
} // namespace
} // namespace avsut::test
