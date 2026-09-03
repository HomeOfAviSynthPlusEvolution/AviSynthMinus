#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B3_UNDEF_AVS_UNUSED
#endif
#include "core/parser/script.h"
#include "filters/resample.h"
#ifdef AVSUT_FINDING_B3_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B3_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <array>
#include <cstddef>
#include <cmath>
#include <ostream>

namespace avsut::test {
namespace {

struct CropArgumentCase {
  const char* name;
  int argument_index;
};

void PrintTo(const CropArgumentCase& test_case, std::ostream* output) { *output << test_case.name; }

AVSValue script_generated_nan(IScriptEnvironment* environment) {
  const AVSValue negative_one(-1.0F);
  return Sqrt(AVSValue(&negative_one, 1), nullptr, environment);
}

PClip create_point_resize(PClip source, const CropArgumentCase& test_case,
                          IScriptEnvironment* environment) {
  std::array<AVSValue, 10> args{source,     3,          3,          AVSValue(), AVSValue(),
                                AVSValue(), AVSValue(), AVSValue(), AVSValue(), AVSValue()};
  args[static_cast<std::size_t>(test_case.argument_index)] = script_generated_nan(environment);
  return FilteredResize::Create_PointResize(AVSValue(args.data(), static_cast<int>(args.size())),
                                            nullptr, environment)
      .AsClip();
}

class PointResizeFactory : public ::testing::TestWithParam<CropArgumentCase> {};

TEST_P(PointResizeFactory, RejectsScriptGeneratedNanCropArgument) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{4, 4, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source_frame, 0x5a, PLANAR_Y);
  const auto source_before = FrameSnapshot::capture(source_frame, vi);
  const PClip source(new StaticFrameClip(vi, source_frame));
  const auto& test_case = GetParam();

  const AVSValue nan = script_generated_nan(environment.get());
  ASSERT_TRUE(std::isnan(nan.AsFloat())) << "B3 crop argument=" << test_case.name;
  EXPECT_THROW(create_point_resize(source, test_case, environment.get()), AvisynthError)
      << "B3 crop argument=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(source_frame, vi), source_before)
      << "B3 crop argument=" << test_case.name << " modified its source";
}

INSTANTIATE_TEST_SUITE_P(
    B3, PointResizeFactory,
    ::testing::Values(CropArgumentCase{"SrcLeft", 3}, CropArgumentCase{"SrcTop", 4},
                      CropArgumentCase{"SrcWidth", 5}, CropArgumentCase{"SrcHeight", 6}),
    [](const ::testing::TestParamInfo<CropArgumentCase>& info) { return info.param.name; });

}  // namespace
}  // namespace avsut::test
