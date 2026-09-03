#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_B8_UNDEF_AVS_UNUSED
#endif
#include "core/parser/script.h"
#include "filters/overlay/444convert.h"
#include "filters/overlay/overlay.h"
#ifdef AVSUT_FINDING_B8_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_B8_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <ostream>
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

StaticVideoSource make_yuv444_source(AviSynthEnvironment& environment, std::uint8_t luma,
                                     std::uint8_t chroma_u, std::uint8_t chroma_v) {
  const auto video_info = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(video_info);
  fill_plane_full_pitch(frame, luma, PLANAR_Y);
  fill_plane_full_pitch(frame, chroma_u, PLANAR_U);
  fill_plane_full_pitch(frame, chroma_v, PLANAR_V);
  auto* clip_impl = new StaticFrameClip(video_info, frame);
  return StaticVideoSource{video_info, frame, clip_impl, PClip(clip_impl),
                           FrameSnapshot::capture(frame, video_info)};
}

std::array<AVSValue, 14> make_overlay_args(const PClip& base, const PClip& overlay,
                                           const PClip& mask, const AVSValue& opacity,
                                           const char* mode) {
  std::array<AVSValue, 14> args{};
  args[0] = base;
  args[1] = overlay;
  args[2] = 0;
  args[3] = 0;
  if (mask) {
    args[4] = mask;
  }
  args[5] = opacity;
  args[6] = mode;
  args[7] = true;
  args[9] = true;
  args[10] = false;
  args[11] = true;
  args[12] = "";
  args[13] = "mpeg2";
  return args;
}

PVideoFrame render_overlay(AviSynthEnvironment& environment, const PClip& base,
                           const PClip& overlay, const PClip& mask, const AVSValue& opacity,
                           const char* mode) {
  const auto args = make_overlay_args(base, overlay, mask, opacity, mode);
  Overlay filter(base, AVSValue(args.data(), static_cast<int>(args.size())), environment.get());
  return filter.GetFrame(0, environment.get());
}

std::vector<int> yuv_planes() { return {PLANAR_Y, PLANAR_U, PLANAR_V}; }

::testing::AssertionResult active_planes_equal(const PVideoFrame& expected,
                                               const PVideoFrame& actual) {
  for (const int plane : yuv_planes()) {
    if (expected->GetRowSize(plane) != actual->GetRowSize(plane) ||
        expected->GetHeight(plane) != actual->GetHeight(plane)) {
      return ::testing::AssertionFailure()
             << "plane=" << plane << " geometry expected=" << expected->GetRowSize(plane) << "x"
             << expected->GetHeight(plane) << " actual=" << actual->GetRowSize(plane) << "x"
             << actual->GetHeight(plane);
    }
    for (int y = 0; y < expected->GetHeight(plane); ++y) {
      const auto* expected_row = expected->GetReadPtr(plane) + y * expected->GetPitch(plane);
      const auto* actual_row = actual->GetReadPtr(plane) + y * actual->GetPitch(plane);
      for (int x = 0; x < expected->GetRowSize(plane); ++x) {
        if (expected_row[x] != actual_row[x]) {
          return ::testing::AssertionFailure() << "plane=" << plane << " x=" << x << " y=" << y
                                               << " expected=" << static_cast<int>(expected_row[x])
                                               << " actual=" << static_cast<int>(actual_row[x]);
        }
      }
    }
  }
  return ::testing::AssertionSuccess();
}

enum class NarrowConversion { Yv12ToYv24, Yv16ToYv24, Yv24ToYv16 };

struct NarrowConversionCase {
  const char* name;
  NarrowConversion operation;
};

void PrintTo(const NarrowConversionCase& test_case, std::ostream* output) {
  *output << test_case.name;
}

void write_conversion_input(PVideoFrame& frame) {
  for (const int plane : yuv_planes()) {
    fill_plane_full_pitch(frame, 0xa5, plane);
    for (int y = 0; y < frame->GetHeight(plane); ++y) {
      auto* row = frame->GetWritePtr(plane) + y * frame->GetPitch(plane);
      for (int x = 0; x < frame->GetRowSize(plane); ++x) {
        const int plane_offset = plane == PLANAR_Y ? 10 : plane == PLANAR_U ? 40 : 90;
        row[x] = static_cast<std::uint8_t>(plane_offset + 7 * y + 3 * x);
      }
    }
  }
}

bool converted_luma_matches(const PVideoFrame& source, const PVideoFrame& destination) {
  for (int y = 0; y < destination->GetHeight(PLANAR_Y); ++y) {
    const auto* source_row = source->GetReadPtr(PLANAR_Y) + y * source->GetPitch(PLANAR_Y);
    const auto* destination_row =
        destination->GetReadPtr(PLANAR_Y) + y * destination->GetPitch(PLANAR_Y);
    for (int x = 0; x < destination->GetRowSize(PLANAR_Y); ++x) {
      if (source_row[x] != destination_row[x]) {
        return false;
      }
    }
  }
  return true;
}

bool narrow_conversion_matches_reference(const PVideoFrame& source, const PVideoFrame& destination,
                                         NarrowConversion operation) {
  if (!converted_luma_matches(source, destination)) {
    return false;
  }
  for (const int plane : {PLANAR_U, PLANAR_V}) {
    for (int y = 0; y < destination->GetHeight(plane); ++y) {
      const auto* destination_row =
          destination->GetReadPtr(plane) + y * destination->GetPitch(plane);
      for (int x = 0; x < destination->GetRowSize(plane); ++x) {
        std::uint8_t expected{};
        if (operation == NarrowConversion::Yv12ToYv24) {
          const auto* source_row = source->GetReadPtr(plane) + (y / 2) * source->GetPitch(plane);
          expected = source_row[x / 2];
        } else if (operation == NarrowConversion::Yv16ToYv24) {
          const auto* source_row = source->GetReadPtr(plane) + y * source->GetPitch(plane);
          expected = source_row[x / 2];
        } else {
          const auto* source_row = source->GetReadPtr(plane) + y * source->GetPitch(plane);
          expected = static_cast<std::uint8_t>(
              (static_cast<int>(source_row[2 * x]) + static_cast<int>(source_row[2 * x + 1]) + 1) /
              2);
        }
        if (destination_row[x] != expected) {
          return false;
        }
      }
    }
  }
  return true;
}

bool run_narrow_conversion(const NarrowConversionCase& test_case) {
  AviSynthEnvironment environment;
  constexpr int kWidth = 4;
  constexpr int kHeight = 2;
  const int source_pixel_type =
      test_case.operation == NarrowConversion::Yv12ToYv24   ? VideoInfo::CS_YV12
      : test_case.operation == NarrowConversion::Yv16ToYv24 ? VideoInfo::CS_YV16
                                                            : VideoInfo::CS_YV24;
  const int destination_pixel_type =
      test_case.operation == NarrowConversion::Yv24ToYv16 ? VideoInfo::CS_YV16 : VideoInfo::CS_YV24;
  const auto source_info =
      make_video_info(VideoInfoSpec{kWidth, kHeight, source_pixel_type, 1, 25, 1});
  const auto destination_info =
      make_video_info(VideoInfoSpec{kWidth, kHeight, destination_pixel_type, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(source_info);
  PVideoFrame destination = environment.get()->NewVideoFrame(destination_info);
  write_conversion_input(source);
  for (const int plane : yuv_planes()) {
    fill_plane_full_pitch(destination, 0x5a, plane);
  }

  switch (test_case.operation) {
    case NarrowConversion::Yv12ToYv24:
      Convert444FromYV12(source, destination, 1, 8, environment.get());
      break;
    case NarrowConversion::Yv16ToYv24:
      Convert444FromYV16(source, destination, 1, 8, environment.get());
      break;
    case NarrowConversion::Yv24ToYv16:
      Convert444ToYV16(source, destination, 1, 8, environment.get());
      break;
  }

  return narrow_conversion_matches_reference(source, destination, test_case.operation) &&
         source->CheckMemory() != 1 && destination->CheckMemory() != 1;
}

class NarrowOverlayConversions : public ::testing::TestWithParam<NarrowConversionCase> {};

TEST_P(NarrowOverlayConversions, HandlesLegallyNarrowRowsWithoutCorruptingFrames) {
  const NarrowConversionCase test_case = GetParam();
  EXPECT_EXIT(
      { std::_Exit(run_narrow_conversion(test_case) ? EXIT_SUCCESS : EXIT_FAILURE); },
      ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

INSTANTIATE_TEST_SUITE_P(
    B8, NarrowOverlayConversions,
    ::testing::Values(NarrowConversionCase{"Yv12ToYv24", NarrowConversion::Yv12ToYv24},
                      NarrowConversionCase{"Yv16ToYv24", NarrowConversion::Yv16ToYv24},
                      NarrowConversionCase{"Yv24ToYv16", NarrowConversion::Yv24ToYv16}),
    [](const ::testing::TestParamInfo<NarrowConversionCase>& info) { return info.param.name; });

AVSValue script_generated_nan(IScriptEnvironment* environment) {
  const AVSValue negative_one(-1.0F);
  return Sqrt(AVSValue(&negative_one, 1), nullptr, environment);
}

enum class NonFiniteOpacity { Nan, PositiveInfinity };

struct NonFiniteOpacityCase {
  const char* name;
  NonFiniteOpacity value;
};

void PrintTo(const NonFiniteOpacityCase& test_case, std::ostream* output) {
  *output << test_case.name;
}

class OverlayOpacityConstruction : public ::testing::TestWithParam<NonFiniteOpacityCase> {};

TEST_P(OverlayOpacityConstruction, RejectsNonFiniteOpacityBeforeIntegerScaling) {
  AviSynthEnvironment environment;
  const StaticVideoSource base = make_yuv444_source(environment, 64, 32, 224);
  const StaticVideoSource overlay = make_yuv444_source(environment, 192, 224, 32);
  const auto& test_case = GetParam();
  const AVSValue opacity = test_case.value == NonFiniteOpacity::Nan
                               ? script_generated_nan(environment.get())
                               : AVSValue(std::numeric_limits<float>::infinity());
  ASSERT_TRUE(test_case.value != NonFiniteOpacity::Nan || std::isnan(opacity.AsFloat()));
  const auto args = make_overlay_args(base.clip, overlay.clip, PClip(), opacity, "Blend");

  EXPECT_THROW(
      {
        Overlay filter(base.clip, AVSValue(args.data(), static_cast<int>(args.size())),
                       environment.get());
      },
      AvisynthError)
      << "B8 Overlay opacity=" << test_case.name;
  EXPECT_EQ(FrameSnapshot::capture(base.frame, base.video_info), base.snapshot)
      << "B8 Overlay opacity=" << test_case.name << " modified base during construction";
  EXPECT_EQ(FrameSnapshot::capture(overlay.frame, overlay.video_info), overlay.snapshot)
      << "B8 Overlay opacity=" << test_case.name << " modified overlay during construction";
  EXPECT_TRUE(base.clip_impl->frame_requests().empty());
  EXPECT_TRUE(overlay.clip_impl->frame_requests().empty());
}

INSTANTIATE_TEST_SUITE_P(
    B8, OverlayOpacityConstruction,
    ::testing::Values(NonFiniteOpacityCase{"Nan", NonFiniteOpacity::Nan},
                      NonFiniteOpacityCase{"PositiveInfinity", NonFiniteOpacity::PositiveInfinity}),
    [](const ::testing::TestParamInfo<NonFiniteOpacityCase>& info) { return info.param.name; });

struct OverlayModeCase {
  const char* name;
  const char* mode;
};

void PrintTo(const OverlayModeCase& test_case, std::ostream* output) { *output << test_case.name; }

PVideoFrame render_mask_equivalence_case(AviSynthEnvironment& environment, const char* mode,
                                         bool with_mask, StaticVideoSource& base,
                                         StaticVideoSource& overlay, StaticVideoSource& full_mask) {
  const PClip mask = with_mask ? full_mask.clip : PClip();
  return render_overlay(environment, base.clip, overlay.clip, mask, AVSValue(1.0F), mode);
}

class EqualLumaMaskEquivalence : public ::testing::TestWithParam<OverlayModeCase> {};

TEST_P(EqualLumaMaskEquivalence, TreatsFullMaskLikeOmittedMaskAtEqualLuma) {
  AviSynthEnvironment environment;
  const auto& test_case = GetParam();
  StaticVideoSource unmasked_base = make_yuv444_source(environment, 100, 16, 32);
  StaticVideoSource unmasked_overlay = make_yuv444_source(environment, 100, 224, 208);
  StaticVideoSource masked_base = make_yuv444_source(environment, 100, 16, 32);
  StaticVideoSource masked_overlay = make_yuv444_source(environment, 100, 224, 208);
  StaticVideoSource full_mask = make_yuv444_source(environment, 255, 255, 255);

  const PVideoFrame unmasked = render_mask_equivalence_case(
      environment, test_case.mode, false, unmasked_base, unmasked_overlay, full_mask);
  const PVideoFrame masked = render_mask_equivalence_case(environment, test_case.mode, true,
                                                          masked_base, masked_overlay, full_mask);

  EXPECT_TRUE(active_planes_equal(unmasked, masked)) << "B8 Overlay mode=" << test_case.name;
  EXPECT_NE(unmasked->CheckMemory(), 1);
  EXPECT_NE(masked->CheckMemory(), 1);
  EXPECT_EQ(unmasked_base.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(unmasked_overlay.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(masked_base.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(masked_overlay.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(full_mask.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(unmasked_base.frame, unmasked_base.video_info),
            unmasked_base.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(unmasked_overlay.frame, unmasked_overlay.video_info),
            unmasked_overlay.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(masked_base.frame, masked_base.video_info),
            masked_base.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(masked_overlay.frame, masked_overlay.video_info),
            masked_overlay.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(full_mask.frame, full_mask.video_info), full_mask.snapshot);
}

INSTANTIATE_TEST_SUITE_P(B8, EqualLumaMaskEquivalence,
                         ::testing::Values(OverlayModeCase{"Darken", "Darken"},
                                           OverlayModeCase{"Lighten", "Lighten"}),
                         [](const ::testing::TestParamInfo<OverlayModeCase>& info) {
                           return info.param.name;
                         });

struct MaskEndpointCase {
  const char* name;
  const char* mode;
  std::uint8_t base_luma;
  std::uint8_t overlay_luma;
};

void PrintTo(const MaskEndpointCase& test_case, std::ostream* output) { *output << test_case.name; }

class FullMaskEndpointEquivalence : public ::testing::TestWithParam<MaskEndpointCase> {};

TEST_P(FullMaskEndpointEquivalence, MatchesOmittedMaskAcrossIntegerModes) {
  AviSynthEnvironment environment;
  const auto& test_case = GetParam();
  StaticVideoSource unmasked_base = make_yuv444_source(environment, test_case.base_luma, 79, 181);
  StaticVideoSource unmasked_overlay =
      make_yuv444_source(environment, test_case.overlay_luma, 213, 41);
  StaticVideoSource masked_base = make_yuv444_source(environment, test_case.base_luma, 79, 181);
  StaticVideoSource masked_overlay =
      make_yuv444_source(environment, test_case.overlay_luma, 213, 41);
  StaticVideoSource full_mask = make_yuv444_source(environment, 255, 255, 255);

  const PVideoFrame unmasked = render_mask_equivalence_case(
      environment, test_case.mode, false, unmasked_base, unmasked_overlay, full_mask);
  const PVideoFrame masked = render_mask_equivalence_case(environment, test_case.mode, true,
                                                          masked_base, masked_overlay, full_mask);

  EXPECT_TRUE(active_planes_equal(unmasked, masked)) << "B8 Overlay mode=" << test_case.name;
  EXPECT_NE(unmasked->CheckMemory(), 1);
  EXPECT_NE(masked->CheckMemory(), 1);
  EXPECT_EQ(unmasked_base.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(unmasked_overlay.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(masked_base.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(masked_overlay.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(full_mask.clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(unmasked_base.frame, unmasked_base.video_info),
            unmasked_base.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(unmasked_overlay.frame, unmasked_overlay.video_info),
            unmasked_overlay.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(masked_base.frame, masked_base.video_info),
            masked_base.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(masked_overlay.frame, masked_overlay.video_info),
            masked_overlay.snapshot);
  EXPECT_EQ(FrameSnapshot::capture(full_mask.frame, full_mask.video_info), full_mask.snapshot);
}

INSTANTIATE_TEST_SUITE_P(B8, FullMaskEndpointEquivalence,
                         ::testing::Values(MaskEndpointCase{"Add", "Add", 31, 220},
                                           MaskEndpointCase{"Darken", "Darken", 220, 31},
                                           MaskEndpointCase{"Lighten", "Lighten", 31, 220},
                                           MaskEndpointCase{"Multiply", "Multiply", 79, 213},
                                           MaskEndpointCase{"Difference", "Difference", 79, 213},
                                           MaskEndpointCase{"Exclusion", "Exclusion", 79, 213},
                                           MaskEndpointCase{"SoftLight", "SoftLight", 79, 213},
                                           MaskEndpointCase{"HardLight", "HardLight", 79, 213}),
                         [](const ::testing::TestParamInfo<MaskEndpointCase>& info) {
                           return info.param.name;
                         });

}  // namespace
}  // namespace avsut::test
