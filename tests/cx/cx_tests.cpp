#include <gtest/gtest.h>

#include <avisynth.h>

#include "support/avisynth_environment.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <future>
#include <chrono>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace avsut::test {
namespace {

const char *DualPluginPath() {
  const char *override_path = std::getenv("AVS_CX_SMOKE_DUAL_PATH");
  return override_path == nullptr || *override_path == '\0' ? CX_SMOKE_DUAL_PATH : override_path;
}

std::string LoadPlugin(IScriptEnvironment *environment, const char *path) {
  AVSValue result;
  if (!static_cast<IScriptEnvironment2 *>(environment)->LoadPlugin(path, true, &result)) {
    return {};
  }
  return result.AsString();
}

PClip CreateY8Clip(IScriptEnvironment *environment, int width, int height) {
  const AVSValue arguments[] = {width, height, "Y8"};
  const char *names[] = {"width", "height", "pixel_type"};
  return environment->Invoke("BlankClip", AVSValue(arguments, 3), names).AsClip();
}

PClip CreateChecker(IScriptEnvironment *environment, PClip source, int block_size) {
  const AVSValue arguments[] = {source, block_size};
  return environment->Invoke("CXCheckerInvert", AVSValue(arguments, 2)).AsClip();
}

std::vector<std::uint8_t> CopyPlane(const PVideoFrame &frame) {
  const int row_size = frame->GetRowSize(PLANAR_Y);
  const int height = frame->GetHeight(PLANAR_Y);
  const int pitch = frame->GetPitch(PLANAR_Y);
  const std::uint8_t *source = frame->GetReadPtr(PLANAR_Y);
  std::vector<std::uint8_t> result(static_cast<size_t>(row_size) * height);
  for (int y = 0; y < height; ++y) {
    std::memcpy(result.data() + static_cast<size_t>(y) * row_size,
                source + static_cast<std::ptrdiff_t>(y) * pitch, static_cast<size_t>(row_size));
  }
  return result;
}

std::vector<std::uint8_t> RenderChecker(const char *plugin_path, int width, int height,
                                        int block_size) {
  AviSynthEnvironment environment;
  LoadPlugin(environment.get(), plugin_path);
  const PClip source = CreateY8Clip(environment.get(), width, height);
  const PClip checker = CreateChecker(environment.get(), source, block_size);
  return CopyPlane(checker->GetFrame(0, environment.get()));
}

TEST(CxPluginLoading, LoadsDualEntryPlugin) {
  AviSynthEnvironment environment;
  EXPECT_EQ(LoadPlugin(environment.get(), DualPluginPath()),
            "AviSynth CX smoke plugin (legacy Init3)");
  EXPECT_TRUE(environment.get()->FunctionExists("CXSdkOnly"));
}

TEST(CxPluginLoading, FallsBackToUnchangedInit3Path) {
  AviSynthEnvironment environment;
  EXPECT_EQ(LoadPlugin(environment.get(), CX_SMOKE_LEGACY_PATH),
            "AviSynth CX smoke plugin (legacy Init3)");
}

TEST(CxCompatibility, PreservesRegistrationTypesIdentityAndWriteProbe) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    auto *env = environment.get();
    LoadPlugin(env, path); // The plugin invokes a just-registered function in Init.
    EXPECT_TRUE(env->Invoke("CXRegisterLate", AVSValue(nullptr, 0)).AsBool());
    EXPECT_EQ(env->Invoke("CXRegisteredLate", AVSValue(nullptr, 0)).AsInt(), 42);
    for (const AVSValue &value : {AVSValue(17), AVSValue(int64_t(17)),
                                 AVSValue(float(1.25)), AVSValue(double(1.25))}) {
      EXPECT_EQ(env->Invoke("CXValueType", AVSValue(&value, 1)).AsInt(), int(value.GetType()));
      EXPECT_EQ(env->Invoke("CXEcho", AVSValue(&value, 1)).GetType(), value.GetType());
    }
    const PClip source = CreateY8Clip(env, 64, 32);
    const AVSValue args[] = {source, source};
    EXPECT_TRUE(env->Invoke("CXSameClip", AVSValue(args, 2)).AsBool());
    const PVideoFrame retained = source->GetFrame(0, env);
    const AVSValue arg(source);
    EXPECT_TRUE(env->Invoke("CXWriteProbe", AVSValue(&arg, 1)).AsBool());
  }
}

TEST(CxCompatibility, AlternatingCompilerChain) {
  const char *other = std::getenv("AVS_CX_OTHER_DUAL_PATH");
  if (!other || !*other) GTEST_SKIP() << "Run run_matrix.ps1 to supply the other compiler DLL";
  AviSynthEnvironment environment;
  auto *env = environment.get();
  LoadPlugin(env, CX_SMOKE_DUAL_PATH);
  LoadPlugin(env, other);
  ASSERT_TRUE(env->FunctionExists("CXMsvcCheckerInvert"));
  ASSERT_TRUE(env->FunctionExists("CXGccCheckerInvert"));
  const PClip original = CreateY8Clip(env, 70, 52);
  PClip chain = original;
  for (int i = 0; i < 6; ++i) {
    const AVSValue args[] = {chain, 16};
    chain = env->Invoke(i % 2 ? "CXGccCheckerInvert" : "CXMsvcCheckerInvert",
                        AVSValue(args, 2)).AsClip();
  }
  const auto expected = CopyPlane(original->GetFrame(0, env));
  std::vector<std::future<std::vector<uint8_t>>> jobs;
  for (int i = 0; i < 4; ++i)
    jobs.push_back(std::async(std::launch::async, [&, i] {
      return CopyPlane(chain->GetFrame(i, env));
    }));
  for (auto &job : jobs) EXPECT_EQ(job.get(), expected);
}

class BoundarySource final : public IClip {
  VideoInfo vi_{};
  std::vector<PVideoFrame> frames_;
  int base_;
public:
  BoundarySource(IScriptEnvironment *env, int base) : base_(base) {
    vi_.width = 70; vi_.height = 33; vi_.pixel_type = VideoInfo::CS_Y8;
    vi_.num_frames = 3; vi_.SetFPS(24, 1);
    vi_.sample_type = SAMPLE_INT32; vi_.nchannels = 2;
    vi_.audio_samples_per_second = 48000; vi_.num_audio_samples = 6000;
    for (int n = 0; n < 3; ++n) {
      PVideoFrame frame = env->NewVideoFrame(vi_);
      for (int y = 0; y < frame->GetHeight(); ++y)
        std::memset(frame->GetWritePtr() + y * frame->GetPitch(), base + n, frame->GetRowSize());
      frames_.push_back(frame);
    }
  }
  const VideoInfo &__stdcall GetVideoInfo() override { return vi_; }
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *) override { return frames_.at(n); }
  void __stdcall GetAudio(void *buffer, int64_t start, int64_t count, IScriptEnvironment *) override {
    for (int64_t i = 0; i < count * 2; ++i)
      static_cast<int *>(buffer)[i] = base_ * 100 + static_cast<int>(start * 2 + i);
  }
  bool __stdcall GetParity(int) override { return false; }
  int __stdcall SetCacheHints(int hint, int) override {
    return hint == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
  }
};

TEST(CxBoundaries, MultipleUpstreamsAndRetainedReadPointers) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    auto *env = environment.get();
    LoadPlugin(env, path);
    const AVSValue args[] = {new BoundarySource(env, 10), new BoundarySource(env, 40)};
    EXPECT_TRUE(env->Invoke("CXMultipleFrames", AVSValue(args, 2)).AsBool());
  }
}

TEST(CxBoundaries, RetainedFrameCopyOnWrite) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    auto *env = environment.get();
    LoadPlugin(env, path);
    const AVSValue arg(new BoundarySource(env, 10));
    EXPECT_TRUE(env->Invoke("CXRetainedCow", AVSValue(&arg, 1)).AsBool());
  }
}

TEST(CxBoundaries, TwoUpstreamStereoAudioRanges) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    auto *env = environment.get();
    LoadPlugin(env, path);
    const AVSValue args[] = {new BoundarySource(env, 10), new BoundarySource(env, 40)};
    EXPECT_TRUE(env->Invoke("CXMultipleAudio", AVSValue(args, 2)).AsBool());
  }
}

TEST(CxBoundaries, FramesSurviveAcrossCallbacksAndReleasedInputs) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    auto *env = environment.get();
    LoadPlugin(env, path);
    PClip held;
    {
      const AVSValue args[] = {new BoundarySource(env, 10), new BoundarySource(env, 40)};
      held = env->Invoke("CXHeldFrames", AVSValue(args, 2)).AsClip();
    } // The filter must retain both inputs itself.
    for (int n : {0, 2, 1, 0}) {
      const auto pixels = CopyPlane(held->GetFrame(n, env));
      EXPECT_EQ(pixels, std::vector<uint8_t>(70 * 33, 10));
    }
  }
}

TEST(CxBoundaries, PluginThreadUsesSavedFactoryEnvironment) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    auto *env = environment.get();
    LoadPlugin(env, path);
    const PClip source = new BoundarySource(env, 10);
    const AVSValue arg(source);
    const PClip clip = env->Invoke("CXThreadFrame", AVSValue(&arg, 1)).AsClip();
    for (int n = 0; n < 3; ++n) {
      auto expected = std::vector<uint8_t>(70 * 33, 10 + n);
      expected[0] = 90 + n;
      EXPECT_EQ(CopyPlane(clip->GetFrame(n, env)), expected);
      EXPECT_EQ(CopyPlane(source->GetFrame(n, env)), std::vector<uint8_t>(70 * 33, 10 + n));
    }
  }
}

TEST(CxBoundaries, RepeatedHostFramePreservesPluginObjectIdentity) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    auto *env = environment.get();
    LoadPlugin(env, path);
    const AVSValue arg(new BoundarySource(env, 10));
    EXPECT_TRUE(env->Invoke("CXSameFrame", AVSValue(&arg, 1)).AsBool());
  }
}

TEST(CxBoundaries, ConcurrentFrameImportsAndLastRelease) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    auto *env = environment.get();
    LoadPlugin(env, path);
    const AVSValue arg(new BoundarySource(env, 10));
    EXPECT_TRUE(env->Invoke("CXConcurrentFrameIdentity", AVSValue(&arg, 1)).AsBool());
  }
}

TEST(CxBoundaries, NewSubframeAndCowIdentitySurviveHostRoundTrip) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    auto *env = environment.get();
    LoadPlugin(env, path);
    const AVSValue arg(new BoundarySource(env, 10));
    EXPECT_TRUE(env->Invoke("CXFrameRoundTripIdentity", AVSValue(&arg, 1)).AsBool());
  }
}

TEST(CxBoundaries, BatchRegistrationsKeepCallbacksAndUserData) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment first, second;
    auto *a = first.get();
    auto *b = second.get();
    for (auto *env : {a, b}) {
      static_cast<IScriptEnvironment2 *>(env)->ClearAutoloadDirs();
      LoadPlugin(env, path);
    }
    for (int batch = 0; batch < 2; ++batch) {
      for (auto *env : {a, b}) {
        const AVSValue args[] = {batch * 1024, 1024, env == a ? 0 : 10000};
        ASSERT_TRUE(env->Invoke("CXRegisterBatch", AVSValue(args, 3)).AsBool());
      }
      // Recheck earlier bindings after growth, interleaving the two sessions.
      for (int i = 0; i < (batch + 1) * 1024; ++i) {
        const std::string name = "CXBatchBinding" + std::to_string(i);
        for (auto *env : {a, b}) {
          const int value = i + (env == a ? 0 : 10000);
          ASSERT_EQ(env->Invoke(name.c_str(), AVSValue(nullptr, 0)).AsInt(),
                    i % 2 ? -1 - value : value) << name;
        }
      }
    }
  }
}

TEST(CxBoundaries, BinarySaveStringDistinguishesHashCollisions) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    LoadPlugin(environment.get(), path);
    EXPECT_TRUE(environment.get()->Invoke("CXBinaryStrings", AVSValue(nullptr, 0)).AsBool());
  }
}

struct RegistrationGate {
  std::promise<void> entered, release;
};
AVSValue __cdecl HoldRegistrationGate(AVSValue, void *data, IScriptEnvironment *) {
  auto *gate = static_cast<RegistrationGate *>(data);
  gate->entered.set_value();
  gate->release.get_future().wait();
  return true;
}

TEST(CxCompatibility, FrameRegistrationWaitsForHostPluginLock) {
  for (const char *path : {CX_SMOKE_LEGACY_PATH, DualPluginPath()}) {
    SCOPED_TRACE(path);
    AviSynthEnvironment environment;
    auto *env = environment.get();
    LoadPlugin(env, path);
    RegistrationGate gate;
    env->AddFunction("CXHoldRegistrationGate", "", HoldRegistrationGate, &gate);
    const AVSValue source(CreateY8Clip(env, 64, 32));
    const PClip clip = env->Invoke("CXRegistrationFrame", AVSValue(&source, 1)).AsClip();
    auto loader = std::async(std::launch::async, [&] {
      LoadPlugin(env, CX_REGISTRATION_GATE_PATH);
    });
    gate.entered.get_future().wait(); // Loader now holds the host plugin mutex.
    std::promise<void> attempting;
    auto started = attempting.get_future();
    auto frame = std::async(std::launch::async, [&] {
      attempting.set_value();
      return clip->GetFrame(0, env);
    });
    started.wait();
    const auto status = frame.wait_for(std::chrono::milliseconds(200));
    gate.release.set_value(); // Always release before any potentially fatal check.
    loader.get();
    EXPECT_EQ(status, std::future_status::timeout);
    EXPECT_TRUE(frame.get());
    std::vector<std::future<PVideoFrame>> frames;
    for (int i = 1; i <= 8; ++i)
      frames.push_back(std::async(std::launch::async, [&, i] { return clip->GetFrame(i, env); }));
    for (auto &job : frames) EXPECT_TRUE(job.get());
    for (int i = 0; i <= 8; ++i)
      EXPECT_EQ(env->Invoke(("CXFromFrame" + std::to_string(i)).c_str(), AVSValue(nullptr, 0)).AsInt(), 42);
  }
}

AVSValue __cdecl LoadRegistrationDependency(AVSValue, void *data, IScriptEnvironment *env) {
  return LoadPlugin(env, static_cast<const char *>(data)).empty() ? AVSValue(false) : AVSValue(true);
}
TEST(CxCompatibility, NestedLoadsRestoreOuterPluginName) {
  AviSynthEnvironment environment;
  auto *env = environment.get();
  env->AddFunction("CXLoadNestedDependency", "", LoadRegistrationDependency,
                   const_cast<char *>(CX_REGISTRATION_DEPENDENCY_PATH));
  env->AddFunction("CXLoadMissingDependency", "", LoadRegistrationDependency,
                   const_cast<char *>(CX_REGISTRATION_DEPENDENCY_PATH ".missing"));
  env->AddFunction("CXLoadThrowingDependency", "", LoadRegistrationDependency,
                   const_cast<char *>(CX_REGISTRATION_THROW_PATH));
  const char *path = std::getenv("AVS_CX_NESTED_PATH");
  LoadPlugin(env, path && *path ? path : CX_NESTED_PATH);
  for (const char *name : {"CXBeforeNested", "CXAfterNested", "CXAfterFailedNested",
                           "CXAfterThrowingNested"}) {
    const std::string qualified = std::string("cx_smoke_nested_") + name;
    EXPECT_TRUE(env->FunctionExists(qualified.c_str()));
    EXPECT_EQ(env->Invoke(qualified.c_str(), AVSValue(nullptr, 0)).AsInt(), 42);
    EXPECT_FALSE(env->FunctionExists((std::string("_") + name).c_str()));
  }
}

TEST(CxPluginLoading, DoesNotEnterLegacyAbiAfterCxInitializationFails) {
  AviSynthEnvironment environment;
  EXPECT_THROW({ LoadPlugin(environment.get(), CX_SMOKE_REJECT_PATH); }, AvisynthError);
  EXPECT_FALSE(environment.get()->FunctionExists("CXRejectedStagedFunction"));
}

TEST(CxCheckerInvert, NativeCxAndLegacyOutputsMatch) {
  constexpr int width = 70;
  constexpr int height = 51;
  constexpr int block_size = 16;
  EXPECT_EQ(RenderChecker(DualPluginPath(), width, height, block_size),
            RenderChecker(CX_SMOKE_LEGACY_PATH, width, height, block_size));
}

TEST(CxCheckerInvert, InvertsAlternatingBlocksAndPreservesSource) {
  AviSynthEnvironment environment;
  LoadPlugin(environment.get(), DualPluginPath());
  const PClip source = CreateY8Clip(environment.get(), 70, 51);
  const PVideoFrame source_before = source->GetFrame(0, environment.get());
  const std::vector<std::uint8_t> original = CopyPlane(source_before);

  const PClip checker = CreateChecker(environment.get(), source, 16);
  const std::vector<std::uint8_t> output = CopyPlane(checker->GetFrame(0, environment.get()));
  const std::vector<std::uint8_t> source_after = CopyPlane(source->GetFrame(0, environment.get()));

  ASSERT_EQ(original, source_after);
  for (int y = 0; y < 51; ++y) {
    for (int x = 0; x < 70; ++x) {
      const size_t index = static_cast<size_t>(y) * 70 + x;
      const bool invert = (((x / 16) + (y / 16)) & 1) != 0;
      const std::uint8_t expected =
          invert ? static_cast<std::uint8_t>(~original[index]) : original[index];
      ASSERT_EQ(output[index], expected) << "x=" << x << " y=" << y;
    }
  }
}

TEST(CxSmokePassThrough, KeepsTheHostFrameWithoutCopyingPixels) {
  AviSynthEnvironment environment;
  LoadPlugin(environment.get(), DualPluginPath());
  const PClip source = CreateY8Clip(environment.get(), 64, 64);
  const AVSValue argument(source);
  const PClip pass =
      environment.get()->Invoke("CXSmokePassThrough", AVSValue(&argument, 1)).AsClip();
  const PVideoFrame source_frame = source->GetFrame(0, environment.get());
  const PVideoFrame output_frame = pass->GetFrame(0, environment.get());
  EXPECT_EQ(source_frame.operator->(), output_frame.operator->());
}

TEST(CxCheckerInvert, RejectsNonPositiveBlockSize) {
  AviSynthEnvironment environment;
  LoadPlugin(environment.get(), DualPluginPath());
  const PClip source = CreateY8Clip(environment.get(), 64, 64);
  EXPECT_THROW({ CreateChecker(environment.get(), source, 0); }, AvisynthError);
}

const char *SdkPath() {
  const char *path = std::getenv("AVS_CX_SDK_PATH");
  return path && *path ? path : CX_SMOKE_SDK_PATH;
}

TEST(CxLegacySdk, UnchangedFilterMatchesLegacy) {
  EXPECT_EQ(RenderChecker(SdkPath(), 70, 51, 16), RenderChecker(CX_SMOKE_LEGACY_PATH, 70, 51, 16));
}

TEST(CxLegacySdk, KeepsHostFrameIdentityAndContainsExceptions) {
  AviSynthEnvironment environment;
  LoadPlugin(environment.get(), SdkPath());
  const PClip source = CreateY8Clip(environment.get(), 64, 64);
  const AVSValue arg(source);
  const PClip pass = environment.get()->Invoke("CXSmokePassThrough", AVSValue(&arg, 1)).AsClip();
  const PVideoFrame original = source->GetFrame(0, environment.get());
  const PVideoFrame output = pass->GetFrame(0, environment.get());
  EXPECT_EQ(original.operator->(), output.operator->());
  EXPECT_THROW(CreateChecker(environment.get(), source, 0), AvisynthError);
  const PClip checker = CreateChecker(environment.get(), source, 16);
  const auto before = CopyPlane(original);
  checker->GetFrame(0, environment.get());
  EXPECT_EQ(before, CopyPlane(source->GetFrame(0, environment.get())));
}

TEST(CxLegacySdk, PureCPlugin) {
  AviSynthEnvironment environment;
  const char *path = std::getenv("AVS_CX_C_PATH");
  try {
    LoadPlugin(environment.get(), path && *path ? path : CX_SMOKE_C_PATH);
    ASSERT_TRUE(environment.get()->FunctionExists("CXPureCAnswer"));
    EXPECT_EQ(environment.get()->Invoke("CXPureCAnswer", AVSValue(nullptr, 0)).AsInt(), 4242);
  } catch (const AvisynthError &e) {
    FAIL() << e.msg;
  }
}

TEST(CxLegacySdk, EnvironmentServicesAndLocalCopyOnWrite) {
  AviSynthEnvironment environment;
  LoadPlugin(environment.get(), SdkPath());
  const AVSValue source(CreateY8Clip(environment.get(), 70, 52));
  EXPECT_TRUE(environment.get()->Invoke("CXCow", AVSValue(&source, 1)).AsBool());
  const PClip clip = environment.get()->Invoke("CXServices", AVSValue(&source, 1)).AsClip();
  const PVideoFrame frame = clip->GetFrame(3, environment.get());
  int error = 0;
  EXPECT_EQ(environment.get()->propGetInt(environment.get()->getFramePropsRO(frame), "SdkFrame", 0,
                                          &error),
            3);
  EXPECT_EQ(error, 0);
  EXPECT_THROW(clip->GetFrame(7, environment.get()), AvisynthError);
  // A plugin exception must not poison later calls.
  EXPECT_TRUE(clip->GetFrame(4, environment.get()));
  const AVSValue elements[] = {AVSValue(int64_t(1) << 45), "borrowed string", 3.25, true};
  const AVSValue array(elements, 4);
  const AVSValue result = environment.get()->Invoke("CXEcho", AVSValue(&array, 1));
  EXPECT_EQ(result.ArraySize(), 4);
  EXPECT_EQ(result[0].AsLong(), int64_t(1) << 45);
  EXPECT_STREQ(result[1].AsString(), "borrowed string");
  EXPECT_EQ(result[2].AsFloat(), 3.25);
  EXPECT_TRUE(result[3].AsBool());
}

TEST(CxLegacySdk, ConcurrentFrameCallsUseTheirOwnEnvironment) {
  AviSynthEnvironment environment;
  LoadPlugin(environment.get(), SdkPath());
  const PClip source = CreateY8Clip(environment.get(), 70, 52);
  const PClip clip = CreateChecker(environment.get(), source, 16);
  const auto expected = CopyPlane(clip->GetFrame(0, environment.get()));
  std::vector<std::future<std::vector<uint8_t>>> jobs;
  for (int i = 0; i < 8; ++i)
    jobs.push_back(std::async(std::launch::async, [&, i] {
      return CopyPlane(clip->GetFrame(i + 1, environment.get()));
    }));
  for (auto &job : jobs)
    EXPECT_EQ(job.get(), expected);
}

#if defined(_WIN32) && defined(_MSC_VER)
TEST(CxLegacySdk, DualBinaryStillWorksThroughOldMsvcEntry) {
  // Deliberately emulate a pre-CX core: resolve and call only Init3 from the
  // very same DLL that includes all SDK implementation code.
  HMODULE module = LoadLibraryA(CX_SMOKE_DUAL_PATH);
  ASSERT_NE(module, nullptr);
  {
    AviSynthEnvironment environment;
    using Init = const char *(__stdcall *)(IScriptEnvironment *, const AVS_Linkage *);
    auto init = reinterpret_cast<Init>(GetProcAddress(module, "AvisynthPluginInit3"));
    ASSERT_NE(init, nullptr);
    init(environment.get(), environment.get()->GetAVSLinkage());
    EXPECT_FALSE(environment.get()->FunctionExists("CXSdkOnly"));
    auto source = CreateY8Clip(environment.get(), 70, 51);
    auto checker = CreateChecker(environment.get(), source, 16);
    EXPECT_EQ(CopyPlane(checker->GetFrame(0, environment.get())),
              RenderChecker(CX_SMOKE_LEGACY_PATH, 70, 51, 16));
  }
  FreeLibrary(module);
}
#endif

TEST(CxLegacySdk, UnmodifiedConvertStackedRoundTripsPlanar16Bit) {
  AviSynthEnvironment environment;
  const char *path = std::getenv("AVS_CX_STACKED_PATH");
  LoadPlugin(environment.get(), path && *path ? path : CX_STACKED_PATH);
  for (const char *format : {"Y16", "YUV420P16", "YUV422P16", "YUV444P16"}) {
    const AVSValue args[] = {70, 52, format};
    const char *names[] = {"width", "height", "pixel_type"};
    const PClip original =
        environment.get()->Invoke("BlankClip", AVSValue(args, 3), names).AsClip();
    const AVSValue source(original);
    for (const char *to : {"ConvertToStacked", "ConvertToDoubleWidth"}) {
      const AVSValue stacked = environment.get()->Invoke(to, AVSValue(&source, 1));
      const char *from = std::strcmp(to, "ConvertToStacked") == 0 ? "ConvertFromStacked"
                                                                  : "ConvertFromDoubleWidth";
      const PClip roundtrip = environment.get()->Invoke(from, AVSValue(&stacked, 1)).AsClip();
      EXPECT_EQ(roundtrip->GetVideoInfo().pixel_type, original->GetVideoInfo().pixel_type);
      const PVideoFrame a = original->GetFrame(0, environment.get());
      const PVideoFrame b = roundtrip->GetFrame(0, environment.get());
      for (int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
        if (original->GetVideoInfo().IsY() && plane != PLANAR_Y)
          continue;
        for (int y = 0; y < a->GetHeight(plane); ++y)
          EXPECT_EQ(0, std::memcmp(a->GetReadPtr(plane) + y * a->GetPitch(plane),
                                   b->GetReadPtr(plane) + y * b->GetPitch(plane),
                                   a->GetRowSize(plane)));
      }
    }
  }
}

} // namespace
} // namespace avsut::test
