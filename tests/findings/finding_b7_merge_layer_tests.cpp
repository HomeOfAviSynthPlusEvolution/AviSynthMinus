#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B7_UNDEF_AVS_UNUSED
#endif
#include "core/parser/script.h"
#include "filters/layer.h"
#include "filters/merge.h"
#ifdef AVSUT_FINDING_B7_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B7_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <ostream>
#include <string>
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

StaticVideoSource make_yuv_source(AviSynthEnvironment& environment, int pixel_type,
                                  std::uint8_t luma, std::uint8_t chroma_u, std::uint8_t chroma_v,
                                  std::uint8_t alpha = 0) {
  const auto video_info = make_video_info(VideoInfoSpec{8, 3, pixel_type, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, luma, PLANAR_Y);
  fill_plane_full_pitch(frame, chroma_u, PLANAR_U);
  fill_plane_full_pitch(frame, chroma_v, PLANAR_V);
  if (video_info.IsYUVA()) {
    fill_plane_full_pitch(frame, alpha, PLANAR_A);
  }
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

template <typename T>
void fill_active_plane(PVideoFrame& frame, int plane, T value) {
  const int row_size = frame->GetRowSize(plane);
  const int width = row_size / static_cast<int>(sizeof(T));
  for (int y = 0; y < frame->GetHeight(plane); ++y) {
    auto* row = reinterpret_cast<T*>(frame->GetWritePtr(plane) +
                                     static_cast<std::size_t>(y) * frame->GetPitch(plane));
    std::fill(row, row + width, value);
  }
}

StaticVideoSource make_float_rgba_source(AviSynthEnvironment& environment) {
  const auto video_info = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_RGBAPS, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A}) {
    fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0x50 + plane), plane);
  }
  fill_active_plane(frame, PLANAR_G, 0.125F);
  fill_active_plane(frame, PLANAR_B, 0.25F);
  fill_active_plane(frame, PLANAR_R, 0.5F);
  fill_active_plane(frame, PLANAR_A, 1.0F);
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

AVSValue script_generated_nan(IScriptEnvironment* environment) {
  const AVSValue negative_one(-1.0F);
  return Sqrt(AVSValue(&negative_one, 1), nullptr, environment);
}

enum class MergeOperation { All, Luma, Chroma };

struct MergeOperationCase {
  const char* name;
  MergeOperation operation;
};

void PrintTo(const MergeOperationCase& test_case, std::ostream* output) {
  *output << test_case.name;
}

class MergeWeightConstruction : public ::testing::TestWithParam<MergeOperationCase> {};

TEST_P(MergeWeightConstruction, RejectsQuietNanBeforeIntegerWeightConversion) {
  AviSynthEnvironment environment;
  const StaticVideoSource first = make_yuv_source(environment, VideoInfo::CS_YV24, 16, 128, 128);
  const StaticVideoSource second = make_yuv_source(environment, VideoInfo::CS_YV24, 235, 64, 192);
  const auto& test_case = GetParam();
  const float nan = std::numeric_limits<float>::quiet_NaN();

  switch (test_case.operation) {
    case MergeOperation::All:
      EXPECT_THROW(
          { MergeAll filter(first.clip, second.clip, nan, environment.get()); }, AvisynthError);
      break;
    case MergeOperation::Luma:
      EXPECT_THROW(
          { MergeLuma filter(first.clip, second.clip, nan, environment.get()); }, AvisynthError);
      break;
    case MergeOperation::Chroma:
      EXPECT_THROW(
          { MergeChroma filter(first.clip, second.clip, nan, environment.get()); }, AvisynthError);
      break;
  }
  EXPECT_EQ(FrameSnapshot::capture(first.frame, first.video_info), first.snapshot)
      << "B7 Merge case=" << test_case.name << " modified first source";
  EXPECT_EQ(FrameSnapshot::capture(second.frame, second.video_info), second.snapshot)
      << "B7 Merge case=" << test_case.name << " modified second source";
  EXPECT_TRUE(first.clip_impl->frame_requests().empty())
      << "B7 Merge case=" << test_case.name << " requested first source during construction";
  EXPECT_TRUE(second.clip_impl->frame_requests().empty())
      << "B7 Merge case=" << test_case.name << " requested second source during construction";
}

INSTANTIATE_TEST_SUITE_P(B7, MergeWeightConstruction,
                         ::testing::Values(MergeOperationCase{"All", MergeOperation::All},
                                           MergeOperationCase{"Luma", MergeOperation::Luma},
                                           MergeOperationCase{"Chroma", MergeOperation::Chroma}),
                         [](const ::testing::TestParamInfo<MergeOperationCase>& info) {
                           return info.param.name;
                         });

TEST(ResetMaskConstruction, RejectsScriptGeneratedNanMaskAndOpacity) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_float_rgba_source(environment);
  const AVSValue nan = script_generated_nan(environment.get());

  ASSERT_TRUE(std::isnan(nan.AsFloat()));
  EXPECT_THROW(
      { ResetMask filter(source.clip, nan.AsFloat(), environment.get()); }, AvisynthError);
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot)
      << "B7 ResetMask non-finite construction modified its source";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty())
      << "B7 ResetMask non-finite construction requested a frame";
}

TEST(ResetMaskFloat, AppliesFiniteMaskWithoutOpacityOverride) {
  AviSynthEnvironment environment;
  const StaticVideoSource source = make_float_rgba_source(environment);

  ResetMask filter(source.clip, 0.25F, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const int width = output->GetRowSize(PLANAR_A) / static_cast<int>(sizeof(float));
  for (int y = 0; y < output->GetHeight(PLANAR_A); ++y) {
    const auto* row = reinterpret_cast<const float*>(
        output->GetReadPtr(PLANAR_A) + static_cast<std::size_t>(y) * output->GetPitch(PLANAR_A));
    for (int x = 0; x < width; ++x) {
      EXPECT_FLOAT_EQ(row[x], 0.25F) << "B7 ResetMask float x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot);
}

StaticVideoSource make_yuy2_source(AviSynthEnvironment& environment,
                                   const std::array<std::uint8_t, 4>& luma, std::uint8_t chroma_u,
                                   std::uint8_t chroma_v) {
  const auto video_info = make_video_info(VideoInfoSpec{4, 1, VideoInfo::CS_YUY2, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0xa5, DEFAULT_PLANE);
  auto* row = frame->GetWritePtr();
  for (int x = 0; x < 4; ++x) {
    row[2 * x] = luma[static_cast<std::size_t>(x)];
  }
  row[1] = chroma_u;
  row[3] = chroma_v;
  row[5] = chroma_u;
  row[7] = chroma_v;
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

TEST(MergeRgb, ExtractsLumaChannelsFromPackedYuy2Inputs) {
  AviSynthEnvironment environment;
  constexpr std::array<std::uint8_t, 4> kBlue{11, 22, 33, 44};
  constexpr std::array<std::uint8_t, 4> kGreen{55, 66, 77, 88};
  constexpr std::array<std::uint8_t, 4> kRed{99, 111, 123, 135};
  const StaticVideoSource blue = make_yuy2_source(environment, kBlue, 201, 202);
  const StaticVideoSource green = make_yuy2_source(environment, kGreen, 203, 204);
  const StaticVideoSource red = make_yuy2_source(environment, kRed, 205, 206);

  MergeRGB filter(red.clip, blue.clip, green.clip, red.clip, PClip(), "", environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_TRUE(filter.GetVideoInfo().IsRGB32());
  const auto* row = output->GetReadPtr();
  for (int x = 0; x < 4; ++x) {
    EXPECT_EQ(static_cast<int>(row[4 * x]), static_cast<int>(kBlue[static_cast<std::size_t>(x)]))
        << "B7 MergeRGB blue x=" << x;
    EXPECT_EQ(static_cast<int>(row[4 * x + 1]),
              static_cast<int>(kGreen[static_cast<std::size_t>(x)]))
        << "B7 MergeRGB green x=" << x;
    EXPECT_EQ(static_cast<int>(row[4 * x + 2]), static_cast<int>(kRed[static_cast<std::size_t>(x)]))
        << "B7 MergeRGB red x=" << x;
    EXPECT_EQ(static_cast<int>(row[4 * x + 3]), 0) << "B7 MergeRGB alpha x=" << x;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(blue.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(green.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(red.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(blue.frame, blue.video_info), blue.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(green.frame, green.video_info), green.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(red.frame, red.video_info), red.snapshot);
}

TEST(LayerMul, PreservesIntegerFullScaleEndpoint) {
  AviSynthEnvironment environment;
  const StaticVideoSource base = make_yuv_source(environment, VideoInfo::CS_YV24, 255, 128, 128);
  const StaticVideoSource overlay = make_yuv_source(environment, VideoInfo::CS_YV24, 255, 128, 128);

  Layer filter(base.clip, overlay.clip, "Mul", -1, 0, 0, 0, true, 1.0F, 0,
               environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < output->GetHeight(PLANAR_Y); ++y) {
    const auto* row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < output->GetRowSize(PLANAR_Y); ++x) {
      EXPECT_EQ(static_cast<int>(row[x]), 255) << "B7 Layer Mul x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(base.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(overlay.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(base.frame, base.video_info), base.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(overlay.frame, overlay.video_info), overlay.snapshot);
}

TEST(LayerMul, AppliesMaskAtPartialOpacity) {
  AviSynthEnvironment environment;
  const StaticVideoSource base = make_yuv_source(environment, VideoInfo::CS_YUVA444, 200, 128, 128, 255);
  const StaticVideoSource overlay = make_yuv_source(environment, VideoInfo::CS_YUVA444, 100, 128, 128, 0);

  Layer filter(base.clip, overlay.clip, "Mul", -1, 0, 0, 0, true, 0.5F, 0,
               environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < output->GetHeight(PLANAR_Y); ++y) {
    const auto* row = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < output->GetRowSize(PLANAR_Y); ++x) {
      EXPECT_EQ(static_cast<int>(row[x]), 200)
          << "B7 Layer Mul partial opacity with zero mask x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(base.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(overlay.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(base.frame, base.video_info), base.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(overlay.frame, overlay.video_info), overlay.snapshot);
}

struct LayerThresholdCase {
  const char* name;
  int threshold;
};

void PrintTo(const LayerThresholdCase& test_case, std::ostream* output) {
  *output << test_case.name;
}

class LayerThresholdConstruction : public ::testing::TestWithParam<LayerThresholdCase> {};

TEST_P(LayerThresholdConstruction, RejectsValuesOutsideEightBitDomain) {
  AviSynthEnvironment environment;
  const StaticVideoSource base = make_yuv_source(environment, VideoInfo::CS_YV24, 16, 128, 128);
  const StaticVideoSource overlay = make_yuv_source(environment, VideoInfo::CS_YV24, 235, 64, 192);
  const auto& test_case = GetParam();

  EXPECT_THROW(
      {
        Layer filter(base.clip, overlay.clip, "Lighten", -1, 0, 0, test_case.threshold,
                     true, 1.0F, 0, environment.get());
      },
      AvisynthError)
      << "B7 Layer threshold=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(base.frame, base.video_info), base.snapshot)
      << "B7 Layer threshold=" << test_case.name << " modified base source";
  EXPECT_EQ(FrameSnapshot::capture(overlay.frame, overlay.video_info), overlay.snapshot)
      << "B7 Layer threshold=" << test_case.name << " modified overlay source";
  EXPECT_TRUE(base.clip_impl->frame_requests().empty())
      << "B7 Layer threshold=" << test_case.name << " requested base during construction";
  EXPECT_TRUE(overlay.clip_impl->frame_requests().empty())
      << "B7 Layer threshold=" << test_case.name << " requested overlay during construction";
}

INSTANTIATE_TEST_SUITE_P(B7, LayerThresholdConstruction,
                         ::testing::Values(LayerThresholdCase{"NegativeOne", -1},
                                           LayerThresholdCase{"AboveByteRange", 256}),
                         [](const ::testing::TestParamInfo<LayerThresholdCase>& info) {
                           return info.param.name;
                         });

class MaskRgb32NarrowRows : public ::testing::TestWithParam<int> {};

TEST_P(MaskRgb32NarrowRows, HandlesRowsShorterThanOneVector) {
  const int width = GetParam();
  EXPECT_EXIT(
      {
        AviSynthEnvironment environment;
        const auto video_info =
            make_video_info(VideoInfoSpec{width, 1, VideoInfo::CS_BGR32, 1, 25, 1});
        PVideoFrame source_frame = environment.get()->NewVideoFrame(video_info);
        PVideoFrame mask_frame = environment.get()->NewVideoFrame(video_info);
        fill_plane_full_pitch(source_frame, 0x5a, DEFAULT_PLANE);
        fill_plane_full_pitch(mask_frame, 0xa5, DEFAULT_PLANE);
        auto* source_row = source_frame->GetWritePtr();
        auto* mask_row = mask_frame->GetWritePtr();
        for (int x = 0; x < width; ++x) {
          source_row[4 * x] = static_cast<std::uint8_t>(10 + x);
          source_row[4 * x + 1] = static_cast<std::uint8_t>(20 + x);
          source_row[4 * x + 2] = static_cast<std::uint8_t>(30 + x);
          source_row[4 * x + 3] = 0;
          mask_row[4 * x] = static_cast<std::uint8_t>(100 + x);
          mask_row[4 * x + 1] = static_cast<std::uint8_t>(110 + x);
          mask_row[4 * x + 2] = static_cast<std::uint8_t>(120 + x);
          mask_row[4 * x + 3] = 0;
        }
        const PClip source(new StaticFrameClip(video_info, source_frame));
        const PClip mask(new StaticFrameClip(video_info, mask_frame));
        Mask filter(source, mask, environment.get());
        const PVideoFrame output = filter.GetFrame(0, environment.get());
        bool output_is_expected = output->CheckMemory() != 1;
        const auto* output_row = output->GetReadPtr();
        for (int x = 0; x < width; ++x) {
          const int expected_alpha = (3736 * mask_row[4 * x] + 19234 * mask_row[4 * x + 1] +
                                      9798 * mask_row[4 * x + 2] + 16384) >>
                                     15;
          output_is_expected = output_is_expected && output_row[4 * x] == source_row[4 * x] &&
                               output_row[4 * x + 1] == source_row[4 * x + 1] &&
                               output_row[4 * x + 2] == source_row[4 * x + 2] &&
                               output_row[4 * x + 3] == expected_alpha;
        }
        std::_Exit(output_is_expected ? EXIT_SUCCESS : EXIT_FAILURE);
      },
      ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

INSTANTIATE_TEST_SUITE_P(B7, MaskRgb32NarrowRows, ::testing::Values(1, 2, 3),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return "Width" + std::to_string(info.param);
                         });

PVideoFrame make_yuv_frame(AviSynthEnvironment& environment, const VideoInfo& video_info,
                           std::uint8_t luma) {
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, luma, PLANAR_Y);
  fill_plane_full_pitch(frame, 128, PLANAR_U);
  fill_plane_full_pitch(frame, 128, PLANAR_V);
  return frame;
}

TEST(SubtractFilter, RepeatsShorterVideoChildAtTheTail) {
  AviSynthEnvironment environment;
  const auto short_video_info = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_YV24, 1, 25, 1});
  const auto long_video_info = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_YV24, 2, 25, 1});
  PVideoFrame short_frame = make_yuv_frame(environment, short_video_info, 16);
  PVideoFrame long_frame_zero = make_yuv_frame(environment, long_video_info, 64);
  PVideoFrame long_frame_one = make_yuv_frame(environment, long_video_info, 235);
  const FrameSnapshot short_snapshot = FrameSnapshot::capture(short_frame, short_video_info);
  const FrameSnapshot long_zero_snapshot = FrameSnapshot::capture(long_frame_zero, long_video_info);
  const FrameSnapshot long_one_snapshot = FrameSnapshot::capture(long_frame_one, long_video_info);
  auto* short_clip_impl = new FrameSequenceClip(short_video_info, {short_frame});
  auto* long_clip_impl = new FrameSequenceClip(long_video_info, {long_frame_zero, long_frame_one});
  const PClip short_clip(short_clip_impl);
  const PClip long_clip(long_clip_impl);

  Subtract filter(short_clip, long_clip, environment.get());
  ASSERT_EQ(filter.GetVideoInfo().num_frames, 2);
  PVideoFrame output;
  ASSERT_NO_THROW(output = filter.GetFrame(1, environment.get()));
  ASSERT_TRUE(output);
  EXPECT_NE(output->CheckMemory(), 1);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const std::uint8_t expected = plane == PLANAR_Y ? 0 : 128;
    for (int y = 0; y < output->GetHeight(plane); ++y) {
      const auto* row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int x = 0; x < output->GetRowSize(plane); ++x) {
        EXPECT_EQ(row[x], expected) << "B7 Subtract plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_EQ(short_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(long_clip_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(FrameSnapshot::capture(short_frame, short_video_info), short_snapshot);
  EXPECT_EQ(FrameSnapshot::capture(long_frame_zero, long_video_info), long_zero_snapshot);
  EXPECT_EQ(FrameSnapshot::capture(long_frame_one, long_video_info), long_one_snapshot);
}

}  // namespace
}  // namespace avsut::test
