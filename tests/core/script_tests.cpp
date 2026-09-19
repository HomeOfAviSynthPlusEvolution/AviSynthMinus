#include <gtest/gtest.h>

#include <avisynth.h>
#include "support/avisynth_environment.h"

#include <cstdint>
#include <limits>
#include <string>

namespace avsut::test {
namespace {

TEST(ScriptFormatting, PreservesLargeFloatsAndDefaultPrecision) {
  AviSynthEnvironment environment;
  auto* env = environment.get();
  for (double value : {std::numeric_limits<double>::max(),
                       -std::numeric_limits<double>::max(), 1.25, -0.0}) {
    const AVSValue input(value);
    const std::string plain = env->Invoke("String", AVSValue(&input, 1)).AsString();
    const AVSValue format_args[] = {AVSValue("{}"), input};
    const std::string formatted = env->Invoke("Format", AVSValue(format_args, 2)).AsString();
    EXPECT_EQ(plain, formatted);
    if (value == std::numeric_limits<double>::max() ||
        value == -std::numeric_limits<double>::max()) {
      EXPECT_EQ(plain.size(), value < 0 ? 317u : 316u);
      EXPECT_EQ(plain.substr(plain.size() - 7), ".000000");
      EXPECT_EQ(std::stod(plain), value);
    } else {
      EXPECT_EQ(plain, value == 1.25 ? "1.250000" : "-0.000000");
    }
  }
}

TEST(ScriptFormatting, PreservesFullWidthIntegers) {
  AviSynthEnvironment environment;
  for (int64_t value : {INT64_C(5000000000), INT64_C(-5000000000),
                        std::numeric_limits<int64_t>::min(),
                        std::numeric_limits<int64_t>::max()}) {
    const AVSValue args[] = {AVSValue("{}"), AVSValue(value)};
    EXPECT_EQ(environment.get()->Invoke("Format", AVSValue(args, 2)).AsString(),
              std::to_string(value));
  }
}

TEST(ScriptCapture, AcceptsLimitAndRejectsOverflow) {
  AviSynthEnvironment environment;
  for (int count : {1023, 1024, 1025}) {
    std::string script;
    for (int i = 0; i < count; ++i)
      script += "v" + std::to_string(i) + " = 1\n";
    script += "f = function[";
    for (int i = 0; i < count; ++i) {
      if (i) script += ',';
      script += "v" + std::to_string(i);
    }
    script += "]() { return v0 }\nf()";
    const AVSValue arg(script.c_str());
    if (count <= 1024) {
      EXPECT_EQ(environment.get()->Invoke("Eval", AVSValue(&arg, 1)).AsInt(), 1);
    } else {
      try {
        environment.get()->Invoke("Eval", AVSValue(&arg, 1));
        FAIL() << "Oversized capture list was accepted";
      } catch (const AvisynthError& error) {
        EXPECT_NE(std::string(error.msg).find("variable capture list too long"), std::string::npos);
      }
    }
  }
}

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
