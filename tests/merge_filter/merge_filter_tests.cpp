#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_MERGE_UNDEF_AVS_UNUSED
#endif
#include "filters/merge.h"
#ifdef AVSUT_MERGE_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_MERGE_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_helper.h"

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSequenceClip;
using avsut::test::FrameSnapshot;
using avsut::test::get_frame_property_int;
using avsut::test::make_video_info;
using avsut::test::set_frame_property_int;
using avsut::test::video_frame_planes;
using avsut::test::VideoInfoSpec;
using avsut::test::write_frame_plane;

enum class MergeOperation { All, Luma, Chroma };

struct MergeFilterCase {
  MergeOperation operation;
  float weight;
  const char* name;
};

void PrintTo(const MergeFilterCase& test_case, std::ostream* stream) { *stream << test_case.name; }

std::vector<PVideoFrame> make_frames(AviSynthEnvironment& environment, const VideoInfo& vi,
                                     std::uint8_t base) {
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < 2; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
      fill_plane_full_pitch(frame, static_cast<std::uint8_t>(base + plane * 23 + frame_index * 7),
                            plane);
      const int width = frame->GetRowSize(plane);
      const int height = frame->GetHeight(plane);
      const int pitch = frame->GetPitch(plane);
      for (int y = 0; y < height; ++y) {
        auto* row = frame->GetWritePtr(plane) + y * pitch;
        for (int x = 0; x < width; ++x) {
          row[x] = static_cast<std::uint8_t>(base + plane * 23 + frame_index * 7 + x * 11 + y * 19);
        }
      }
    }
    frames.push_back(frame);
  }
  return frames;
}

std::vector<FrameSnapshot> snapshot_frames(const std::vector<PVideoFrame>& frames,
                                           const VideoInfo& vi) {
  std::vector<FrameSnapshot> snapshots;
  for (const auto& frame : frames) {
    snapshots.push_back(FrameSnapshot::capture(frame, vi));
  }
  return snapshots;
}

std::uint8_t weighted_sample(std::uint8_t first, std::uint8_t second, float weight) {
  const int weight_i = static_cast<int>(weight * 32768.0f + 0.5f);
  const int inverse_weight = 32768 - weight_i;
  return static_cast<std::uint8_t>(
      (static_cast<int>(first) * inverse_weight + static_cast<int>(second) * weight_i + 16384) >>
      15);
}

void expect_plane(const PVideoFrame& first, const PVideoFrame& second, const PVideoFrame& output,
                  int plane, bool merge, float weight) {
  ASSERT_EQ(output->GetRowSize(plane), first->GetRowSize(plane));
  ASSERT_EQ(output->GetHeight(plane), first->GetHeight(plane));
  const int first_pitch = first->GetPitch(plane);
  const int second_pitch = second->GetPitch(plane);
  const int output_pitch = output->GetPitch(plane);
  const int width = first->GetRowSize(plane);
  const int height = first->GetHeight(plane);
  for (int y = 0; y < height; ++y) {
    const auto* first_row = first->GetReadPtr(plane) + y * first_pitch;
    const auto* second_row = second->GetReadPtr(plane) + y * second_pitch;
    const auto* output_row = output->GetReadPtr(plane) + y * output_pitch;
    for (int x = 0; x < width; ++x) {
      const auto expected =
          merge ? weighted_sample(first_row[x], second_row[x], weight) : first_row[x];
      EXPECT_EQ(output_row[x], expected) << "plane=" << plane << " x=" << x << " y=" << y;
    }
  }
}

struct MergeFormatCase {
  int pixel_type;
  int width;
  int height;
  MergeOperation operation;
  float weight;
  const char* name;
};

void PrintTo(const MergeFormatCase& test_case, std::ostream* stream) { *stream << test_case.name; }

std::vector<PVideoFrame> make_format_frames(AviSynthEnvironment& environment, const VideoInfo& vi,
                                            std::uint8_t base) {
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < vi.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : video_frame_planes(vi)) {
      fill_plane_full_pitch(frame,
                            static_cast<std::uint8_t>(base + plane * 19 + frame_index * 31),
                            plane);
      write_frame_plane<std::uint8_t>(frame, plane, [base, plane, frame_index](int x, int y) {
        return static_cast<std::uint8_t>(base + plane * 19 + frame_index * 31 + x * 7 + y * 13);
      });
    }
    frames.push_back(frame);
  }
  return frames;
}

bool merge_format_plane(const MergeFormatCase& test_case, const VideoInfo& vi, int plane) {
  switch (test_case.operation) {
    case MergeOperation::All:
      return true;
    case MergeOperation::Luma:
      return plane == PLANAR_Y;
    case MergeOperation::Chroma:
      return plane == PLANAR_U || plane == PLANAR_V || (vi.IsYUVA() && plane == PLANAR_A);
  }
  return false;
}

PVideoFrame run_format_filter(const MergeFormatCase& test_case, PClip child, PClip other,
                              IScriptEnvironment* environment) {
  switch (test_case.operation) {
    case MergeOperation::All: {
      MergeAll filter(child, other, test_case.weight, environment);
      EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
      return filter.GetFrame(1, environment);
    }
    case MergeOperation::Luma: {
      MergeLuma filter(child, other, test_case.weight, environment);
      EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
      return filter.GetFrame(1, environment);
    }
    case MergeOperation::Chroma: {
      MergeChroma filter(child, other, test_case.weight, environment);
      EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
      return filter.GetFrame(1, environment);
    }
  }
  throw std::logic_error("unknown merge format operation");
}

PVideoFrame run_filter(const MergeFilterCase& test_case, PClip child, PClip other,
                       IScriptEnvironment* environment) {
  switch (test_case.operation) {
    case MergeOperation::All: {
      MergeAll filter(child, other, test_case.weight, environment);
      return filter.GetFrame(1, environment);
    }
    case MergeOperation::Luma: {
      MergeLuma filter(child, other, test_case.weight, environment);
      return filter.GetFrame(1, environment);
    }
    case MergeOperation::Chroma: {
      MergeChroma filter(child, other, test_case.weight, environment);
      return filter.GetFrame(1, environment);
    }
  }
  throw std::logic_error("unknown merge operation");
}

class MergeFilterTest : public ::testing::TestWithParam<MergeFilterCase> {};

TEST_P(MergeFilterTest, MergesSelectedYuv444PlanesAndRequestsMatchingFrame) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 2, 25, 1});
  auto first_frames = make_frames(environment, vi, 13);
  auto second_frames = make_frames(environment, vi, 157);
  const auto first_snapshots = snapshot_frames(first_frames, vi);
  const auto second_snapshots = snapshot_frames(second_frames, vi);
  auto* first_clip = new FrameSequenceClip(vi, first_frames);
  auto* second_clip = new FrameSequenceClip(vi, second_frames);
  const PClip child(first_clip);
  const PClip other(second_clip);

  const PVideoFrame output = run_filter(test_case, child, other, environment.get());
  const bool merge_luma =
      test_case.operation == MergeOperation::All || test_case.operation == MergeOperation::Luma;
  const bool merge_chroma =
      test_case.operation == MergeOperation::All || test_case.operation == MergeOperation::Chroma;
  expect_plane(first_frames[1], second_frames[1], output, PLANAR_Y, merge_luma, test_case.weight);
  expect_plane(first_frames[1], second_frames[1], output, PLANAR_U, merge_chroma, test_case.weight);
  expect_plane(first_frames[1], second_frames[1], output, PLANAR_V, merge_chroma, test_case.weight);

  EXPECT_NE(output->CheckMemory(), 1);
  const std::vector<int> expected_first_requests =
      test_case.operation == MergeOperation::All && test_case.weight == 1.0f ? std::vector<int>{}
                                                                             : std::vector<int>{1};
  EXPECT_EQ(first_clip->frame_requests(), expected_first_requests);
  EXPECT_EQ(second_clip->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(snapshot_frames(first_frames, vi), first_snapshots);
  EXPECT_EQ(snapshot_frames(second_frames, vi), second_snapshots);
}

TEST(MergeFilter, ZeroWeightDoesNotRequestSecondClip) {
  const MergeFilterCase test_case{MergeOperation::All, 0.0f, "All_Weight0"};
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{5, 3, VideoInfo::CS_YV24, 2, 25, 1});
  auto first_frames = make_frames(environment, vi, 13);
  auto second_frames = make_frames(environment, vi, 157);
  const auto first_snapshots = snapshot_frames(first_frames, vi);
  const auto second_snapshots = snapshot_frames(second_frames, vi);
  auto* first_clip = new FrameSequenceClip(vi, first_frames);
  auto* second_clip = new FrameSequenceClip(vi, second_frames);
  const PClip child(first_clip);
  const PClip other(second_clip);

  const PVideoFrame output = run_filter(test_case, child, other, environment.get());
  expect_plane(first_frames[1], second_frames[1], output, PLANAR_Y, false, test_case.weight);
  expect_plane(first_frames[1], second_frames[1], output, PLANAR_U, false, test_case.weight);
  expect_plane(first_frames[1], second_frames[1], output, PLANAR_V, false, test_case.weight);
  EXPECT_EQ(first_clip->frame_requests(), std::vector<int>{1});
  EXPECT_TRUE(second_clip->frame_requests().empty());
  EXPECT_EQ(snapshot_frames(first_frames, vi), first_snapshots);
  EXPECT_EQ(snapshot_frames(second_frames, vi), second_snapshots);
}

TEST(MergeFilter, UsesBaseFramePropertiesForWeightedOutput) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 4, VideoInfo::CS_YV12, 2, 25, 1});
  auto first_frames = make_frames(environment, vi, 13);
  auto second_frames = make_frames(environment, vi, 157);
  set_frame_property_int(environment.get(), first_frames[1], "_ChromaLocation", AVS_CHROMA_LEFT);
  set_frame_property_int(environment.get(), first_frames[1], "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), first_frames[1], "_FieldBased", 1);
  set_frame_property_int(environment.get(), second_frames[1], "_ChromaLocation", AVS_CHROMA_CENTER);
  set_frame_property_int(environment.get(), second_frames[1], "_ColorRange",
                         AVS_COLORRANGE_LIMITED);
  set_frame_property_int(environment.get(), second_frames[1], "_FieldBased", 0);
  const auto first_snapshots = snapshot_frames(first_frames, vi);
  const auto second_snapshots = snapshot_frames(second_frames, vi);
  auto* first_clip = new FrameSequenceClip(vi, first_frames);
  auto* second_clip = new FrameSequenceClip(vi, second_frames);
  const PClip child(first_clip);
  const PClip other(second_clip);

  MergeAll filter(child, other, 0.25f, environment.get());
  const PVideoFrame output = filter.GetFrame(1, environment.get());

  for (const auto& property : std::array<std::pair<const char*, int>, 3>{
           std::pair{"_ChromaLocation", AVS_CHROMA_LEFT},
           std::pair{"_ColorRange", AVS_COLORRANGE_FULL},
           std::pair{"_FieldBased", 1}}) {
    const auto actual = get_frame_property_int(environment.get(), output, property.first);
    ASSERT_TRUE(actual.has_value()) << property.first;
    EXPECT_EQ(*actual, property.second) << property.first;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(first_clip->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(second_clip->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(snapshot_frames(first_frames, vi), first_snapshots);
  EXPECT_EQ(snapshot_frames(second_frames, vi), second_snapshots);
}

class MergeFormatFilterTest : public ::testing::TestWithParam<MergeFormatCase> {};

TEST_P(MergeFormatFilterTest, CoversSubsampledAndAlphaPlanesAtNonEndpointWeight) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{test_case.width, test_case.height,
                                                test_case.pixel_type, 2, 25, 1});
  auto first_frames = make_format_frames(environment, vi, 23);
  auto second_frames = make_format_frames(environment, vi, 147);
  const auto first_snapshots = snapshot_frames(first_frames, vi);
  const auto second_snapshots = snapshot_frames(second_frames, vi);
  auto* first_clip = new FrameSequenceClip(vi, first_frames);
  auto* second_clip = new FrameSequenceClip(vi, second_frames);
  const PClip child(first_clip);
  const PClip other(second_clip);

  const PVideoFrame output = run_format_filter(test_case, child, other, environment.get());
  for (const int plane : video_frame_planes(vi)) {
    expect_plane(first_frames[1], second_frames[1], output, plane,
                 merge_format_plane(test_case, vi, plane), test_case.weight);
  }

  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(first_clip->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(second_clip->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(snapshot_frames(first_frames, vi), first_snapshots);
  EXPECT_EQ(snapshot_frames(second_frames, vi), second_snapshots);
}

INSTANTIATE_TEST_SUITE_P(
    FormatCases, MergeFormatFilterTest,
    ::testing::Values(
        MergeFormatCase{VideoInfo::CS_YV12, 8, 4, MergeOperation::All, 0.25f,
                        "Yv12_Width8_Height4_All_Weight25"},
        MergeFormatCase{VideoInfo::CS_YV12, 8, 4, MergeOperation::Luma, 0.25f,
                        "Yv12_Width8_Height4_Luma_Weight25"},
        MergeFormatCase{VideoInfo::CS_YV12, 8, 4, MergeOperation::Chroma, 0.25f,
                        "Yv12_Width8_Height4_Chroma_Weight25"},
        MergeFormatCase{VideoInfo::CS_YV16, 8, 5, MergeOperation::All, 0.25f,
                        "Yv16_Width8_Height5_All_Weight25"},
        MergeFormatCase{VideoInfo::CS_YV16, 8, 5, MergeOperation::Luma, 0.25f,
                        "Yv16_Width8_Height5_Luma_Weight25"},
        MergeFormatCase{VideoInfo::CS_YV16, 8, 5, MergeOperation::Chroma, 0.25f,
                        "Yv16_Width8_Height5_Chroma_Weight25"},
        MergeFormatCase{VideoInfo::CS_YUVA420, 8, 4, MergeOperation::All, 0.25f,
                        "Yuva420_Width8_Height4_All_Weight25"},
        MergeFormatCase{VideoInfo::CS_YUVA420, 8, 4, MergeOperation::Luma, 0.25f,
                        "Yuva420_Width8_Height4_Luma_Weight25"},
        MergeFormatCase{VideoInfo::CS_YUVA420, 8, 4, MergeOperation::Chroma, 0.25f,
                        "Yuva420_Width8_Height4_Chroma_Weight25"}),
    [](const ::testing::TestParamInfo<MergeFormatCase>& info) { return info.param.name; });

INSTANTIATE_TEST_SUITE_P(
    Operations, MergeFilterTest,
    ::testing::Values(MergeFilterCase{MergeOperation::All, 0.25f, "All_Weight25"},
                      MergeFilterCase{MergeOperation::Luma, 0.25f, "Luma_Weight25"},
                      MergeFilterCase{MergeOperation::Chroma, 0.25f, "Chroma_Weight25"},
                      MergeFilterCase{MergeOperation::All, 1.0f, "All_Weight100"},
                      MergeFilterCase{MergeOperation::Luma, 1.0f, "Luma_Weight100"},
                      MergeFilterCase{MergeOperation::Chroma, 1.0f, "Chroma_Weight100"}),
    [](const ::testing::TestParamInfo<MergeFilterCase>& info) { return info.param.name; });

}  // namespace
