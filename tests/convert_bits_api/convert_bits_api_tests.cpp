#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
namespace avsut::test {
namespace {
struct Case {
  int sb, db, sf, df, chroma;
  uint64_t hash;
};
std::vector<Case> Cases() {
  std::ifstream file(DEPTH_FIXTURE);
  std::string line;
  std::vector<Case> cases;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    Case c{};
    std::istringstream in(line);
    if (in >> c.sb >> c.db >> c.sf >> c.df >> c.chroma >> std::hex >> c.hash)
      cases.push_back(c);
  }
  return cases;
}
int Format(int depth, bool chroma) {
  const int i = depth == 32 ? 5 : (depth - 8) / 2;
  const int rgb[] = {VideoInfo::CS_RGBAP,   VideoInfo::CS_RGBAP10, VideoInfo::CS_RGBAP12,
                     VideoInfo::CS_RGBAP14, VideoInfo::CS_RGBAP16, VideoInfo::CS_RGBAPS};
  const int yuv[] = {VideoInfo::CS_YUVA444,    VideoInfo::CS_YUVA444P10, VideoInfo::CS_YUVA444P12,
                     VideoInfo::CS_YUVA444P14, VideoInfo::CS_YUVA444P16, VideoInfo::CS_YUVA444PS};
  return chroma ? yuv[i] : rgb[i];
}
void Write(PVideoFrame& f, int plane, int x, int y, int bits, double value) {
  auto* row = f->GetWritePtr(plane) + y * f->GetPitch(plane);
  if (bits == 8)
    row[x] = uint8_t(value);
  else if (bits == 32)
    reinterpret_cast<float*>(row)[x] = float(value);
  else
    reinterpret_cast<uint16_t*>(row)[x] = uint16_t(value);
}
double Read(const PVideoFrame& f, int plane, int x, int y, int bits) {
  const auto* row = f->GetReadPtr(plane) + y * f->GetPitch(plane);
  return bits == 8    ? row[x]
         : bits == 32 ? reinterpret_cast<const float*>(row)[x]
                      : reinterpret_cast<const uint16_t*>(row)[x];
}
PClip Convert(IScriptEnvironment* env, PClip source, int bits, bool sf, bool df) {
  const AVSValue args[] = {source, bits, sf, df, -1};
  const char* names[] = {nullptr, "bits", "fulls", "fulld", "dither"};
  return env->Invoke("ConvertBits", AVSValue(args, 5), names).AsClip();
}
class DepthPublic : public testing::TestWithParam<Case> {};
TEST_P(DepthPublic, PinnedPixelsAlphaPropertiesAndSourceImmutability) {
  const auto c = GetParam();
  for (bool scalar : {false, true}) {
    AviSynthEnvironment environment;
    auto* env = environment.get();
    if (scalar)
      env->Invoke("SetMaxCPU", "none");
    auto vi = make_video_info({257, 3, Format(c.sb, c.chroma != 0), 1, 25, 1});
    auto frame = env->NewVideoFrame(vi);
    const int yp[] = {PLANAR_Y, PLANAR_U, PLANAR_V}, rp[] = {PLANAR_R, PLANAR_G, PLANAR_B};
    const int* planes = c.chroma ? yp : rp;
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 257; ++x) {
        const int maximum = c.sb == 32 ? 1 : (1 << c.sb) - 1;
        const double v = c.sb == 32 ? float((x * 73) % 769 - 128) / 512.f - (c.chroma ? .5f : 0.f)
                         : x == 0   ? 0
                         : x == 1   ? maximum
                                    : (x * 2573 + 193) & maximum;
        for (int k = 0; k < 3; ++k)
          Write(frame, planes[k], x, y, c.sb, v);
        Write(frame, PLANAR_A, x, y, c.sb, c.sb == 32 ? .5 : 1 << (c.sb - 1));
      }
    set_frame_property_int(env, frame, "_ColorRange", c.sf ? 0 : 1);
    set_frame_property_int(env, frame, "DepthMarker", 471);
    const auto before = FrameSnapshot::capture(frame, vi);
    const PClip source = new StaticFrameClip(vi, frame);
    const auto output = Convert(env, source, c.db, c.sf != 0, c.df != 0)->GetFrame(0, env);
    const int plane = c.chroma ? PLANAR_U : PLANAR_R;
    const int bytes = c.db == 32 ? 4 : c.db == 8 ? 1 : 2;
    for (int y = 0; y < 3; ++y) {
      uint64_t hash = 14695981039346656037ULL;
      const auto* row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int b = 0; b < 257 * bytes; ++b)
        hash = (hash ^ row[b]) * 1099511628211ULL;
      EXPECT_EQ(hash, c.hash) << "scalar=" << scalar;
    }
    const float alpha = c.sb == 32 ? .5f : float(1 << (c.sb - 1));
    const float scale = (c.db == 32 ? 1.f : float((1 << c.db) - 1)) / (c.sb == 32 ? 1.f : float((1 << c.sb) - 1));
    const double expected = c.db == 32 ? alpha * scale : std::floor(alpha * scale + .5f);
    for (int x : {0, 16, 256})
      EXPECT_NEAR(Read(output, PLANAR_A, x, 0, c.db), expected, c.db == 32 ? 1e-7 : 0);
    EXPECT_EQ(env->propGetInt(env->getFramePropsRO(output), "_ColorRange", 0, nullptr), c.df ? 0 : 1);
    EXPECT_EQ(env->propGetInt(env->getFramePropsRO(output), "DepthMarker", 0, nullptr), 471);
    EXPECT_EQ(FrameSnapshot::capture(frame, vi), before);
  }
}
INSTANTIATE_TEST_SUITE_P(Profiles, DepthPublic, testing::ValuesIn(Cases()), [](const testing::TestParamInfo<Case>& p) {
  const auto c = p.param;
  return "S" + std::to_string(c.sb) + "D" + std::to_string(c.db) + "SF" + std::to_string(c.sf) + "DF" +
         std::to_string(c.df) + "C" + std::to_string(c.chroma);
});
TEST(DepthPublicValidation, PinnedProfilesAreComplete) {
  EXPECT_EQ(Cases().size(), 288u);
}
TEST(DepthPacked, MatchesPlanarColorAndIndependentAlphaRange) {
  for (bool scalar : {true, false})
    for (int sb : {8, 16})
      for (int components : {3, 4})
        for (bool sf : {false, true})
          for (bool df : {false, true}) {
            AviSynthEnvironment environment;
            auto* env = environment.get();
            if (scalar)
              env->Invoke("SetMaxCPU", "none");
            const int db = sb == 8 ? 16 : 8;
            const int fmt = sb == 8 ? (components == 3 ? VideoInfo::CS_BGR24 : VideoInfo::CS_BGR32)
                                    : (components == 3 ? VideoInfo::CS_BGR48 : VideoInfo::CS_BGR64);
            auto vi = make_video_info({17, 3, fmt, 1, 25, 1}),
                 pv = make_video_info({17, 3, Format(sb, false), 1, 25, 1});
            auto packed = env->NewVideoFrame(vi), planar = env->NewVideoFrame(pv);
            const int planes[] = {PLANAR_B, PLANAR_G, PLANAR_R, PLANAR_A};
            for (int y = 0; y < 3; ++y)
              for (int x = 0; x < 17; ++x)
                for (int c = 0; c < 4; ++c) {
                  const int v = c == 3 ? (1 << (sb - 1)) : (x * 7919 + y * 3571 + c * 1291) & ((1 << sb) - 1);
                  Write(planar, planes[c], x, y, sb, v);
                  if (c < components)
                    Write(packed, 0, x * components + c, 2 - y, sb, v);
                }
            const auto before = FrameSnapshot::capture(packed, vi);
            const PClip a = new StaticFrameClip(vi, packed), b = new StaticFrameClip(pv, planar);
            const auto output = Convert(env, a, db, sf, df)->GetFrame(0, env),
                       reference = Convert(env, b, db, sf, df)->GetFrame(0, env);
            for (int y = 0; y < 3; ++y)
              for (int x = 0; x < 17; ++x)
                for (int c = 0; c < components; ++c)
                  EXPECT_EQ(Read(output, 0, x * components + c, 2 - y, db), Read(reference, planes[c], x, y, db));
            EXPECT_EQ(FrameSnapshot::capture(packed, vi), before);
          }
}
TEST(DepthFloat, ExtremeInputsSaturateInCAndNative) {
  for (bool scalar : {true, false})
    for (int db : {8, 10, 16}) {
      AviSynthEnvironment environment;
      auto* env = environment.get();
      if (scalar)
        env->Invoke("SetMaxCPU", "none");
      auto vi = make_video_info({33, 2, VideoInfo::CS_RGBPS, 1, 25, 1});
      auto frame = env->NewVideoFrame(vi);
      const float values[] = {0, 1, 1e10f, -1e10f, INFINITY, -INFINITY, NAN, .5f};
      for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 33; ++x)
          for (int plane : {PLANAR_R, PLANAR_G, PLANAR_B})
            Write(frame, plane, x, y, 32, values[x % 8]);
      const PClip source = new StaticFrameClip(vi, frame);
      const auto output = Convert(env, source, db, true, true)->GetFrame(0, env);
      const int m = (1 << db) - 1, expected[] = {0, m, m, 0, m, 0, 0, 1 << (db - 1)};
      for (int x = 0; x < 33; ++x)
        EXPECT_EQ(Read(output, PLANAR_R, x, 0, db), expected[x % 8]);
    }
}

TEST(DepthProperties, SourceRangeSelectionIsLocalToEachRequestedFrame) {
  for (bool scalar : {true, false}) {
    AviSynthEnvironment environment;
    auto* env = environment.get();
    if (scalar)
      env->Invoke("SetMaxCPU", "none");
    auto vi = make_video_info({17, 2, VideoInfo::CS_Y8, 3, 25, 1});
    std::vector<PVideoFrame> frames;
    for (int n = 0; n < 3; ++n) {
      auto f = env->NewVideoFrame(vi);
      for (int y = 0; y < 2; ++y)
        std::memset(f->GetWritePtr() + y * f->GetPitch(), 16, 17);
      if (n < 2)
        set_frame_property_int(env, f, "_ColorRange", n);
      frames.push_back(f);
    }
    const PClip source = new FrameSequenceClip(vi, frames);
    const AVSValue args[] = {source, 10, false};
    const char* names[] = {nullptr, "bits", "fulld"};
    const PClip output = env->Invoke("ConvertBits", AVSValue(args, 3), names).AsClip();
    for (int n : {1, 0, 2, 1, 0}) {
      const auto f = output->GetFrame(n, env);
      EXPECT_EQ(Read(f, PLANAR_Y, 0, 0, 10), n == 0 ? 119 : 64);
      EXPECT_EQ(env->propGetInt(env->getFramePropsRO(f), "_ColorRange", 0, nullptr), 1);
    }
    EXPECT_EQ(env->propGetInt(env->getFramePropsRO(frames[0]), "_ColorRange", 0, nullptr), 0);
    EXPECT_EQ(env->propNumElements(env->getFramePropsRO(frames[2]), "_ColorRange"), -1);
  }
}
TEST(DepthProperties, FalseTrueRangeRetainsStorageReinterpretation) {
  AviSynthEnvironment environment;
  auto* env = environment.get();
  auto vi = make_video_info({17, 2, VideoInfo::CS_Y10, 1, 25, 1});
  auto frame = env->NewVideoFrame(vi);
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 17; ++x)
      Write(frame, PLANAR_Y, x, y, 10, x * 3571);
  const PClip source = new StaticFrameClip(vi, frame);
  for (int bits : {8, 14}) {
    const AVSValue args[] = {source, bits, false, true, true};
    const char* names[] = {nullptr, "bits", "truerange", "fulls", "fulld"};
    const PClip clip = env->Invoke("ConvertBits", AVSValue(args, 5), names).AsClip();
    const auto output = clip->GetFrame(0, env);
    EXPECT_EQ(clip->GetVideoInfo().BitsPerComponent(), bits);
    for (int x = 0; x < 17; ++x)
      EXPECT_EQ(Read(output, PLANAR_Y, x, 0, bits),
                bits == 14 ? x * 3571 : std::floor(float(x * 3571) * (255.f / 65535) + .5f));
  }
}
} // namespace
} // namespace avsut::test
