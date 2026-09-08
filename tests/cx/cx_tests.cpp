#include <gtest/gtest.h>

#include <avisynth.h>

#include "support/avisynth_environment.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <future>
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
