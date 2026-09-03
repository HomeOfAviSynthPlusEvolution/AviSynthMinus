#include <gtest/gtest.h>

#include <avisynth.h>

#ifdef AVS_WINDOWS
#include <avs/win.h>
#define AVSUT_FINDING_B10_WINDOWS_CONTEXT
#define VS_TARGET_OS_WINDOWS
#endif

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B10_UNDEF_AVS_UNUSED
#endif
#include "filters/exprfilter/exprfilter.h"
#ifdef AVSUT_FINDING_B10_WINDOWS_CONTEXT
#undef VS_TARGET_OS_WINDOWS
#undef AVSUT_FINDING_B10_WINDOWS_CONTEXT
#endif
#ifdef AVSUT_FINDING_B10_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B10_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace avsut::test {
namespace {

struct StaticVideoSource {
  VideoInfo video_info;
  PVideoFrame frame;
  StaticFrameClip* clip_impl;
  PClip clip;
  FrameSnapshot snapshot;
};

StaticVideoSource make_y8_row_source(AviSynthEnvironment& environment) {
  const VideoInfo video_info = make_video_info(VideoInfoSpec{19, 4, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0xa5, PLANAR_Y);
  for (int y = 0; y < video_info.height; ++y) {
    auto* row =
        frame->GetWritePtr(PLANAR_Y) + static_cast<std::size_t>(y) * frame->GetPitch(PLANAR_Y);
    for (int x = 0; x < video_info.width; ++x) {
      row[x] = static_cast<std::uint8_t>(20 + y * 40 + x);
    }
  }

  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

StaticVideoSource make_y32_negative_source(AviSynthEnvironment& environment) {
  const VideoInfo video_info = make_video_info(VideoInfoSpec{8, 1, VideoInfo::CS_Y32, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0xa5, PLANAR_Y);
  auto* row = reinterpret_cast<float*>(frame->GetWritePtr(PLANAR_Y));
  constexpr std::array<float, 8> kSamples{-1.0F,   -0.75F,   -0.5F,     -0.25F,
                                          -0.125F, -0.0625F, -0.03125F, -0.015625F};
  for (int x = 0; x < video_info.width; ++x) {
    row[x] = kSamples[static_cast<std::size_t>(x)];
  }

  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

StaticVideoSource make_rgbap8_source(AviSynthEnvironment& environment) {
  const VideoInfo video_info = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_RGBAP8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  constexpr std::array<std::pair<int, std::uint8_t>, 4> kPlaneValues{{
      {PLANAR_R, 17},
      {PLANAR_G, 61},
      {PLANAR_B, 149},
      {PLANAR_A, 231},
  }};
  for (const auto& [plane, value] : kPlaneValues) {
    fill_plane_full_pitch(frame, value, plane);
  }

  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

std::uint8_t y8_at(const PVideoFrame& frame, int x, int y) {
  return frame->GetReadPtr(PLANAR_Y)[static_cast<std::size_t>(y) * frame->GetPitch(PLANAR_Y) + x];
}

::testing::AssertionResult plane_has_value(const PVideoFrame& frame, int plane,
                                           std::uint8_t expected, const char* operation) {
  for (int y = 0; y < frame->GetHeight(plane); ++y) {
    const auto* row =
        frame->GetReadPtr(plane) + static_cast<std::size_t>(y) * frame->GetPitch(plane);
    for (int x = 0; x < frame->GetRowSize(plane); ++x) {
      if (row[x] != expected) {
        return ::testing::AssertionFailure()
               << operation << " plane=" << plane << " row=" << y << " column=" << x
               << " expected=" << static_cast<int>(expected)
               << " actual=" << static_cast<int>(row[x]);
      }
    }
  }
  return ::testing::AssertionSuccess();
}

void expect_source_unchanged(const StaticVideoSource& source, const char* operation) {
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot)
      << operation << " modified its source";
}

TEST(ExprRelativePixels, MatchesScalarPreviousRowBoundaryWithVectorC) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_y8_row_source(environment);
  const std::vector<PClip> children{source.clip};
  const std::vector<std::string> expressions{"x[0,-1]"};
  Exprfilter scalar(children, expressions, nullptr, false, false, false, false, "none", 0, 0,
                    environment.get());
  Exprfilter vector_c(children, expressions, nullptr, false, false, false, true, "none", 0, 0,
                      environment.get());

  PVideoFrame scalar_output;
  PVideoFrame vector_output;
  ASSERT_NO_THROW(scalar_output = scalar.GetFrame(0, environment.get()));
  ASSERT_NO_THROW(vector_output = vector_c.GetFrame(0, environment.get()));
  ASSERT_NE(scalar_output, nullptr);
  ASSERT_NE(vector_output, nullptr);

  for (int y = 0; y < source.video_info.height; ++y) {
    for (int x = 0; x < source.video_info.width; ++x) {
      const std::uint8_t expected = static_cast<std::uint8_t>(20 + std::max(0, y - 1) * 40 + x);
      ASSERT_EQ(y8_at(scalar_output, x, y), expected)
          << "B10 scalar relative pixel row=" << y << " column=" << x;
      ASSERT_EQ(y8_at(vector_output, x, y), expected)
          << "B10 Vector-C relative pixel row=" << y << " column=" << x;
    }
  }
  EXPECT_NE(scalar_output->CheckMemory(), 1);
  EXPECT_NE(vector_output->CheckMemory(), 1);
  expect_source_unchanged(source, "B10 relative pixel addressing");
}

TEST(ExprFormatOverride, PreservesRgbaPlaneOrderForProcessedYuvaOutput) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_rgbap8_source(environment);
  const std::vector<PClip> children{source.clip};
  const std::vector<std::string> expressions{"x 1 +", "x 1 +", "x 1 +", "x 1 +"};
  Exprfilter filter(children, expressions, "YUVA444P8", false, false, false, false, "none", 0, 0,
                    environment.get());

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_NE(output, nullptr);
  ASSERT_TRUE(plane_has_value(output, PLANAR_Y, 18, "B10 processed RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_U, 62, "B10 processed RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_V, 150, "B10 processed RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_A, 232, "B10 processed RGBA to YUVA"));
  EXPECT_NE(output->CheckMemory(), 1);
  expect_source_unchanged(source, "B10 processed RGBA to YUVA");
}

TEST(ExprFormatOverride, PreservesRgbaPlaneOrderForCopiedYuvaOutput) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_rgbap8_source(environment);
  const std::vector<PClip> children{source.clip};
  const std::vector<std::string> expressions{"", "", "", ""};
  Exprfilter filter(children, expressions, "YUVA444P8", false, false, false, false, "none", 0, 0,
                    environment.get());

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_NE(output, nullptr);
  ASSERT_TRUE(plane_has_value(output, PLANAR_Y, 17, "B10 copied RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_U, 61, "B10 copied RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_V, 149, "B10 copied RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_A, 231, "B10 copied RGBA to YUVA"));
  EXPECT_NE(output->CheckMemory(), 1);
  expect_source_unchanged(source, "B10 copied RGBA to YUVA");
}

TEST(ExprSqrt, PreservesNegativeFloatDomainAcrossScalarAndSse2) {
  AviSynthEnvironment environment;
  if ((environment.get()->GetCPUFlags() & CPUF_SSE2) == 0) {
    GTEST_SKIP() << "B10 Expr SSE2 JIT requires CPUF_SSE2";
  }

  const StaticVideoSource source = make_y32_negative_source(environment);
  const std::vector<PClip> children{source.clip};
  const std::vector<std::string> expressions{"x sqrt"};
  Exprfilter scalar(children, expressions, nullptr, false, false, false, false, "none", 0, 0,
                    environment.get());
  Exprfilter sse2(children, expressions, nullptr, false, false, true, false, "none", 0, 0,
                  environment.get());

  PVideoFrame scalar_output;
  PVideoFrame sse2_output;
  ASSERT_NO_THROW(scalar_output = scalar.GetFrame(0, environment.get()));
  ASSERT_NO_THROW(sse2_output = sse2.GetFrame(0, environment.get()));
  ASSERT_NE(scalar_output, nullptr);
  ASSERT_NE(sse2_output, nullptr);
  const auto* scalar_row = reinterpret_cast<const float*>(scalar_output->GetReadPtr(PLANAR_Y));
  const auto* sse2_row = reinterpret_cast<const float*>(sse2_output->GetReadPtr(PLANAR_Y));
  for (int x = 0; x < source.video_info.width; ++x) {
    ASSERT_TRUE(std::isnan(scalar_row[x])) << "B10 scalar sqrt column=" << x;
    ASSERT_TRUE(std::isnan(sse2_row[x]))
        << "B10 SSE2 sqrt column=" << x << " value=" << sse2_row[x];
  }
  EXPECT_NE(scalar_output->CheckMemory(), 1);
  EXPECT_NE(sse2_output->CheckMemory(), 1);
  expect_source_unchanged(source, "B10 negative sqrt");
}

}  // namespace
}  // namespace avsut::test
