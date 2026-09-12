// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#include "support/video_filter_test_support.h"
#include <gtest/gtest.h>
#include <cstring>

namespace avsut::test {
namespace {
PClip Source(IScriptEnvironment* env, int type, int width, int height, int location = 0) {
  const auto vi = make_video_info({width, height, type, 1, 25, 1});
  PVideoFrame frame = env->NewVideoFrame(vi);
  if (vi.IsPlanar()) {
    for (int p : {PLANAR_Y, PLANAR_U, PLANAR_V})
      write_frame_plane<uint8_t>(frame, p, [p](int x, int y) { return uint8_t(31 + p * 13 + x * 29 + y * 47); });
  } else {
    write_frame_plane<uint8_t>(frame, DEFAULT_PLANE, [](int x, int y) { return uint8_t(13 + x * 37 + y * 53); });
  }
  auto* props = env->getFramePropsRW(frame);
  env->propSetInt(props, "_ChromaLocation", location, 0);
  env->propSetInt(props, "_ColorRange", vi.IsRGB() ? 0 : 1, 0);
  env->propSetInt(props, "Yuy2Marker", 321, 0);
  return new StaticFrameClip(vi, frame);
}
void EqualPixels(PClip a, PClip b, IScriptEnvironment* env) {
  ASSERT_EQ(a->GetVideoInfo().pixel_type, b->GetVideoInfo().pixel_type);
  const auto af = a->GetFrame(0, env), bf = b->GetFrame(0, env);
  const bool planar = a->GetVideoInfo().IsPlanar();
  const int planes[] = {planar ? PLANAR_Y : DEFAULT_PLANE, PLANAR_U, PLANAR_V};
  for (int i = 0; i < (planar ? 3 : 1); ++i) {
    const int p = planes[i];
    ASSERT_EQ(af->GetRowSize(p), bf->GetRowSize(p));
    ASSERT_EQ(af->GetHeight(p), bf->GetHeight(p));
    for (int y = 0; y < af->GetHeight(p); ++y)
      EXPECT_EQ(std::memcmp(af->GetReadPtr(p) + y * af->GetPitch(p), bf->GetReadPtr(p) + y * bf->GetPitch(p),
                            af->GetRowSize(p)),
                0);
  }
  EXPECT_EQ(env->propGetInt(env->getFramePropsRO(af), "Yuy2Marker", 0, nullptr), 321);
  EXPECT_NE(af->CheckMemory(), 1);
}
PClip Convert(IScriptEnvironment* env, const char* name, PClip clip, bool interlaced = false, const char* in = nullptr,
              const char* out = nullptr) {
  AVSValue args[4] = {clip, interlaced, in ? AVSValue(in) : AVSValue(), out ? AVSValue(out) : AVSValue()};
  const char* names[4] = {nullptr, "interlaced", "ChromaInPlacement", "ChromaOutPlacement"};
  return env->Invoke(name, AVSValue(args, 4), names).AsClip();
}
TEST(Yuy2API, LosslessPackingAndUnpackingAtShortWidths) {
  for (bool c : {false, true}) {
    AviSynthEnvironment e;
    auto* env = e.get();
    if (c)
      env->Invoke("SetMaxCPU", "none");
    for (int width : {2, 4, 6, 8, 10, 18, 34, 66}) {
      PClip src = Source(env, VideoInfo::CS_YV16, width, 3);
      PClip packed = env->Invoke("ConvertToYUY2", src).AsClip();
      auto f = packed->GetFrame(0, env), original = src->GetFrame(0, env);
      for (int y = 0; y < 3; ++y)
        for (int x = 0; x < width / 2; ++x) {
          auto* p = f->GetReadPtr() + y * f->GetPitch() + x * 4;
          EXPECT_EQ(p[0], original->GetReadPtr(PLANAR_Y)[y * original->GetPitch(PLANAR_Y) + x * 2]);
          EXPECT_EQ(p[2], original->GetReadPtr(PLANAR_Y)[y * original->GetPitch(PLANAR_Y) + x * 2 + 1]);
          EXPECT_EQ(p[1], original->GetReadPtr(PLANAR_U)[y * original->GetPitch(PLANAR_U) + x]);
          EXPECT_EQ(p[3], original->GetReadPtr(PLANAR_V)[y * original->GetPitch(PLANAR_V) + x]);
        }
      EqualPixels(src, env->Invoke("ConvertToYV16", packed).AsClip(), env);
      // Invoke may wrap an identity result in a distinct cache clip.
      EqualPixels(packed, env->Invoke("ConvertToYUY2", packed).AsClip(), env);
    }
  }
}
TEST(Yuy2API, GrayscaleDropsChromaLocationWithoutChangingSource) {
  for (bool c : {false, true}) {
    AviSynthEnvironment e;
    auto* env = e.get();
    if (c)
      env->Invoke("SetMaxCPU", "none");
    for (int width : {2, 6, 16, 18, 34}) {
      PClip source = Source(env, VideoInfo::CS_YUY2, width, 3, 1);
      auto original = source->GetFrame(0, env);
      const auto before = FrameSnapshot::capture(original, source->GetVideoInfo());
      auto output = env->Invoke("ConvertToY8", source).AsClip()->GetFrame(0, env);
      EXPECT_EQ(env->propNumElements(env->getFramePropsRO(output), "_ChromaLocation"), -1);
      EXPECT_EQ(env->propGetInt(env->getFramePropsRO(output), "_ColorRange", 0, nullptr), 1);
      EXPECT_EQ(env->propGetInt(env->getFramePropsRO(output), "Yuy2Marker", 0, nullptr), 321);
      EXPECT_EQ(env->propGetInt(env->getFramePropsRO(original), "_ChromaLocation", 0, nullptr), 1);
      for (int y = 0; y < 3; ++y)
        for (int x = 0; x < width; ++x)
          EXPECT_EQ(output->GetReadPtr()[y * output->GetPitch() + x],
                    original->GetReadPtr()[y * original->GetPitch() + 2 * x]);
      EXPECT_EQ(FrameSnapshot::capture(original, source->GetVideoInfo()), before);
    }
  }
}
TEST(Yuy2API, PropertiesAndExplicitPlacementSelectTheSameCoordinates) {
  for (bool c : {false, true}) {
    AviSynthEnvironment e;
    auto* env = e.get();
    if (c)
      env->Invoke("SetMaxCPU", "none");
    PClip center = Source(env, VideoInfo::CS_YUY2, 18, 8, 1);
    for (bool fields : {false, true}) {
      EqualPixels(Convert(env, "ConvertToYV12", center, fields),
                  Convert(env, "ConvertToYV12", center, fields, "center"), env);
      PClip left = Source(env, VideoInfo::CS_YUY2, 18, 8, 0);
      EqualPixels(Convert(env, "ConvertToYV12", center, fields, "left"), Convert(env, "ConvertToYV12", left, fields),
                  env);
      PClip planar = Convert(env, "ConvertToYV16", center, false, "center", "center");
      EqualPixels(Convert(env, "ConvertToYV12", center, fields), Convert(env, "ConvertToYV12", planar, fields), env);
      PClip yv12 = Source(env, VideoInfo::CS_YV12, 18, 8, 1);
      PClip output = Convert(env, "ConvertToYUY2", yv12, fields, nullptr, "center");
      auto f = output->GetFrame(0, env);
      EXPECT_EQ(env->propGetInt(env->getFramePropsRO(f), "_ChromaLocation", 0, nullptr), 1);
      EqualPixels(Convert(env, "ConvertToYV16", output, false, "center", "center"),
                  Convert(env, "ConvertToYV16", yv12, fields, nullptr, "center"), env);
    }
  }
}
TEST(Yuy2API, GreyscalePreservesLumaPropertiesAndSource) {
  for (bool c : {false, true}) {
    AviSynthEnvironment e;
    auto* env = e.get();
    if (c)
      env->Invoke("SetMaxCPU", "none");
    for (int width : {2, 6, 14, 16, 18, 30, 32, 34, 62, 64, 66, 130}) {
      PClip source = Source(env, VideoInfo::CS_YUY2, width, 3, 1);
      auto original = source->GetFrame(0, env);
      const auto before = FrameSnapshot::capture(original, source->GetVideoInfo());
      auto result = env->Invoke("Greyscale", source).AsClip();
      EXPECT_EQ(result->GetVideoInfo().pixel_type, VideoInfo::CS_YUY2);
      auto output = result->GetFrame(0, env);
      // Greyscale keeps YUY2 storage, so its chroma location remains applicable.
      EXPECT_EQ(env->propGetInt(env->getFramePropsRO(output), "_ChromaLocation", 0, nullptr), 1);
      EXPECT_EQ(env->propGetInt(env->getFramePropsRO(output), "_ColorRange", 0, nullptr), 1);
      EXPECT_EQ(env->propGetInt(env->getFramePropsRO(output), "Yuy2Marker", 0, nullptr), 321);
      for (int y = 0; y < 3; ++y)
        for (int x = 0; x < width * 2; ++x)
          EXPECT_EQ(output->GetReadPtr()[y * output->GetPitch() + x],
                    x % 2 ? 128 : original->GetReadPtr()[y * original->GetPitch() + x]);
      EXPECT_EQ(FrameSnapshot::capture(original, source->GetVideoInfo()), before);
    }
  }
}
TEST(Yuy2API, RgbUsesPlanarCompositionAndBackIsOrdinaryConversion) {
  for (bool c : {false, true}) {
    AviSynthEnvironment e;
    auto* env = e.get();
    if (c)
      env->Invoke("SetMaxCPU", "none");
    PClip packed = Source(env, VideoInfo::CS_YUY2, 18, 8, 1);
    PClip planar = Convert(env, "ConvertToYV16", packed, false, "center", "center");
    for (const char* name : {"ConvertToRGB24", "ConvertToRGB32", "ConvertToRGB48", "ConvertToRGB64"})
      EqualPixels(env->Invoke(name, packed).AsClip(), env->Invoke(name, planar).AsClip(), env);
    for (int type : {VideoInfo::CS_BGR24, VideoInfo::CS_BGR32, VideoInfo::CS_YV24, VideoInfo::CS_YV12}) {
      PClip src = Source(env, type, 18, 8);
      EqualPixels(env->Invoke("ConvertBackToYUY2", src).AsClip(), env->Invoke("ConvertToYUY2", src).AsClip(), env);
      EqualPixels(env->Invoke("ConvertToYUY2", src).AsClip(),
                  env->Invoke("ConvertToYUY2", env->Invoke("ConvertToYUV422", src)).AsClip(), env);
    }
  }
}
TEST(Yuy2API, NeutralRgbRangeAndAlphaAreIndependent) {
  for (bool c : {false, true}) {
    AviSynthEnvironment e;
    auto* env = e.get();
    if (c)
      env->Invoke("SetMaxCPU", "none");
    for (int full : {0, 1}) {
      const auto vi = make_video_info({6, 3, VideoInfo::CS_BGR32, 1, 25, 1});
      PVideoFrame frame = env->NewVideoFrame(vi);
      write_frame_plane<uint8_t>(frame, DEFAULT_PLANE, [](int x, int) { return uint8_t(x % 4 == 3 ? 17 : 128); });
      env->propSetInt(env->getFramePropsRW(frame), "_ColorRange", full ? 0 : 1, 0);
      PClip src = new StaticFrameClip(vi, frame);
      AVSValue args[2] = {src, "Rec601"};
      const char* names[2] = {nullptr, "matrix"};
      auto out = env->Invoke("ConvertToYUY2", AVSValue(args, 2), names).AsClip()->GetFrame(0, env);
      for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 6; ++x) {
          EXPECT_EQ(out->GetReadPtr()[y * out->GetPitch() + x * 2], full ? 126 : 128);
          EXPECT_EQ(out->GetReadPtr()[y * out->GetPitch() + x * 2 + 1], 128);
        }
    }
  }
}
} // namespace
} // namespace avsut::test
