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

TEST(SetMaxCPU, IsolatesFlagsToEnvironmentInstance) {
  AviSynthEnvironment env1;
  AviSynthEnvironment env2;
  const int initial_flags = env2.get()->GetCPUFlags();
  ASSERT_NE(initial_flags, 0);

  const AVSValue none("none");
  env1.get()->Invoke("SetMaxCPU", AVSValue(&none, 1));
  constexpr int simd_mask = CPUF_MMX | CPUF_SSE | CPUF_SSE2 | CPUF_AVX | CPUF_AVX2;
  EXPECT_EQ(env1.get()->GetCPUFlags() & simd_mask, 0);
  EXPECT_EQ(env2.get()->GetCPUFlags(), initial_flags)
      << "SetMaxCPU on one environment altered CPU flags in an independent environment";
}

}  // namespace
}  // namespace avsut::test
