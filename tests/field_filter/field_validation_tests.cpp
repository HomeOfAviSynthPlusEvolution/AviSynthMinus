#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_LOCAL_UNDEF_AVS_UNUSED
#endif
#include "filters/combine.h"
#include "filters/edit.h"
#include "filters/field.h"
#include "filters/fps.h"
#ifdef AVSUT_LOCAL_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_LOCAL_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

namespace avsut::test {
namespace {

struct VideoSource {
  VideoInfo video_info;
  PVideoFrame frame;
  StaticFrameClip* clip_impl;
  PClip clip;
  FrameSnapshot snapshot;
};

VideoSource make_y8_source(AviSynthEnvironment& environment) {
  const auto video_info = make_video_info(VideoInfoSpec{4, 4, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, 0x5a, PLANAR_Y);
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return VideoSource{video_info, frame, clip_impl, PClip(clip_impl), FrameSnapshot::capture(frame, video_info)};
}

struct SelectEveryCase {
  const char* name;
  int every;
  int from;
};

void PrintTo(const SelectEveryCase& test_case, std::ostream* output) {
  *output << test_case.name;
}

class SelectEveryConstruction : public ::testing::TestWithParam<SelectEveryCase> {};

TEST_P(SelectEveryConstruction, RejectsNegativeOrOutOfRangePatternParameters) {
  AviSynthEnvironment environment;
  const VideoSource source = make_y8_source(environment);
  const auto& test_case = GetParam();

  EXPECT_THROW(
      { SelectEvery filter(source.clip, test_case.every, test_case.from, environment.get()); }, AvisynthError)
      << "SelectEvery case=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot)
      << "SelectEvery case=" << test_case.name << " modified its source";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty())
      << "SelectEvery case=" << test_case.name << " requested a frame during construction";
}

INSTANTIATE_TEST_SUITE_P(BoundaryCases, SelectEveryConstruction,
                         ::testing::Values(SelectEveryCase{"NegativeEvery", -2, 0},
                                           SelectEveryCase{"NegativeFrom", 2, -1}, SelectEveryCase{"FromAtEnd", 2, 1},
                                           SelectEveryCase{"FromPastEnd", 2, 2}),
                         [](const ::testing::TestParamInfo<SelectEveryCase>& info) { return info.param.name; });

enum class EditOperation { Freeze, Delete, Duplicate };

struct EditFrameCase {
  const char* name;
  EditOperation operation;
  int first;
  int last;
  int frame;
};

void PrintTo(const EditFrameCase& test_case, std::ostream* output) {
  *output << test_case.name;
}

PClip create_edit_filter(PClip source, const EditFrameCase& test_case, IScriptEnvironment* environment) {
  switch (test_case.operation) {
    case EditOperation::Freeze: {
      const AVSValue args[4] = {source, test_case.first, test_case.last, test_case.frame};
      return FreezeFrame::Create(AVSValue(args, 4), nullptr, environment).AsClip();
    }
    case EditOperation::Delete: {
      const AVSValue frames[1] = {test_case.frame};
      const AVSValue args[2] = {source, AVSValue(frames, 1)};
      return DeleteFrame::Create(AVSValue(args, 2), nullptr, environment).AsClip();
    }
    case EditOperation::Duplicate: {
      const AVSValue frames[1] = {test_case.frame};
      const AVSValue args[2] = {source, AVSValue(frames, 1)};
      return DuplicateFrame::Create(AVSValue(args, 2), nullptr, environment).AsClip();
    }
  }
  return PClip();
}

class EditFrameFactory : public ::testing::TestWithParam<EditFrameCase> {};

TEST_P(EditFrameFactory, RejectsInvalidFrameSelection) {
  AviSynthEnvironment environment;
  const VideoSource source = make_y8_source(environment);
  const auto& test_case = GetParam();

  EXPECT_THROW(create_edit_filter(source.clip, test_case, environment.get()), AvisynthError)
      << "edit case=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source.frame, source.video_info), source.snapshot)
      << "edit case=" << test_case.name << " modified its source";
  EXPECT_TRUE(source.clip_impl->frame_requests().empty())
      << "edit case=" << test_case.name << " requested a frame during construction";
}

INSTANTIATE_TEST_SUITE_P(BoundaryCases, EditFrameFactory,
                         ::testing::Values(EditFrameCase{"FreezeNegativeFirst", EditOperation::Freeze, -1, 0, 0},
                                           EditFrameCase{"FreezeSourcePastEnd", EditOperation::Freeze, 0, 0, 1},
                                           EditFrameCase{"DeleteNegativeFrame", EditOperation::Delete, 0, 0, -1},
                                           EditFrameCase{"DeletePastEnd", EditOperation::Delete, 0, 0, 1},
                                           EditFrameCase{"DeleteOnlyFrame", EditOperation::Delete, 0, 0, 0},
                                           EditFrameCase{"DuplicateNegativeFrame", EditOperation::Duplicate, 0, 0, -1},
                                           EditFrameCase{"DuplicatePastEnd", EditOperation::Duplicate, 0, 0, 1}),
                         [](const ::testing::TestParamInfo<EditFrameCase>& info) { return info.param.name; });

} // namespace
} // namespace avsut::test
