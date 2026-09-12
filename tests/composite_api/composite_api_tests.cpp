// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#include "support/video_filter_test_support.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace avsut::test {
namespace {
int Plane(const VideoInfo& vi, int c) {
  const int yuv[] = {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A};
  const int rgb[] = {PLANAR_B, PLANAR_G, PLANAR_R, PLANAR_A};
  return vi.IsPlanar() ? (vi.IsRGB() ? rgb[c] : yuv[c]) : DEFAULT_PLANE;
}
int Width(const VideoInfo& vi, int c) {
  return vi.IsYUY2() ? vi.width >> (c == 0 ? 0 : 1) : !vi.IsPlanar() ? vi.width : vi.width >> vi.GetPlaneWidthSubsampling(Plane(vi, c));
}
int Height(const VideoInfo& vi, int c) {
  return vi.IsPlanar() ? vi.height >> vi.GetPlaneHeightSubsampling(Plane(vi, c)) : vi.height;
}
int Offset(const VideoInfo& vi, int c, int x) {
  if (vi.IsYUY2()) return c == 0 ? 2 * x : 4 * x + (c == 1 ? 1 : 3);
  return vi.IsPlanar() ? x : x * vi.NumComponents() + c;
}
double Read(const PVideoFrame& frame, const VideoInfo& vi, int c, int x, int y) {
  const int p = Plane(vi, c), offset = Offset(vi, c, x);
  const auto* row = frame->GetReadPtr(p) + ptrdiff_t(y) * frame->GetPitch(p);
  return vi.ComponentSize() == 1 ? row[offset] : vi.ComponentSize() == 2
       ? reinterpret_cast<const uint16_t*>(row)[offset] : reinterpret_cast<const float*>(row)[offset];
}
void Write(PVideoFrame& frame, const VideoInfo& vi, int c, int x, int y, double value) {
  const int p = Plane(vi, c), offset = Offset(vi, c, x);
  auto* row = frame->GetWritePtr(p) + ptrdiff_t(y) * frame->GetPitch(p);
  if (vi.ComponentSize() == 1) row[offset] = uint8_t(value);
  else if (vi.ComponentSize() == 2) reinterpret_cast<uint16_t*>(row)[offset] = uint16_t(value);
  else reinterpret_cast<float*>(row)[offset] = float(value);
}
double Maximum(const VideoInfo& vi) { return vi.BitsPerComponent() == 32 ? 1 : (1 << vi.BitsPerComponent()) - 1; }
double Rounded(double value, const VideoInfo& vi) {
  return vi.BitsPerComponent() == 32 ? float(value) : std::floor(std::clamp(value, 0.0, Maximum(vi)) + .5);
}
struct Source {
  VideoInfo vi;
  PVideoFrame frame;
  PClip clip;
  FrameSnapshot snapshot;
};
Source Make(IScriptEnvironment* env, int type, int seed, int width = 36, int height = 6, bool full = false) {
  const auto vi = make_video_info({width, height, type, 3, 25, 1});
  auto frame = env->NewVideoFrame(vi);
  for (const int p : video_frame_planes(vi)) fill_plane_full_pitch(frame, 0xA5, p);
  for (int c = 0; c < vi.NumComponents(); ++c)
    for (int y = 0; y < Height(vi, c); ++y)
      for (int x = 0; x < Width(vi, c); ++x) {
        const double normalized = full ? 1.0 : ((seed + x * 17 + y * 31 + c * 43) % 256) / 255.0;
        double value = vi.BitsPerComponent() == 32 ? normalized : std::floor(normalized * Maximum(vi) + .5);
        if (!full && vi.BitsPerComponent() == 32 && !vi.IsRGB() && c > 0 && c < 3) value -= .5;
        Write(frame, vi, c, x, y, value);
      }
  env->propSetInt(env->getFramePropsRW(frame), "CompositeMarker", seed, 0);
  return {vi, frame, PClip(new FrameSequenceClip(vi, {frame, frame, frame})), FrameSnapshot::capture(frame, vi)};
}
void Unchanged(const Source& source) { EXPECT_EQ(FrameSnapshot::capture(source.frame, source.vi), source.snapshot); }
PClip LayerClip(IScriptEnvironment* env, const Source& base, const Source& source, const char* op,
                double opacity, bool chroma = true, int x = 0, int y = 0, const char* placement = "MPEG1") {
  const AVSValue args[] = {base.clip, source.clip, op, opacity, chroma, x, y, 8, placement};
  const char* names[] = {nullptr, nullptr, "op", "opacity", "use_chroma", "x", "y", "threshold", "placement"};
  return env->Invoke("Layer", AVSValue(args, 9), names).AsClip();
}
double Luma(const Source& source, int x, int y) {
  const double b = Read(source.frame, source.vi, 0, x, y), g = Read(source.frame, source.vi, 1, x, y);
  const double r = Read(source.frame, source.vi, 2, x, y);
  return source.vi.BitsPerComponent() == 32 ? float(.114f * float(b) + .587f * float(g) + .299f * float(r))
       : std::floor((3736 * b + 19234 * g + 9798 * r) / 32768.0);
}
double Weight(const VideoInfo& vi, double opacity, double mask) {
  const double maximum = Maximum(vi);
  return vi.BitsPerComponent() == 32 ? opacity * mask : std::floor(mask * std::floor(opacity * maximum + .5) / maximum + .5) / maximum;
}

TEST(CompositeMerge, PublicFormatsUseOneArithmeticAndPreserveSources) {
  for (bool scalar : {false, true})
    for (int type : {VideoInfo::CS_YV24, VideoInfo::CS_YUV444P10, VideoInfo::CS_YUVA444P16, VideoInfo::CS_YUVA444PS,
                    VideoInfo::CS_RGBAP, VideoInfo::CS_RGBAP16, VideoInfo::CS_RGBAPS, VideoInfo::CS_BGR32,
                    VideoInfo::CS_BGR64, VideoInfo::CS_YUY2}) {
      AviSynthEnvironment environment; auto* env = environment.get();
      if (scalar) env->Invoke("SetMaxCPU", "none");
      const auto base = Make(env, type, 17), source = Make(env, type, 113);
      for (double weight : {0.0, .25, .5, 1.0}) {
        SCOPED_TRACE(::testing::Message() << type << '/' << scalar << '/' << weight);
        const AVSValue args[] = {base.clip, source.clip, weight};
        const auto output = env->Invoke("Merge", AVSValue(args, 3)).AsClip()->GetFrame(1, env);
        for (int c = 0; c < base.vi.NumComponents(); ++c)
          for (int y = 0; y < Height(base.vi, c); ++y)
            for (int x = 0; x < Width(base.vi, c); ++x) {
              const double a = Read(base.frame, base.vi, c, x, y), b = Read(source.frame, source.vi, c, x, y);
              EXPECT_NEAR(Read(output, base.vi, c, x, y), Rounded(a + weight * (b - a), base.vi), base.vi.ComponentSize() == 4 ? 1e-7 : 0);
            }
        EXPECT_EQ(env->propGetInt(env->getFramePropsRO(output), "CompositeMarker", 0, nullptr), weight == 1 ? 113 : 17);
        EXPECT_NE(output->CheckMemory(), 1);
        Unchanged(base); Unchanged(source);
      }
    }
}

TEST(CompositeLayer, RgbOperationsHonorAlphaAndGuidesAcrossStorageAndTargets) {
  for (bool scalar : {false, true})
    for (int type : {VideoInfo::CS_RGBAP, VideoInfo::CS_RGBAP10, VideoInfo::CS_RGBAP16, VideoInfo::CS_RGBAPS,
                    VideoInfo::CS_BGR32, VideoInfo::CS_BGR64}) {
      AviSynthEnvironment environment; auto* env = environment.get();
      if (scalar) env->Invoke("SetMaxCPU", "none");
      const auto base = Make(env, type, 29), source = Make(env, type, 151);
      for (const char* op : {"Add", "Subtract", "Mul", "Fast", "Lighten", "Darken"})
        for (bool chroma : {false, true}) {
          const bool fast = !std::strcmp(op, "Fast"), mul = !std::strcmp(op, "Mul"), sub = !std::strcmp(op, "Subtract");
          const bool lighter = !std::strcmp(op, "Lighten"), darker = !std::strcmp(op, "Darken");
          if (!chroma && (fast || lighter || darker)) continue;
          SCOPED_TRACE(::testing::Message() << type << '/' << scalar << '/' << op << '/' << chroma);
          const auto output = LayerClip(env, base, source, op, .37, chroma)->GetFrame(1, env);
          const double maximum = Maximum(base.vi), opacity = double(float(.37));
          for (int y = 0; y < base.vi.height; ++y)
            for (int x = 0; x < base.vi.width; ++x) {
              const double w = fast ? .5 : Weight(base.vi, opacity, Read(source.frame, base.vi, 3, x, y));
              const double threshold = base.vi.BitsPerComponent() == 32 ? float(8 / 255.0f) : 8 << (base.vi.BitsPerComponent() - 8);
              const double ga = Luma(base, x, y), gb = Luma(source, x, y);
              const auto guide_limit = [&](double v) { return base.vi.ComponentSize() == 4 ? double(float(v)) : v; };
              const bool selected = (!lighter || gb > guide_limit(ga + threshold)) && (!darker || gb < guide_limit(ga - threshold));
              for (int c = 0; c < 4; ++c) {
                const double a = Read(base.frame, base.vi, c, x, y), b = chroma ? Read(source.frame, base.vi, c, x, y) : gb;
                double target = sub ? maximum - b : b;
                if (mul) target = base.vi.ComponentSize() == 4 ? a * b : std::floor(a * b / maximum);
                double expected = selected ? Rounded(a + (target - a) * w, base.vi) : a;
                if (fast && base.vi.IsPlanar() && c == 3) expected = a;
                ASSERT_NEAR(Read(output, base.vi, c, x, y), expected, base.vi.ComponentSize() == 4 ? 2e-7 : 0) << c << '/' << x << '/' << y;
              }
            }
          EXPECT_NE(output->CheckMemory(), 1); Unchanged(base); Unchanged(source);
        }
    }
}

TEST(CompositeLayer, ClippedYuvaUsesFullResolutionAlphaAndPlacement) {
  for (bool scalar : {false, true})
    for (int type : {VideoInfo::CS_YUVA420, VideoInfo::CS_YUVA420P10, VideoInfo::CS_YUVA420P16, VideoInfo::CS_YUVA420PS})
      for (bool mpeg2 : {false, true}) {
        AviSynthEnvironment environment; auto* env = environment.get();
        if (scalar) env->Invoke("SetMaxCPU", "none");
        const auto base = Make(env, type, 13, 20, 8), source = Make(env, type, 171, 16, 6);
        SCOPED_TRACE(::testing::Message() << type << '/' << scalar << '/' << mpeg2);
        const auto output = LayerClip(env, base, source, "Add", .63, true, -2, -2, mpeg2 ? "MPEG2" : "MPEG1")->GetFrame(1, env);
        for (int c = 0; c < 4; ++c) {
          const int sub = c == 1 || c == 2 ? 2 : 1;
          for (int y = 0; y < Height(base.vi, c); ++y)
            for (int x = 0; x < Width(base.vi, c); ++x) {
              const double a = Read(base.frame, base.vi, c, x, y);
              double expected = a;
              if (c != 3 && x < 14 / sub && y < 4 / sub) {
                const int sx = x * sub + 2, sy = y * sub + 2;
                const auto alpha = [&](int dx, int dy) { return Read(source.frame, source.vi, 3, sx + dx, sy + dy); };
                double mask = alpha(0, 0);
                if (sub == 2) {
                  if (mpeg2) mask = (alpha(-1, 0) + alpha(-1, 1) + 2 * (alpha(0, 0) + alpha(0, 1)) + alpha(1, 0) + alpha(1, 1)) / 8;
                  else mask = (alpha(0, 0) + alpha(1, 0) + alpha(0, 1) + alpha(1, 1)) / 4;
                  mask = Rounded(mask, base.vi);
                }
                const double b = Read(source.frame, source.vi, c, x + 2 / sub, y + 2 / sub);
                expected = Rounded(a + (b - a) * Weight(base.vi, double(float(.63)), mask), base.vi);
              }
              ASSERT_NEAR(Read(output, base.vi, c, x, y), expected, base.vi.ComponentSize() == 4 ? 2e-7 : 0) << c << '/' << x << '/' << y;
            }
        }
        Unchanged(base); Unchanged(source); EXPECT_NE(output->CheckMemory(), 1);
      }
}

TEST(CompositeLayer, FullScaleMultiplyAndExtremeEmptyOverlap) {
  for (int type : {VideoInfo::CS_YV24, VideoInfo::CS_YUV444P16, VideoInfo::CS_RGBAP16, VideoInfo::CS_BGR64}) {
    AviSynthEnvironment environment; auto* env = environment.get();
    const auto base = Make(env, type, 31), white = Make(env, type, 255, 36, 6, true);
    const auto multiplied = LayerClip(env, base, white, "Mul", 1)->GetFrame(0, env);
    for (int c = 0; c < (base.vi.IsRGB() ? base.vi.NumComponents() : 1); ++c)
      for (int y = 0; y < Height(base.vi, c); ++y)
        for (int x = 0; x < Width(base.vi, c); ++x)
          EXPECT_EQ(Read(multiplied, base.vi, c, x, y), Read(base.frame, base.vi, c, x, y));
    for (int offset : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
      const auto empty = LayerClip(env, base, white, "Add", 1, true, offset, offset)->GetFrame(0, env);
      EXPECT_EQ(FrameSnapshot::capture(empty, base.vi), base.snapshot);
    }
    Unchanged(base); Unchanged(white);
  }
}

TEST(CompositeLayer, YuvModesShareNormalizedArithmeticAndLumaDecisions) {
  for (bool scalar : {false, true})
    for (int type : {VideoInfo::CS_YV12, VideoInfo::CS_YV16, VideoInfo::CS_YV411, VideoInfo::CS_YUY2,
                    VideoInfo::CS_YUV444P16, VideoInfo::CS_YUVA444P16, VideoInfo::CS_YUVA444PS}) {
      AviSynthEnvironment environment; auto* env = environment.get();
      if (scalar) env->Invoke("SetMaxCPU", "none");
      const auto base = Make(env, type, 47), source = Make(env, type, 163);
      const auto& vi = base.vi;
      const bool floating = vi.ComponentSize() == 4, alpha = vi.IsYUVA();
      const double maximum = Maximum(vi), neutral = floating ? 0 : (maximum + 1) / 2;
      const double threshold = floating ? double(8 / 255.f) : 8 << (vi.BitsPerComponent() - 8);
      for (const char* op : {"Add", "Subtract", "Mul", "Fast", "Lighten", "Darken"})
        for (bool chroma : {false, true}) {
          const bool fast = !std::strcmp(op, "Fast"), mul = !std::strcmp(op, "Mul"), sub = !std::strcmp(op, "Subtract");
          const bool lighter = !std::strcmp(op, "Lighten"), darker = !std::strcmp(op, "Darken"), select = lighter || darker;
          if (!chroma && (fast || select)) continue;
          SCOPED_TRACE(::testing::Message() << type << '/' << scalar << '/' << op << '/' << chroma);
          const auto output = LayerClip(env, base, source, op, .5, chroma)->GetFrame(0, env);
          for (int c = 0; c < vi.NumComponents(); ++c) {
            const int sx = vi.width / Width(vi, c), sy = vi.height / Height(vi, c);
            const auto average = [&](const Source& frame, int plane, int x, int y) {
              double value = 0;
              for (int dy = 0; dy < sy; ++dy)
                for (int dx = 0; dx < sx; ++dx) value += Read(frame.frame, frame.vi, plane, sx * x + dx, sy * y + dy);
              return Rounded(value / (sx * sy), vi);
            };
            for (int y = 0; y < Height(vi, c); ++y)
              for (int x = 0; x < Width(vi, c); ++x) {
                const double a = Read(base.frame, vi, c, x, y), b = Read(source.frame, vi, c, x, y);
                double target = b, opacity = .5;
                if (c == 0 && mul) target = floating ? a * b : std::floor(a * b / maximum);
                else if (c != 0 && !chroma) { target = neutral; if (mul) opacity *= .5; }
                else if (sub) target = (c == 0 ? maximum : 2 * neutral) - b;
                const double mask = alpha && !fast && !select ? average(source, 3, x, y) : maximum;
                const double w = fast ? .5 : Weight(vi, opacity, mask);
                const auto limit = [&](double v) { return floating ? double(float(v)) : v; };
                const bool selected = !select || (lighter ? average(source, 0, x, y) > limit(average(base, 0, x, y) + threshold)
                                                         : average(source, 0, x, y) < limit(average(base, 0, x, y) - threshold));
                const double expected = c == 3 || !selected ? a : Rounded(a + (target - a) * w, vi);
                ASSERT_NEAR(Read(output, vi, c, x, y), expected, floating ? 2e-7 : 0) << c << '/' << x << '/' << y;
              }
          }
          Unchanged(base); Unchanged(source); EXPECT_NE(output->CheckMemory(), 1);
        }
    }
}

TEST(CompositeOverlay, BlendUsesContinuousMasksAndPreservesAlpha) {
  for (bool scalar : {false, true})
    for (int type : {VideoInfo::CS_YUVA444, VideoInfo::CS_YUVA444P10, VideoInfo::CS_YUVA444P16, VideoInfo::CS_YUVA444PS,
                    VideoInfo::CS_RGBAP, VideoInfo::CS_RGBAPS}) {
      AviSynthEnvironment environment; auto* env = environment.get();
      if (scalar) env->Invoke("SetMaxCPU", "none");
      const auto base = Make(env, type, 9), source = Make(env, type, 191);
      const int depth = base.vi.BitsPerComponent();
      const int gray = depth == 8 ? VideoInfo::CS_Y8 : depth == 10 ? VideoInfo::CS_Y10 : depth == 16 ? VideoInfo::CS_Y16 : VideoInfo::CS_Y32;
      const auto mask = Make(env, gray, 89);
      const AVSValue args[] = {base.clip, source.clip, mask.clip, .43, "Blend"};
      const char* names[] = {nullptr, nullptr, "mask", "opacity", "mode"};
      const auto output = env->Invoke("Overlay", AVSValue(args, 5), names).AsClip()->GetFrame(0, env);
      SCOPED_TRACE(::testing::Message() << type << '/' << scalar);
      for (int c = 0; c < 4; ++c)
        for (int y = 0; y < base.vi.height; ++y)
          for (int x = 0; x < base.vi.width; ++x) {
            const double a = Read(base.frame, base.vi, c, x, y), b = Read(source.frame, base.vi, c, x, y);
            const double w = double(float(.43)) * Read(mask.frame, mask.vi, 0, x, y) / Maximum(base.vi);
            EXPECT_NEAR(Read(output, base.vi, c, x, y), c == 3 ? a : Rounded(a + (b - a) * w, base.vi), depth == 32 ? 2e-7 : 0);
          }
      Unchanged(base); Unchanged(source); Unchanged(mask); EXPECT_NE(output->CheckMemory(), 1);
    }
}
} // namespace
} // namespace avsut::test
