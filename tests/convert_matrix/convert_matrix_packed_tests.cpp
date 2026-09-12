#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"
#include <gtest/gtest.h>
#include <cstring>
namespace avsut::test {
namespace {
template <class T>
void CheckPacked(IScriptEnvironment* env, int width, int components, bool with_alpha) {
  const bool high = sizeof(T) == 2;
  const int packed_format = high ? (components == 4 ? VideoInfo::CS_BGR64 : VideoInfo::CS_BGR48)
                                 : (components == 4 ? VideoInfo::CS_BGR32 : VideoInfo::CS_BGR24);
  const int planar_format = high ? VideoInfo::CS_RGBAP16 : VideoInfo::CS_RGBAP;
  auto pv = make_video_info({width, 3, packed_format, 1, 25, 1});
  auto rv = make_video_info({width, 3, planar_format, 1, 25, 1});
  auto packed = env->NewVideoFrame(pv), planar = env->NewVideoFrame(rv);
  const int planes[] = {PLANAR_B, PLANAR_G, PLANAR_R, PLANAR_A};
  for (int y = 0; y < 3; ++y) {
    auto* row = reinterpret_cast<T*>(packed->GetWritePtr() + (2 - y) * packed->GetPitch());
    for (int x = 0; x < width; ++x)
      for (int c = 0; c < 4; ++c) {
        const T sample =
            c == 3 && components == 3 ? T(-1) : T((x * 3271 + y * 7351 + c * 11317) & (high ? 65535 : 255));
        if (c < components)
          row[x * components + c] = sample;
        reinterpret_cast<T*>(planar->GetWritePtr(planes[c]) + y * planar->GetPitch(planes[c]))[x] = sample;
      }
  }
  set_frame_property_int(env, packed, "PackedMatrixMarker", 421);
  const auto packed_before = FrameSnapshot::capture(packed, pv);
  const PClip pc = new StaticFrameClip(pv, packed), rc = new StaticFrameClip(rv, planar);
  const auto packed_gray = env->Invoke("ConvertToY", pc).AsClip()->GetFrame(0, env);
  const auto planar_gray = env->Invoke("ConvertToY", rc).AsClip()->GetFrame(0, env);
  for (int y = 0; y < 3; ++y)
    EXPECT_EQ(std::memcmp(packed_gray->GetReadPtr() + y * packed_gray->GetPitch(),
                          planar_gray->GetReadPtr() + y * planar_gray->GetPitch(), width * sizeof(T)),
              0);
  const auto gray_rgb = env->Invoke("Greyscale", pc).AsClip()->GetFrame(0, env);
  for (int y = 0; y < 3; ++y) {
    const auto* row = reinterpret_cast<const T*>(gray_rgb->GetReadPtr() + (2 - y) * gray_rgb->GetPitch());
    const auto* luma = reinterpret_cast<const T*>(planar_gray->GetReadPtr() + y * planar_gray->GetPitch());
    const auto* original = reinterpret_cast<const T*>(packed->GetReadPtr() + (2 - y) * packed->GetPitch());
    for (int x = 0; x < width; ++x) {
      for (int c = 0; c < 3; ++c)
        EXPECT_EQ(row[x * components + c], luma[x]);
      if (components == 4)
        EXPECT_EQ(row[x * components + 3], original[x * components + 3]);
    }
  }
  const PClip py = env->Invoke("ConvertToYUVA444", pc).AsClip();
  const PClip ry = env->Invoke("ConvertToYUVA444", rc).AsClip();
  const auto actual = py->GetFrame(0, env), expected = ry->GetFrame(0, env);
  for (int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A})
    for (int y = 0; y < 3; ++y)
      EXPECT_EQ(std::memcmp(actual->GetReadPtr(plane) + y * actual->GetPitch(plane),
                            expected->GetReadPtr(plane) + y * expected->GetPitch(plane), width * sizeof(T)),
                0);
  EXPECT_EQ(FrameSnapshot::capture(packed, pv), packed_before);
  EXPECT_EQ(env->propGetInt(env->getFramePropsRO(actual), "PackedMatrixMarker", 0, nullptr), 421);

  const PClip input = with_alpha ? py : env->Invoke("RemoveAlphaPlane", py).AsClip();
  const auto before = input->GetFrame(0, env);
  const auto snapshot = FrameSnapshot::capture(before, input->GetVideoInfo());
  const char* filter = high ? (components == 4 ? "ConvertToRGB64" : "ConvertToRGB48")
                            : (components == 4 ? "ConvertToRGB32" : "ConvertToRGB24");
  const PClip packed_result = env->Invoke(filter, input).AsClip();
  const PClip planar_result = env->Invoke("ConvertToPlanarRGBA", input).AsClip();
  const auto result = packed_result->GetFrame(0, env), reference = planar_result->GetFrame(0, env);
  for (int y = 0; y < 3; ++y) {
    const auto* row = reinterpret_cast<const T*>(result->GetReadPtr() + (2 - y) * result->GetPitch());
    for (int x = 0; x < width; ++x)
      for (int c = 0; c < components; ++c)
        EXPECT_EQ(row[x * components + c],
                  reinterpret_cast<const T*>(reference->GetReadPtr(planes[c]) + y * reference->GetPitch(planes[c]))[x]);
  }
  EXPECT_EQ(FrameSnapshot::capture(before, input->GetVideoInfo()), snapshot);
  EXPECT_EQ(env->propNumElements(env->getFramePropsRO(result), "_ChromaLocation"), -1);
}
TEST(PackedMatrix, PublicCompositionOrientationAlphaAndTails) {
  for (bool scalar : {true, false}) {
    AviSynthEnvironment environment;
    auto* env = environment.get();
    if (scalar)
      env->Invoke("SetMaxCPU", "none");
    for (int width : {1, 17, 33})
      for (int components : {3, 4})
        for (bool alpha : {true, false}) {
          SCOPED_TRACE(::testing::Message() << scalar << '/' << width << '/' << components << '/' << alpha);
          CheckPacked<uint8_t>(env, width, components, alpha);
          CheckPacked<uint16_t>(env, width, components, alpha);
        }
  }
}
} // namespace
} // namespace avsut::test
