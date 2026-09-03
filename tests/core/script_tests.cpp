#include <gtest/gtest.h>

#include <avisynth.h>
#include "support/avisynth_environment.h"

#include <cstdint>

namespace avsut::test {
namespace {

TEST(ScriptAbs, PreservesSixtyFourBitIntegerRange) {
  AviSynthEnvironment environment;
  const int64_t input_val = -5000000000LL;
  const AVSValue arg(input_val);
  const AVSValue result = environment.get()->Invoke("Abs", AVSValue(&arg, 1));
  EXPECT_TRUE(result.IsInt());
  EXPECT_EQ(result.AsLong(), 5000000000LL);
}

}  // namespace
}  // namespace avsut::test
