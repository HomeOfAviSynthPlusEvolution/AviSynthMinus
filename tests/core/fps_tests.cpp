#include <gtest/gtest.h>

#include <avisynth.h>
#include "support/avisynth_environment.h"

#include <string>

namespace avsut::test {
namespace {

TEST(FpsValidation, RejectsNonPositiveRationalFrameRates) {
  AviSynthEnvironment environment;
  for (const char* filter : {"ChangeFPS", "ConvertFPS"}) {
    for (const char* rate : {"0", "-1", "25,0", "25,-1"}) {
      const std::string script = std::string("BlankClip(width=16,height=8,length=2).") +
          filter + "(" + rate + ")";
      SCOPED_TRACE(script);
      EXPECT_THROW(environment.get()->Invoke("Eval", AVSValue(script.c_str())), AvisynthError);
    }
  }
  // Zero numerator must also be rejected when bypassing ChangeFPS's linear check.
  EXPECT_THROW(environment.get()->Invoke("Eval", AVSValue(
      "BlankClip(width=16,height=8,length=2).ChangeFPS(0,linear=false)")), AvisynthError);
  // Zone mode performs a separate division during construction.
  EXPECT_THROW(environment.get()->Invoke("Eval", AVSValue(
      "BlankClip(width=16,height=8,length=2,pixel_type=\"YUY2\").ConvertFPS(25,0,zone=0)")),
      AvisynthError);
}

TEST(FpsValidation, RejectsSourceWithoutFrameRate) {
  AviSynthEnvironment environment;
  for (const char* filter : {"ChangeFPS", "ConvertFPS"}) {
    const std::string script = std::string("Tone(length=0.1).") + filter + "(25)";
    SCOPED_TRACE(script);
    EXPECT_THROW(environment.get()->Invoke("Eval", AVSValue(script.c_str())), AvisynthError);
  }
}

TEST(FpsValidation, RejectsInvalidContinuedFractionDenominatorAndLimit) {
  AviSynthEnvironment environment;
  for (const char* function : {"ContinuedNumerator", "ContinuedDenominator"}) {
    for (const char* arguments : {"25,0", "25,-1", "25,1,limit=0", "25,1,limit=-1",
                                 "25.0,limit=0", "25.0,limit=-1"}) {
      const std::string script = std::string(function) + "(" + arguments + ")";
      SCOPED_TRACE(script);
      EXPECT_THROW(environment.get()->Invoke("Eval", AVSValue(script.c_str())), AvisynthError);
    }
  }
  EXPECT_EQ(environment.get()->Invoke("Eval", AVSValue("ContinuedNumerator(0,1)")).AsInt(), 0);
  EXPECT_EQ(environment.get()->Invoke("Eval", AVSValue("ContinuedDenominator(0,1)")).AsInt(), 1);
}

TEST(FpsValidation, PreservesValidRationalConversions) {
  AviSynthEnvironment environment;
  for (const char* filter : {"ChangeFPS", "ConvertFPS"}) {
    const std::string script = std::string("BlankClip(width=16,height=8,length=2,fps=25).") +
        filter + "(50,1)";
    const PClip clip = environment.get()->Invoke("Eval", AVSValue(script.c_str())).AsClip();
    EXPECT_EQ(clip->GetVideoInfo().num_frames, 4);
    EXPECT_EQ(clip->GetVideoInfo().fps_numerator, 50u);
    EXPECT_EQ(clip->GetVideoInfo().fps_denominator, 1u);
    EXPECT_NO_THROW(clip->GetFrame(0, environment.get()));
  }
  EXPECT_EQ(environment.get()->Invoke("Eval", AVSValue("ContinuedNumerator(30000,1001)")).AsInt(), 30000);
  EXPECT_EQ(environment.get()->Invoke("Eval", AVSValue("ContinuedDenominator(30000,1001)")).AsInt(), 1001);
}

} // namespace
} // namespace avsut::test
