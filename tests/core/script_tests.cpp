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

TEST(ScriptArrayBounds, RejectsInvalidFlatAndNestedIndexes) {
  AviSynthEnvironment environment;
  for (const char* script : {
      "ArrayIns([10,20],99,-1)", "ArrayIns([10,20],99,3)",
      "ArraySet([10,20],99,-1)", "ArraySet([10,20],99,2)",
      "ArrayDel([10,20],-1)", "ArrayDel([10,20],2)",
      "ArrayIns([[10,20]],99,1,0)", "ArraySet([[10,20]],99,0,2)",
      "ArrayDel([[10,20]],0,-1)", "ArrayAdd([[10,20]],99,1)"}) {
    SCOPED_TRACE(script);
    const AVSValue arg(script);
    EXPECT_THROW(environment.get()->Invoke("Eval", AVSValue(&arg, 1)), AvisynthError);
  }
}

TEST(ScriptArrayBounds, PreservesValidBoundaryAndNestedOperations) {
  AviSynthEnvironment environment;
  for (const char* script : {
      "ArrayIns([10,20],99,0)[0] == 99",
      "ArrayIns([10,20],99,2)[2] == 99",
      "ArraySet([10,20],99,1)[1] == 99",
      "ArrayDel([10,20],1)[0] == 10",
      "ArrayIns([[10,20]],99,0,2)[0,2] == 99",
      "ArraySet([[10,20]],99,0,1)[0,1] == 99",
      "ArrayDel([[10,20]],0,0)[0,0] == 20",
      "ArrayAdd([[10,20]],99,0)[0,2] == 99"}) {
    SCOPED_TRACE(script);
    const AVSValue arg(script);
    EXPECT_TRUE(environment.get()->Invoke("Eval", AVSValue(&arg, 1)).AsBool());
  }
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
