#include <gtest/gtest.h>
#include "support/avisynth_environment.h"
#include <vector>
#include <cstring>
#include <algorithm>
using avsut::test::AviSynthEnvironment;
extern "C" int v12_c_probe(void);
namespace {
void Load(IScriptEnvironment *env, const char *path) { env->Invoke("LoadPlugin", AVSValue(path)); }
PClip Blank(IScriptEnvironment *env) {
  const AVSValue a[] = {320, 96, 20, "RGB32"};
  const char *names[] = {"width", "height", "length", "pixel_type"};
  return env->Invoke("BlankClip", AVSValue(a, 4), names).AsClip();
}
std::vector<BYTE> Bytes(PVideoFrame f) {
  std::vector<BYTE> b;
  for (int y = 0; y < f->GetHeight(); ++y) {
    auto *p = f->GetReadPtr() + y * f->GetPitch();
    b.insert(b.end(), p, p + f->GetRowSize());
  }
  return b;
}
TEST(V12Compatibility, OriginalSdksLoadAndCallTheirSupportedMethods) {
  for (auto path : {V12_v11_PATH, V12_partial_PATH, V12_full_PATH, V12_cx_PATH, V12_oldcx_PATH}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment e;
    auto *env = e.get(); Load(env, path);
    EXPECT_TRUE(env->Invoke("V12Lock", AVSValue(nullptr, 0)).AsBool());
    EXPECT_EQ(uint32_t(env->Invoke("V12Flags", AVSValue(nullptr, 0)).AsLong()), uint32_t(env->GetCPUFlags()));
  }
  EXPECT_EQ(v12_c_probe(), 1);
}
TEST(V12Compatibility, MissingMessageFeatureDoesNotPreventOtherServices) {
  AviSynthEnvironment e; auto *env = e.get(); Load(env, V12_missing_PATH);
  EXPECT_TRUE(env->Invoke("V12MissingVerified", AVSValue(nullptr, 0)).AsBool());
}
TEST(V12Compatibility, CpuHighBitsAndL2ReachUpstreamAndCxPlugins) {
  for (auto path : {V12_full_PATH, V12_cx_PATH}) {
    AviSynthEnvironment e;
    auto *env = e.get(); Load(env, path);
    EXPECT_EQ(env->Invoke("V12L2", AVSValue(nullptr, 0)).AsLong(), int64_t(env->GetEnvProperty(AEP_CACHESIZE_L2)));
#if defined(X86_32) || defined(X86_64)
    // Force high bits only for querying; never execute SIMD under this synthetic policy.
    env->Invoke("SetMaxCPU", "none,avx512fast+");
    EXPECT_NE(env->GetCPUFlagsEx() & CPUF_AVX512VNNI, 0);
#endif
    EXPECT_EQ(env->Invoke("V12Flags", AVSValue(nullptr, 0)).AsLong(), env->GetCPUFlagsEx());
    env->Invoke("SetMaxCPU", "none");
    EXPECT_EQ(env->Invoke("V12Flags", AVSValue(nullptr, 0)).AsLong(), env->GetCPUFlagsEx());
  }
}
TEST(V12Compatibility, MessageRenderingMatchesLegacyAndPreservesInput) {
  std::vector<BYTE> expected;
  for (auto path : {V12_v11_PATH, V12_partial_PATH, V12_full_PATH, V12_cx_PATH}) {
    for (bool ex : {false, true}) {
      AviSynthEnvironment e;
      auto *env = e.get(); Load(env, path);
      PClip source = Blank(env);
      PVideoFrame original = source->GetFrame(0, env);
      const auto before = Bytes(original);
      const AVSValue a[] = {source, "V12 message", ex, false};
      PClip rendered = env->Invoke("V12Draw", AVSValue(a, 4)).AsClip();
      auto actual = Bytes(rendered->GetFrame(0, env));
      EXPECT_NE(actual, before);
      EXPECT_EQ(Bytes(original), before);
      if (expected.empty()) expected = actual;
      EXPECT_EQ(actual, expected) << path << " Ex=" << ex;
    }
  }
}
TEST(V12Compatibility, Utf8RenderingMatchesAcrossEntrypoints) {
  std::vector<BYTE> expected;
  for (auto path : {V12_full_PATH, V12_cx_PATH}) {
    AviSynthEnvironment e; auto *env = e.get(); Load(env, path);
    PClip source = Blank(env);
    const AVSValue a[] = {source, "V12 \xC3\xA9 \xCE\xA9", true, true};
    const auto actual = Bytes(env->Invoke("V12Draw", AVSValue(a, 4)).AsClip()->GetFrame(0, env));
    if (expected.empty()) expected = actual;
    EXPECT_EQ(actual, expected);
    EXPECT_NE(actual, Bytes(source->GetFrame(0, env)));
  }
}
TEST(V12Compatibility, PrefetchNotifiesEachModeThroughBothEntrypoints) {
  for (auto path : {V12_full_PATH, V12_cx_PATH})
    for (int mode : {MT_NICE_FILTER, MT_MULTI_INSTANCE, MT_SERIALIZED})
      for (int threads : {0, 1, 3}) {
        SCOPED_TRACE(::testing::Message() << path << " mode=" << mode << " threads=" << threads);
        AviSynthEnvironment e; auto *env = e.get(); Load(env, path);
        const AVSValue args[] = {Blank(env), mode};
        PClip probe = env->Invoke("V12Probe", AVSValue(args, 2)).AsClip();
        if (threads) {
          const AVSValue pf[] = {probe, threads};
          probe = env->Invoke("Prefetch", AVSValue(pf, 2)).AsClip();
        }
        for (int n = 0; n < 12; ++n) {
          auto f = probe->GetFrame(n, env);
          auto *p = env->getFramePropsRO(f);
          EXPECT_EQ(env->propGetInt(p, "V12Threads", 0, nullptr), threads && mode == MT_SERIALIZED ? 1 : threads);
          EXPECT_EQ(env->propGetInt(p, "V12Notifications", 0, nullptr), threads ? 1 : 0);
        }
      }
}
}
