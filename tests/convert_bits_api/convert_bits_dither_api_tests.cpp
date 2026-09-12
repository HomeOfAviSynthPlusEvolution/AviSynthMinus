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
  int sb, db, qb, sf, df, chroma, mode;
  uint64_t hash;
};
std::vector<Case> Cases() {
  std::vector<Case> cases;
  for (int mode : {0, 1}) {
    std::ifstream file(mode == 0 ? ORDERED_FIXTURE : FLOYD_FIXTURE);
    std::string line;
    while (std::getline(file, line)) {
      if (line.empty() || line[0] == '#')
        continue;
      Case c{};
      c.mode = mode;
      std::istringstream in(line);
      if (in >> c.sb >> c.db >> c.qb >> c.sf >> c.df >> c.chroma >> std::hex >> c.hash)
        cases.push_back(c);
    }
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
PClip Convert(IScriptEnvironment* env, PClip source, int bits, bool sf, bool df, int mode, int qb) {
  const AVSValue args[] = {source, bits, sf, df, mode, qb};
  const char* names[] = {nullptr, "bits", "fulls", "fulld", "dither", "dither_bits"};
  return env->Invoke("ConvertBits", AVSValue(args, 6), names).AsClip();
}
class DitherPublic : public testing::TestWithParam<Case> {};
TEST_P(DitherPublic, PinnedPixelsAlphaPropertiesAndSourceImmutability) {
  const auto c = GetParam();
  for (bool scalar : {false, true}) {
    AviSynthEnvironment environment;
    auto* env = environment.get();
    if (scalar)
      env->Invoke("SetMaxCPU", "none");
    auto vi = make_video_info({257, 17, Format(c.sb, c.chroma != 0), 1, 25, 1});
    auto frame = env->NewVideoFrame(vi);
    const int yp[] = {PLANAR_Y, PLANAR_U, PLANAR_V}, rp[] = {PLANAR_R, PLANAR_G, PLANAR_B};
    const int* planes = c.chroma ? yp : rp;
    for (int y = 0; y < 17; ++y)
      for (int x = 0; x < 257; ++x) {
        const int maximum = c.sb == 32 ? 1 : (1 << c.sb) - 1;
        const double v = c.sb == 32 ? float((x * 73) % 769 - 128) / 512.f - (c.chroma ? .5f : 0.f)
                         : x == 0   ? 0
                         : x == 1   ? maximum
                                    : (x * 2573 + y * 7919 + 193) & maximum;
        for (int k = 0; k < 3; ++k)
          Write(frame, planes[k], x, y, c.sb, v);
        Write(frame, PLANAR_A, x, y, c.sb, c.sb == 32 ? .5 : 1 << (c.sb - 1));
      }
    set_frame_property_int(env, frame, "_ColorRange", c.sf ? 0 : 1);
    set_frame_property_int(env, frame, "DepthMarker", 471);
    const auto before = FrameSnapshot::capture(frame, vi);
    const PClip source = new StaticFrameClip(vi, frame);
    const auto output = Convert(env, source, c.db, c.sf != 0, c.df != 0, c.mode, c.qb)->GetFrame(0, env);
    const int plane = c.chroma ? PLANAR_U : PLANAR_R;
    const int bytes = c.db == 32 ? 4 : c.db == 8 ? 1 : 2;
    uint64_t hash = 14695981039346656037ULL;
    for (int y = 0; y < 17; ++y) {
      const auto* row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int b = 0; b < 257 * bytes; ++b)
        hash = (hash ^ row[b]) * 1099511628211ULL;
    }
    EXPECT_EQ(hash, c.hash) << "scalar=" << scalar;
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
INSTANTIATE_TEST_SUITE_P(Profiles, DitherPublic, testing::ValuesIn(Cases()), [](const testing::TestParamInfo<Case>& p) {
  const auto c = p.param;
  return "M" + std::to_string(c.mode) + "Q" + std::to_string(c.qb) + "S" + std::to_string(c.sb) + "D" +
         std::to_string(c.db) + "SF" + std::to_string(c.sf) + "DF" + std::to_string(c.df) + "C" +
         std::to_string(c.chroma);
});
TEST(DitherPublicValidation, ProfilesAreComplete) {
  EXPECT_EQ(Cases().size(), 1952u);
}
TEST(DitherPublicValidation, SevenBitThresholdRegression) {
  for (bool scalar : {false, true}) {
    AviSynthEnvironment environment;
    auto* env = environment.get();
    if (scalar)
      env->Invoke("SetMaxCPU", "none");
    auto vi = make_video_info({16, 16, VideoInfo::CS_RGBP16, 1, 25, 1});
    auto frame = env->NewVideoFrame(vi);
    for (int plane : {PLANAR_R, PLANAR_G, PLANAR_B})
      for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
          Write(frame, plane, x, y, 16, 60);
    const PClip source = new StaticFrameClip(vi, frame);
    const auto output = Convert(env, source, 10, true, true, 0, 9)->GetFrame(0, env);
    EXPECT_EQ(Read(output, PLANAR_R, 0, 9, 10), 0);
  }
}
} // namespace
} // namespace avsut::test
