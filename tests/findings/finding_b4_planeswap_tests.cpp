#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B4_UNDEF_AVS_UNUSED
#endif
#include "filters/planeswap.h"
#ifdef AVSUT_FINDING_B4_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B4_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>

namespace avsut::test {
namespace {

struct ComponentStorageCase {
  const char* name;
  int v_pixel_type;
  int y_pixel_type;
  int alpha_pixel_type;
};

void PrintTo(const ComponentStorageCase& test_case, std::ostream* output) {
  *output << test_case.name;
}

std::array<int, 4> frame_planes(const VideoInfo& video_info) {
  if (video_info.IsPlanarRGBA()) {
    return {PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A};
  }
  if (video_info.IsPlanarRGB()) {
    return {PLANAR_G, PLANAR_B, PLANAR_R, DEFAULT_PLANE};
  }
  if (video_info.IsYUVA()) {
    return {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A};
  }
  if (video_info.IsYUV() && !video_info.IsY()) {
    return {PLANAR_Y, PLANAR_U, PLANAR_V, DEFAULT_PLANE};
  }
  return {DEFAULT_PLANE, DEFAULT_PLANE, DEFAULT_PLANE, DEFAULT_PLANE};
}

void fill_frame(PVideoFrame& frame, const VideoInfo& video_info) {
  const auto planes = frame_planes(video_info);
  for (std::size_t index = 0; index < planes.size(); ++index) {
    if (index != 0 && planes[index] == DEFAULT_PLANE) {
      continue;
    }
    fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0x30 + index), planes[index]);
  }
}

struct TestClip {
  VideoInfo video_info;
  PVideoFrame frame;
  PClip clip;
  FrameSnapshot snapshot;
};

TestClip make_test_clip(AviSynthEnvironment& environment, int pixel_type) {
  const auto video_info = make_video_info(VideoInfoSpec{4, 4, pixel_type, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_frame(frame, video_info);
  return TestClip{video_info, frame, PClip(new StaticFrameClip(video_info, frame)),
                  FrameSnapshot::capture(frame, video_info)};
}

class YToUvConstruction : public ::testing::TestWithParam<ComponentStorageCase> {};

TEST_P(YToUvConstruction, RejectsMismatchedComponentStorage) {
  AviSynthEnvironment environment;
  const auto& test_case = GetParam();
  const TestClip u = make_test_clip(environment, VideoInfo::CS_Y8);
  const TestClip v = make_test_clip(environment, test_case.v_pixel_type);
  std::optional<TestClip> y;
  std::optional<TestClip> alpha;

  if (test_case.y_pixel_type != 0) {
    y.emplace(make_test_clip(environment, test_case.y_pixel_type));
  }
  if (test_case.alpha_pixel_type != 0) {
    alpha.emplace(make_test_clip(environment, test_case.alpha_pixel_type));
  }

  EXPECT_THROW(
      {
        SwapYToUV filter(u.clip, v.clip, y ? y->clip : PClip(), alpha ? alpha->clip : PClip(),
                         environment.get());
      },
      AvisynthError)
      << "B4 component storage=" << test_case.name;

  EXPECT_EQ(FrameSnapshot::capture(u.frame, u.video_info), u.snapshot)
      << "B4 component storage=" << test_case.name << " modified U source";
  EXPECT_EQ(FrameSnapshot::capture(v.frame, v.video_info), v.snapshot)
      << "B4 component storage=" << test_case.name << " modified V source";
  if (y) {
    EXPECT_EQ(FrameSnapshot::capture(y->frame, y->video_info), y->snapshot)
        << "B4 component storage=" << test_case.name << " modified Y source";
  }
  if (alpha) {
    EXPECT_EQ(FrameSnapshot::capture(alpha->frame, alpha->video_info), alpha->snapshot)
        << "B4 component storage=" << test_case.name << " modified alpha source";
  }
}

INSTANTIATE_TEST_SUITE_P(
    B4, YToUvConstruction,
    ::testing::Values(ComponentStorageCase{"V16BitWithU8Bit", VideoInfo::CS_Y16, 0, 0},
                      ComponentStorageCase{"Y16BitWithU8Chroma", VideoInfo::CS_Y8,
                                           VideoInfo::CS_Y16, 0},
                      ComponentStorageCase{"PackedRgb32Alpha", VideoInfo::CS_Y8, VideoInfo::CS_Y8,
                                           VideoInfo::CS_BGR32}),
    [](const ::testing::TestParamInfo<ComponentStorageCase>& info) { return info.param.name; });

}  // namespace
}  // namespace avsut::test
