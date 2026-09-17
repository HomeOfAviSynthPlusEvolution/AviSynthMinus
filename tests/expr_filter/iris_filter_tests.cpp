#include "support/video_filter_test_support.h"
#include <algorithm>
#include <atomic>
#ifndef AVS_TEST_IRIS_SHARED
#include <avs_iris/expr.h>
#endif
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
using avsut::test::AviSynthEnvironment;
thread_local IScriptEnvironment *expected_environment = nullptr;
struct Activity {
  std::atomic<int> active{0}, peak{0}, snapshots{0};
};
class Source final : public IClip {
  VideoInfo vi_;
  Activity &activity_;
  bool fail_;

public:
  Source(Activity &activity, bool fail = false)
      : activity_(activity), fail_(fail) {
    vi_ = avsut::test::make_video_info({66, 34, VideoInfo::CS_YV12, 32, 24, 1});
  }
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env) override {
    if (expected_environment)
      EXPECT_EQ(env, expected_environment);
    struct Guard {
      Activity &a;
      explicit Guard(Activity &v) : a(v) {
        const int active = ++a.active;
        int peak = a.peak;
        while (peak < active && !a.peak.compare_exchange_weak(peak, active)) {
        }
      }
      ~Guard() { --a.active; }
    } guard(activity_);
    if (n == 0)
      ++activity_.snapshots;
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    if (fail_ && n == 13)
      env->ThrowError("Iris test upstream frame 13");
    auto f = env->NewVideoFrame(vi_);
    for (int p = 0; p < 3; ++p) {
      int id = p == 0 ? PLANAR_Y : p == 1 ? PLANAR_U : PLANAR_V;
      for (int y = 0; y < f->GetHeight(id); ++y)
        for (int x = 0; x < f->GetRowSize(id); ++x)
          f->GetWritePtr(id)[y * f->GetPitch(id) + x] =
              uint8_t(3 + p * 7 + x + y);
    }
    env->propSetInt(env->getFramePropsRW(f), "Gain", n + 1, 0);
    return f;
  }
  void __stdcall GetAudio(void *, int64_t, int64_t,
                          IScriptEnvironment *) override {}
  bool __stdcall GetParity(int) override { return false; }
  int __stdcall SetCacheHints(int hint, int) override {
    return hint == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
  }
  const VideoInfo &__stdcall GetVideoInfo() override { return vi_; }
  int __stdcall GetVersion() override { return AVISYNTH_INTERFACE_VERSION; }
};
const std::vector<const char *> backends = {"scalar"
#ifdef AVS_TEST_IRIS_LLVM
                                            ,
                                            "llvm"
#endif
#ifdef AVS_TEST_IRIS_SLEEF
                                            ,
                                            "sleef", "sleef-fast"
#endif
};
PClip make_filter(IScriptEnvironment *env, PClip source, int mode,
                  const char *backend, bool builtin = false) {
  AVSValue clips[] = {source, source};
  const char *expression = mode == 0   ? "x[-1,0] x.Gain + frameno +"
                           : mode == 1 ? "x x.Gain +"
                           : mode == 2 ? ""
                                       : "x y + x.Gain +";
  if (builtin) {
    std::vector<AVSValue> values{source};
    std::vector<const char *> names{nullptr};
    if (mode == 3) {
      values.emplace_back(source);
      names.push_back(nullptr);
    }
    values.emplace_back(expression);
    names.push_back(nullptr);
    values.emplace_back(backend);
    names.push_back("backend");
    values.emplace_back(mode == 3 ? 2 : mode == 1 ? 1 : 0);
    names.push_back("lut");
    return env
        ->Invoke("IrisExpr", AVSValue(values.data(), int(values.size())),
                 names.data())
        .AsClip();
  }
#ifndef AVS_TEST_IRIS_SHARED
  AVSValue expressions[] = {expression};
  AVSValue args[] = {AVSValue(clips, mode == 3 ? 2 : 1),
                     AVSValue(expressions, 1),
                     AVSValue(),
                     backend,
                     AVSValue(),
                     AVSValue(),
                     AVSValue(),
                     true,
                     mode == 3   ? 2
                     : mode == 1 ? 1
                                 : 0,
                     256};
  return CreateIrisExpr(AVSValue(args, 10), nullptr, env).AsClip();
#else
  throw std::logic_error(
      "shared tests must use the registered IrisExpr factory");
#endif
}
void verify(PClip clip, int n, int mode, IScriptEnvironment *env) {
  auto frame = clip->GetFrame(n, env);
  int error = 0;
  EXPECT_EQ(env->propGetInt(env->getFramePropsRO(frame), "Gain", 0, &error),
            n + 1);
  EXPECT_EQ(error, 0);
  for (int p = 0; p < 3; ++p) {
    int id = p == 0 ? PLANAR_Y : p == 1 ? PLANAR_U : PLANAR_V;
    for (int y = 0; y < frame->GetHeight(id); ++y)
      for (int x = 0; x < frame->GetRowSize(id); ++x) {
        int base = 3 + p * 7 + (mode == 0 ? std::max(x - 1, 0) : x) + y;
        int expected = mode == 0   ? base + 2 * n + 1
                       : mode == 1 ? base + 1
                       : mode == 3 ? base * 2 + 1
                                   : base;
        ASSERT_EQ(frame->GetReadPtr(id)[y * frame->GetPitch(id) + x],
                  std::min(expected, 255));
      }
  }
}
#ifndef AVS_TEST_IRIS_SHARED
TEST(IrisNative, OneInstanceConcurrentRequestsAndErrors) {
  for (const char *backend : backends)
    for (int mode = 0; mode < 4; ++mode) {
      SCOPED_TRACE(std::string(backend) + " mode=" + std::to_string(mode));
      AviSynthEnvironment owner;
      Activity activity;
      PClip source = new Source(activity, true);
      PClip clip = make_filter(owner.get(), source, mode, backend);
      EXPECT_EQ(clip->SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
      int snapshots = activity.snapshots;
      EXPECT_EQ(snapshots, mode == 1 || mode == 3 ? 1 : 0);
      std::vector<std::unique_ptr<AviSynthEnvironment>> environments;
      for (int i = 0; i < 8; ++i)
        environments.emplace_back(new AviSynthEnvironment);
      std::vector<std::thread> threads;
      for (int t = 0; t < 8; ++t)
        threads.emplace_back([&, t] {
          auto *env = environments[t]->get();
          expected_environment = env;
          for (int k = 0; k < 12; ++k) {
            int n = 1 + (k * 13 + t * 7) % 31;
            try {
              verify(clip, n, mode, env);
              EXPECT_NE(n, 13);
            } catch (const AvisynthError &e) {
              EXPECT_EQ(n, 13);
              EXPECT_STREQ(e.msg, "Iris test upstream frame 13");
            } catch (...) {
              ADD_FAILURE() << "unexpected frame exception";
            }
          }
        });
      for (auto &t : threads)
        t.join();
      EXPECT_GE(activity.peak.load(), 2);
      EXPECT_EQ(activity.active.load(), 0);
      EXPECT_EQ(activity.snapshots.load(), snapshots);
      verify(clip, 7, mode, owner.get());
    }
}
#endif
TEST(IrisNative, PrefetchUnorderedFrames) {
  for (const char *backend : backends)
    for (int workers : {1, 2, 4, 8})
      for (int mode = 0; mode < 4; ++mode) {
        SCOPED_TRACE(std::string(backend) + " workers=" +
                     std::to_string(workers) + " mode=" + std::to_string(mode));
        AviSynthEnvironment environment;
        Activity activity;
        PClip source = new Source(activity);
        PClip clip =
            make_filter(environment.get(), source, mode, backend, true);
        AVSValue args[] = {clip, workers};
        PClip prefetch =
            environment.get()->Invoke("Prefetch", AVSValue(args, 2)).AsClip();
        for (int i = 0; i < 32; ++i)
          verify(prefetch, (i * 13) % 32, mode, environment.get());
        if (workers > 1)
          EXPECT_GE(activity.peak.load(), 2);
        prefetch = nullptr;
        EXPECT_EQ(activity.active.load(), 0);
      }
}
TEST(IrisNative, BuiltinRegistrationAndConstructionErrors) {
  AviSynthEnvironment environment;
  auto *env = environment.get();
  EXPECT_TRUE(env->FunctionExists("IrisExpr"));
  auto clip = env->Invoke("Eval", "IrisExpr(BlankClip(width=66,height=34,pixel_"
                                  "type=\"YV12\"), \"32\")")
                  .AsClip();
  EXPECT_EQ(clip->GetFrame(0, env)->GetReadPtr()[0], 32);
  for (const char *script :
       {"IrisExpr(BlankClip(pixel_type=\"Y8\"), \"\", format=\"Y16\")",
        "IrisExpr(BlankClip(pixel_type=\"Y8\"), \"sx\", lut=1)",
        "IrisExpr(BlankClip(pixel_type=\"Y8\"), \"x\", lut_max_mb=0)",
        "IrisExpr(BlankClip(pixel_type=\"Y8\"), \"x\", backend=\"invalid\")"})
    EXPECT_THROW(env->Invoke("Eval", script), AvisynthError);
  try {
    env->Invoke("Eval", "a=BlankClip(width=2,height=2,pixel_type=\"Y14\")"
                        "\nIrisExpr(a,a,\"x y +\",lut=2)");
    FAIL() << "oversized LUT accepted";
  } catch (const AvisynthError &e) {
    EXPECT_NE(std::string(e.msg).find("lut_max_mb=-1"), std::string::npos);
  }
}

TEST(IrisNative, ExprAndIrisExprUseIrisBackendOptions) {
  AviSynthEnvironment environment;
  auto *env = environment.get();
  for (const char *name : {"Expr", "IrisExpr"}) {
    ASSERT_TRUE(env->FunctionExists(name));
    for (const char *backend : backends) {
      const std::string script = std::string(name) +
                                 "(BlankClip(width=4,height=2,pixel_type="
                                 "\"Y8\"), \"2 3 +\", backend=\"" +
                                 backend + "\", lut=1, lut_max_mb=1)";
      SCOPED_TRACE(script);
      auto clip = env->Invoke("Eval", script.c_str()).AsClip();
      EXPECT_EQ(clip->GetFrame(0, env)->GetReadPtr()[0], 5);
    }
  }
}

TEST(IrisNative, ExprAcceptsIgnoredLegacyFlagsAndPositionalArguments) {
  AviSynthEnvironment environment;
  auto *env = environment.get();
  const std::string prefix =
      "Expr(BlankClip(width=4,height=2,pixel_type=\"Y8\"), \"300\", ";
  for (int flags = 0; flags < 16; ++flags) {
    const std::string avx = flags & 1 ? "true" : "false";
    const std::string single = flags & 2 ? "true" : "false";
    const std::string sse = flags & 4 ? "true" : "false";
    const std::string vector = flags & 8 ? "true" : "false";
    const std::string named = prefix + "format=\"Y16\", optAvx2=" + avx +
                              ", optSingleMode=" + single + ", optSSE2=" + sse +
                              ", scale_inputs=\"none\", clamp_float=false, "
                              "clamp_float_UV=false, lut=1, optVectorC=" +
                              vector;
    std::vector<std::string> scripts{named + ")"};
    for (const char *backend : backends)
      scripts.push_back(named + ", backend=\"" + backend +
                        "\", optimize=false, lut_max_mb=1)");
    for (const auto &script : scripts) {
      SCOPED_TRACE(script);
      auto clip = env->Invoke("Eval", script.c_str()).AsClip();
      EXPECT_EQ(clip->GetVideoInfo().pixel_type, VideoInfo::CS_Y16);
      auto frame = clip->GetFrame(0, env);
      EXPECT_EQ(reinterpret_cast<const uint16_t *>(frame->GetReadPtr())[0],
                300);
    }

    // An explicit expression array delimits the variadic string argument.
    AVSValue source =
        env->Invoke("Eval", "BlankClip(width=4,height=2,pixel_type=\"Y8\")");
    AVSValue sources[] = {source};
    AVSValue expressions[] = {"300"};
    AVSValue positional[] = {AVSValue(sources, 1),
                             AVSValue(expressions, 1),
                             "Y16",
                             bool(flags & 1),
                             bool(flags & 2),
                             bool(flags & 4),
                             "none",
                             false,
                             false,
                             1,
                             bool(flags & 8),
                             "scalar",
                             false,
                             1};
    for (int count : {11, 14}) {
      SCOPED_TRACE(::testing::Message()
                   << "flags=" << flags << ", args=" << count);
      auto clip = env->Invoke("Expr", AVSValue(positional, count)).AsClip();
      EXPECT_EQ(clip->GetVideoInfo().pixel_type, VideoInfo::CS_Y16);
      auto frame = clip->GetFrame(0, env);
      EXPECT_EQ(reinterpret_cast<const uint16_t *>(frame->GetReadPtr())[0],
                300);
    }
  }
  for (const char *script :
       {"Expr(BlankClip(pixel_type=\"Y8\"), \"x\", optAvx2=1)",
        "Expr(BlankClip(pixel_type=\"Y8\"), \"x\", optSSE2=false, "
        "backend=\"invalid\")",
        "Expr(BlankClip(pixel_type=\"Y8\"), \"x\", lut_max_mb=0)",
        "IrisExpr(BlankClip(pixel_type=\"Y8\"), \"x\", optAvx2=false)"})
    EXPECT_THROW(env->Invoke("Eval", script), AvisynthError);
}
} // namespace
