#include <cstdint>
#include <string>
#include <vector>

#include <avisynth.h>

#ifdef AVS_WINDOWS
#include <avs/win.h>
#define AVSUT_EXPR_FILTER_WINDOWS_CONTEXT
#define VS_TARGET_OS_WINDOWS
#endif

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_EXPR_FILTER_UNDEF_AVS_UNUSED
#endif
#include "filters/exprfilter/exprfilter.h"
#ifdef AVSUT_EXPR_FILTER_WINDOWS_CONTEXT
#undef VS_TARGET_OS_WINDOWS
#undef AVSUT_EXPR_FILTER_WINDOWS_CONTEXT
#endif
#ifdef AVSUT_EXPR_FILTER_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_EXPR_FILTER_UNDEF_AVS_UNUSED
#endif

#include "support/deterministic_data.h"
#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <ostream>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::fill_random;
using avsut::test::FrameSequenceClip;
using avsut::test::FrameSnapshot;
using avsut::test::frame_plane_geometry;
using avsut::test::make_video_info;
using avsut::test::PlaneView;
using avsut::test::video_frame_planes;
using avsut::test::VideoInfoSpec;

struct ExprExecutionVariant {
  const char* name;
  bool opt_avx2;
  bool opt_sse2;
  bool opt_vector_c;
  int64_t required_cpu_flag;
};

void PrintTo(const ExprExecutionVariant& variant, std::ostream* stream) {
  *stream << variant.name;
}

constexpr std::uint32_t kYuvSeed = 0xF30E5801U;
constexpr std::uint32_t kRgbSeed = 0xF30E5802U;
constexpr std::uint32_t kLutSeed = 0xF30E5803U;

template <typename Pixel>
void fill_seeded_frame(PVideoFrame& frame, const VideoInfo& video_info, std::uint32_t seed,
                       std::uint32_t frame_salt) {
  for (const int plane : video_frame_planes(video_info)) {
    fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0xa5 + plane), plane);
    const auto geometry = frame_plane_geometry(frame, plane, sizeof(Pixel));
    PlaneView<Pixel> view(reinterpret_cast<Pixel*>(frame->GetWritePtr(plane)),
                          static_cast<std::size_t>(geometry.width),
                          static_cast<std::size_t>(geometry.height),
                          static_cast<std::size_t>(geometry.pitch));
    fill_random(view, seed ^ frame_salt ^ static_cast<std::uint32_t>(plane) * 0x9e3779b9U);
  }
}

std::vector<PVideoFrame> make_seeded_yuv_frames(AviSynthEnvironment& environment,
                                                const VideoInfo& video_info) {
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < video_info.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
    fill_seeded_frame<std::uint8_t>(frame, video_info, kYuvSeed,
                                    static_cast<std::uint32_t>(frame_index) * 0x01010101U);
    frames.push_back(frame);
  }
  return frames;
}

std::vector<PVideoFrame> make_seeded_rgb_frames(AviSynthEnvironment& environment,
                                                const VideoInfo& video_info) {
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < video_info.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
    fill_seeded_frame<std::uint16_t>(frame, video_info, kRgbSeed,
                                     static_cast<std::uint32_t>(frame_index) * 0x01010101U);
    frames.push_back(frame);
  }
  return frames;
}

std::vector<PVideoFrame> make_seeded_lut_frames(AviSynthEnvironment& environment,
                                                const VideoInfo& video_info) {
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < video_info.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
    fill_seeded_frame<std::uint8_t>(frame, video_info, kLutSeed,
                                    static_cast<std::uint32_t>(frame_index) * 0x01010101U);
    frames.push_back(frame);
  }
  return frames;
}

void skip_unavailable_variant(const ExprExecutionVariant& variant, IScriptEnvironment* environment) {
  if (variant.required_cpu_flag != 0 &&
      (environment->GetCPUFlags() & variant.required_cpu_flag) == 0) {
    GTEST_SKIP() << "Expr variant " << variant.name << " requires CPU flag 0x"
                 << std::hex << variant.required_cpu_flag;
  }
}

Exprfilter make_filter(const PClip& clip, const std::vector<std::string>& expressions,
                       const ExprExecutionVariant& variant, int lutmode,
                       IScriptEnvironment* environment) {
  return Exprfilter(std::vector<PClip>{clip}, expressions, nullptr, variant.opt_avx2,
                    false, variant.opt_sse2, variant.opt_vector_c, "none", 0, lutmode,
                    environment);
}

class ExprYuvVariantTest : public ::testing::TestWithParam<ExprExecutionVariant> {};

TEST_P(ExprYuvVariantTest, EvaluatesSeededYuvPlanesAcrossTailWidth) {
  const auto& variant = GetParam();
  AviSynthEnvironment environment;
  skip_unavailable_variant(variant, environment.get());

  constexpr int width = 23;
  constexpr int height = 5;
  const auto video_info = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_YV24, 3, 25, 1});
  auto frames = make_seeded_yuv_frames(environment, video_info);
  const std::vector<FrameSnapshot> source_snapshots = {
      FrameSnapshot::capture(frames[0], video_info),
      FrameSnapshot::capture(frames[1], video_info),
      FrameSnapshot::capture(frames[2], video_info)};
  auto* source_impl = new FrameSequenceClip(video_info, frames);
  const PClip source(source_impl);

  const std::vector<std::string> expressions{"x sx +", "x sy +", "x sx sy + +"};
  Exprfilter filter = make_filter(source, expressions, variant, 0, environment.get());
  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, width);
  ASSERT_EQ(filter.GetVideoInfo().height, height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(1, environment.get());
  ASSERT_NE(output, nullptr);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    for (int y = 0; y < height; ++y) {
      const auto* source_row = frames[1]->GetReadPtr(plane) + y * frames[1]->GetPitch(plane);
      const auto* output_row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int x = 0; x < width; ++x) {
        const int coordinate_term = plane == PLANAR_Y ? x : plane == PLANAR_U ? y : x + y;
        const auto expected = static_cast<std::uint8_t>(std::min(255, source_row[x] + coordinate_term));
        EXPECT_EQ(output_row[x], expected)
            << "variant=" << variant.name << " plane=" << plane << " x=" << x
            << " y=" << y << " seed=0x" << std::hex << kYuvSeed;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(FrameSnapshot::capture(frames[0], video_info), source_snapshots[0]);
  EXPECT_EQ(FrameSnapshot::capture(frames[1], video_info), source_snapshots[1]);
  EXPECT_EQ(FrameSnapshot::capture(frames[2], video_info), source_snapshots[2]);
}

INSTANTIATE_TEST_SUITE_P(
    FixedSeedVariants, ExprYuvVariantTest,
    ::testing::Values(
        ExprExecutionVariant{"Scalar_Width23_Height5_SeedF30E5801", false, false, false, 0},
        ExprExecutionVariant{"VectorC_Width23_Height5_SeedF30E5801", false, false, true, 0},
        ExprExecutionVariant{"Sse2_Width23_Height5_SeedF30E5801", false, true, false, CPUF_SSE2},
        ExprExecutionVariant{"Avx2_Width23_Height5_SeedF30E5801", true, true, false, CPUF_AVX2}),
    [](const ::testing::TestParamInfo<ExprExecutionVariant>& info) { return info.param.name; });

class ExprRgbVariantTest : public ::testing::TestWithParam<ExprExecutionVariant> {};

TEST_P(ExprRgbVariantTest, PreservesPlanarGbrOrderAndCopiesAlphaAtSixteenBits) {
  const auto& variant = GetParam();
  AviSynthEnvironment environment;
  skip_unavailable_variant(variant, environment.get());

  constexpr int width = 17;
  constexpr int height = 3;
  const auto video_info = make_video_info(
      VideoInfoSpec{width, height, VideoInfo::CS_RGBAP16, 1, 25, 1});
  auto frames = make_seeded_rgb_frames(environment, video_info);
  const FrameSnapshot source_snapshot = FrameSnapshot::capture(frames[0], video_info);
  auto* source_impl = new FrameSequenceClip(video_info, frames);
  const PClip source(source_impl);

  const std::vector<std::string> expressions{"x 101 +", "x 203 +", "x 307 +"};
  Exprfilter filter = make_filter(source, expressions, variant, 0, environment.get());
  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_RGBAP16);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_NE(output, nullptr);
  for (int y = 0; y < height; ++y) {
    for (const int plane : {PLANAR_R, PLANAR_G, PLANAR_B}) {
      const auto* source_row = reinterpret_cast<const std::uint16_t*>(
          frames[0]->GetReadPtr(plane) + y * frames[0]->GetPitch(plane));
      const auto* output_row = reinterpret_cast<const std::uint16_t*>(
          output->GetReadPtr(plane) + y * output->GetPitch(plane));
      const std::uint16_t bias = plane == PLANAR_R ? 101 : plane == PLANAR_G ? 203 : 307;
      for (int x = 0; x < width; ++x) {
        const auto expected = static_cast<std::uint16_t>(
            std::min<std::uint32_t>(65535U, static_cast<std::uint32_t>(source_row[x]) + bias));
        EXPECT_EQ(output_row[x], expected)
            << "variant=" << variant.name << " plane=" << plane << " x=" << x
            << " y=" << y << " seed=0x" << std::hex << kRgbSeed;
      }
    }
    const auto* source_alpha = reinterpret_cast<const std::uint16_t*>(
        frames[0]->GetReadPtr(PLANAR_A) + y * frames[0]->GetPitch(PLANAR_A));
    const auto* output_alpha = reinterpret_cast<const std::uint16_t*>(
        output->GetReadPtr(PLANAR_A) + y * output->GetPitch(PLANAR_A));
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(output_alpha[x], source_alpha[x])
          << "variant=" << variant.name << " alpha x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(frames[0], video_info), source_snapshot);
}

INSTANTIATE_TEST_SUITE_P(
    FixedSeedVariants, ExprRgbVariantTest,
    ::testing::Values(
        ExprExecutionVariant{"Scalar_Width17_Height3_SeedF30E5802", false, false, false, 0},
        ExprExecutionVariant{"VectorC_Width17_Height3_SeedF30E5802", false, false, true, 0},
        ExprExecutionVariant{"Sse2_Width17_Height3_SeedF30E5802", false, true, false, CPUF_SSE2},
        ExprExecutionVariant{"Avx2_Width17_Height3_SeedF30E5802", true, true, false, CPUF_AVX2}),
    [](const ::testing::TestParamInfo<ExprExecutionVariant>& info) { return info.param.name; });

TEST(ExprLut, BuildsOneDimensionalTableAndUsesRequestedFrame) {
  AviSynthEnvironment environment;
  const auto video_info = make_video_info(
      VideoInfoSpec{19, 2, VideoInfo::CS_Y8, 2, 25, 1});
  auto frames = make_seeded_lut_frames(environment, video_info);
  const FrameSnapshot first_snapshot = FrameSnapshot::capture(frames[0], video_info);
  const FrameSnapshot second_snapshot = FrameSnapshot::capture(frames[1], video_info);
  auto* source_impl = new FrameSequenceClip(video_info, frames);
  const PClip source(source_impl);

  const std::vector<std::string> expressions{"255 x -"};
  const ExprExecutionVariant vector_c{"VectorC", false, false, true, 0};
  Exprfilter filter = make_filter(source, expressions, vector_c, 1, environment.get());
  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_Y8);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  EXPECT_TRUE(source_impl->frame_requests().empty())
      << "lut initialization should not request a source frame";

  const PVideoFrame output = filter.GetFrame(1, environment.get());
  ASSERT_NE(output, nullptr);
  for (int y = 0; y < video_info.height; ++y) {
    const auto* source_row = frames[1]->GetReadPtr(PLANAR_Y) + y * frames[1]->GetPitch(PLANAR_Y);
    const auto* output_row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < video_info.width; ++x) {
      EXPECT_EQ(output_row[x], static_cast<std::uint8_t>(255 - source_row[x]))
          << "lut_x x=" << x << " y=" << y << " seed=0x" << std::hex << kLutSeed;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(FrameSnapshot::capture(frames[0], video_info), first_snapshot);
  EXPECT_EQ(FrameSnapshot::capture(frames[1], video_info), second_snapshot);
}

}  // namespace
