#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_LOCAL_UNDEF_AVS_UNUSED
#endif
#include "merge/merge_all.h"
using aif::filters::merge::MergeAll;
#include "merge/merge_luma.h"
using aif::filters::merge::MergeLuma;
#include "merge/merge_chroma.h"
using aif::filters::merge::MergeChroma;
#ifdef AVSUT_LOCAL_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_LOCAL_UNDEF_AVS_UNUSED
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

StaticVideoSource make_yuv_source(AviSynthEnvironment& environment, int pixel_type, std::uint8_t luma,
                                  std::uint8_t chroma_u, std::uint8_t chroma_v, std::uint8_t alpha = 0) {
  const auto video_info = make_video_info(VideoInfoSpec{8, 3, pixel_type, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, luma, PLANAR_Y);
  fill_plane_full_pitch(frame, chroma_u, PLANAR_U);
  fill_plane_full_pitch(frame, chroma_v, PLANAR_V);
  if (video_info.IsYUVA()) {
    fill_plane_full_pitch(frame, alpha, PLANAR_A);
  }
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl), FrameSnapshot::capture(frame, video_info)};
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
      EXPECT_THROW({ MergeAll filter(first.clip, second.clip, nan, environment.get()); }, AvisynthError);
      break;
    case MergeOperation::Luma:
      EXPECT_THROW({ MergeLuma filter(first.clip, second.clip, nan, environment.get()); }, AvisynthError);
      break;
    case MergeOperation::Chroma:
      EXPECT_THROW({ MergeChroma filter(first.clip, second.clip, nan, environment.get()); }, AvisynthError);
      break;
  }
  EXPECT_EQ(FrameSnapshot::capture(first.frame, first.video_info), first.snapshot)
      << "Merge case=" << test_case.name << " modified first source";
  EXPECT_EQ(FrameSnapshot::capture(second.frame, second.video_info), second.snapshot)
      << "Merge case=" << test_case.name << " modified second source";
  EXPECT_TRUE(first.clip_impl->frame_requests().empty())
      << "Merge case=" << test_case.name << " requested first source during construction";
  EXPECT_TRUE(second.clip_impl->frame_requests().empty())
      << "Merge case=" << test_case.name << " requested second source during construction";
}

INSTANTIATE_TEST_SUITE_P(BoundaryCases, MergeWeightConstruction,
                         ::testing::Values(MergeOperationCase{"All", MergeOperation::All},
                                           MergeOperationCase{"Luma", MergeOperation::Luma},
                                           MergeOperationCase{"Chroma", MergeOperation::Chroma}),
                         [](const ::testing::TestParamInfo<MergeOperationCase>& info) { return info.param.name; });

} // namespace
} // namespace avsut::test
