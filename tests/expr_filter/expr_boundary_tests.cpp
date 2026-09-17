#include <gtest/gtest.h>

#include <avisynth.h>

#include "iris_test_support.h"

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
  StaticFrameClip *clip_impl;
  PClip clip;
  FrameSnapshot snapshot;
};

StaticVideoSource make_y8_row_source(AviSynthEnvironment &environment) {
  const VideoInfo video_info =
      make_video_info(VideoInfoSpec{19, 4, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0xa5, PLANAR_Y);
  for (int y = 0; y < video_info.height; ++y) {
    auto *row = frame->GetWritePtr(PLANAR_Y) +
                static_cast<std::size_t>(y) * frame->GetPitch(PLANAR_Y);
    for (int x = 0; x < video_info.width; ++x) {
      row[x] = static_cast<std::uint8_t>(20 + y * 40 + x);
    }
  }

  auto *clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

StaticVideoSource make_y32_negative_source(AviSynthEnvironment &environment) {
  const VideoInfo video_info =
      make_video_info(VideoInfoSpec{8, 1, VideoInfo::CS_Y32, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0xa5, PLANAR_Y);
  auto *row = reinterpret_cast<float *>(frame->GetWritePtr(PLANAR_Y));
  constexpr std::array<float, 8> kSamples{
      -1.0F, -0.75F, -0.5F, -0.25F, -0.125F, -0.0625F, -0.03125F, -0.015625F};
  for (int x = 0; x < video_info.width; ++x) {
    row[x] = kSamples[static_cast<std::size_t>(x)];
  }

  auto *clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

StaticVideoSource make_rgbap8_source(AviSynthEnvironment &environment) {
  const VideoInfo video_info =
      make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_RGBAP8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  constexpr std::array<std::pair<int, std::uint8_t>, 4> kPlaneValues{{
      {PLANAR_R, 17},
      {PLANAR_G, 61},
      {PLANAR_B, 149},
      {PLANAR_A, 231},
  }};
  for (const auto &[plane, value] : kPlaneValues) {
    fill_plane_full_pitch(frame, value, plane);
  }

  auto *clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

std::uint8_t y8_at(const PVideoFrame &frame, int x, int y) {
  return frame->GetReadPtr(
      PLANAR_Y)[static_cast<std::size_t>(y) * frame->GetPitch(PLANAR_Y) + x];
}

::testing::AssertionResult plane_has_value(const PVideoFrame &frame, int plane,
                                           std::uint8_t expected,
                                           const char *operation) {
  for (int y = 0; y < frame->GetHeight(plane); ++y) {
    const auto *row = frame->GetReadPtr(plane) +
                      static_cast<std::size_t>(y) * frame->GetPitch(plane);
    for (int x = 0; x < frame->GetRowSize(plane); ++x) {
      if (row[x] != expected) {
        return ::testing::AssertionFailure()
               << operation << " plane=" << plane << " row=" << y
               << " column=" << x << " expected=" << static_cast<int>(expected)
               << " actual=" << static_cast<int>(row[x]);
      }
    }
  }
  return ::testing::AssertionSuccess();
}

void expect_source_unchanged(const StaticVideoSource &source,
                             const char *operation) {
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info),
            source.snapshot)
      << operation << " modified its source";
}

class ExprBoundary : public ::testing::TestWithParam<IrisTestBackend> {};

TEST_P(ExprBoundary, ClampsPreviousRowBoundary) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_y8_row_source(environment);
  const std::vector<PClip> children{source.clip};
  const std::vector<std::string> expressions{"x[0,-1]"};
  PClip filter = make_iris_test_filter(children, expressions, GetParam(), 0,
                                       nullptr, environment.get());

  PVideoFrame output;
  ASSERT_NO_THROW(output = filter->GetFrame(0, environment.get()));
  ASSERT_NE(output, nullptr);

  for (int y = 0; y < source.video_info.height; ++y) {
    for (int x = 0; x < source.video_info.width; ++x) {
      const std::uint8_t expected =
          static_cast<std::uint8_t>(20 + std::max(0, y - 1) * 40 + x);
      ASSERT_EQ(y8_at(output, x, y), expected)
          << "relative pixel row=" << y << " column=" << x;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  expect_source_unchanged(source, "relative pixel addressing");
}

TEST_P(ExprBoundary, PreservesRgbaPlaneOrderForProcessedYuvaOutput) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_rgbap8_source(environment);
  const std::vector<PClip> children{source.clip};
  const std::vector<std::string> expressions{"x 1 +", "x 1 +", "x 1 +",
                                             "x 1 +"};
  PClip filter = make_iris_test_filter(children, expressions, GetParam(), 0,
                                       "YUVA444P8", environment.get());

  const PVideoFrame output = filter->GetFrame(0, environment.get());
  ASSERT_NE(output, nullptr);
  ASSERT_TRUE(plane_has_value(output, PLANAR_Y, 18, "processed RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_U, 62, "processed RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_V, 150, "processed RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_A, 232, "processed RGBA to YUVA"));
  EXPECT_NE(output->CheckMemory(), 1);
  expect_source_unchanged(source, "processed RGBA to YUVA");
}

TEST_P(ExprBoundary, PreservesRgbaPlaneOrderForCopiedYuvaOutput) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_rgbap8_source(environment);
  const std::vector<PClip> children{source.clip};
  const std::vector<std::string> expressions{"", "", "", ""};
  PClip filter = make_iris_test_filter(children, expressions, GetParam(), 0,
                                       "YUVA444P8", environment.get());

  const PVideoFrame output = filter->GetFrame(0, environment.get());
  ASSERT_NE(output, nullptr);
  ASSERT_TRUE(plane_has_value(output, PLANAR_Y, 17, "copied RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_U, 61, "copied RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_V, 149, "copied RGBA to YUVA"));
  ASSERT_TRUE(plane_has_value(output, PLANAR_A, 231, "copied RGBA to YUVA"));
  EXPECT_NE(output->CheckMemory(), 1);
  expect_source_unchanged(source, "copied RGBA to YUVA");
}

TEST_P(ExprBoundary, ConstantNegativeInputMatchesRuntimeClamp) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_y32_negative_source(environment);
  const std::vector<PClip> children{source.clip};
  const std::vector<std::string> expressions{"-1 sqrt"};
  PClip filter = make_iris_test_filter(children, expressions, GetParam(), 0,
                                       nullptr, environment.get());
  const PVideoFrame output = filter->GetFrame(0, environment.get());
  const auto *row =
      reinterpret_cast<const float *>(output->GetReadPtr(PLANAR_Y));
  for (int x = 0; x < source.video_info.width; ++x)
    EXPECT_EQ(row[x], 0.0F) << "constant sqrt column=" << x;
  expect_source_unchanged(source, "constant negative sqrt");
}

TEST_P(ExprBoundary, ClampsNegativeFloatInput) {
  AviSynthEnvironment environment;

  const StaticVideoSource source = make_y32_negative_source(environment);
  const std::vector<PClip> children{source.clip};
  const std::vector<std::string> expressions{"x sqrt"};
  PClip filter = make_iris_test_filter(children, expressions, GetParam(), 0,
                                       nullptr, environment.get());

  PVideoFrame output;
  ASSERT_NO_THROW(output = filter->GetFrame(0, environment.get()));
  ASSERT_NE(output, nullptr);
  const auto *row =
      reinterpret_cast<const float *>(output->GetReadPtr(PLANAR_Y));
  for (int x = 0; x < source.video_info.width; ++x) {
    ASSERT_EQ(row[x], 0.0F) << "sqrt column=" << x;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  expect_source_unchanged(source, "negative sqrt");
}

INSTANTIATE_TEST_SUITE_P(
    Backends, ExprBoundary, ::testing::ValuesIn(iris_test_backends()),
    [](const ::testing::TestParamInfo<IrisTestBackend> &info) {
      return info.param.name;
    });

} // namespace
} // namespace avsut::test
