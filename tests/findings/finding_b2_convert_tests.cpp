#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_planar.h"
#include "core/parser/script.h"
#ifdef AVSUT_FINDING_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

namespace avsut::test {
namespace {

template <typename T>
void fill_active_plane(PVideoFrame& frame, int plane, T value) {
  const int row_size = frame->GetRowSize(plane);
  ASSERT_EQ(row_size % static_cast<int>(sizeof(T)), 0);
  const int width = row_size / static_cast<int>(sizeof(T));
  for (int y = 0; y < frame->GetHeight(plane); ++y) {
    auto* row = reinterpret_cast<T*>(frame->GetWritePtr(plane) +
                                     static_cast<std::size_t>(y) * frame->GetPitch(plane));
    std::fill(row, row + width, value);
  }
}

template <typename T>
void expect_active_plane_value(const PVideoFrame& frame, int plane, T expected,
                               const char* operation) {
  const int row_size = frame->GetRowSize(plane);
  ASSERT_EQ(row_size % static_cast<int>(sizeof(T)), 0);
  const int width = row_size / static_cast<int>(sizeof(T));
  for (int y = 0; y < frame->GetHeight(plane); ++y) {
    const auto* row = reinterpret_cast<const T*>(
        frame->GetReadPtr(plane) + static_cast<std::size_t>(y) * frame->GetPitch(plane));
    for (int x = 0; x < width; ++x) {
      if constexpr (std::is_same<T, float>::value) {
        EXPECT_FLOAT_EQ(row[x], expected)
            << operation << " plane=" << plane << " row=" << y << " column=" << x;
      } else {
        EXPECT_EQ(row[x], expected)
            << operation << " plane=" << plane << " row=" << y << " column=" << x;
      }
    }
  }
}

template <typename T>
void verify_missing_yuv_alpha_becomes_opaque(int pixel_type, T y, T u, T v, T opaque,
                                             const char* format_name) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{5, 3, pixel_type, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source_frame, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(source_frame, 0xb2, PLANAR_U);
  fill_plane_full_pitch(source_frame, 0xc3, PLANAR_V);
  fill_active_plane(source_frame, PLANAR_Y, y);
  fill_active_plane(source_frame, PLANAR_U, u);
  fill_active_plane(source_frame, PLANAR_V, v);
  const auto source_before = FrameSnapshot::capture(source_frame, vi);

  const PClip source(new StaticFrameClip(vi, source_frame));
  const PClip converted(new ConvertYUV444ToRGB(source, "601:full", -2, environment.get()));

  ASSERT_TRUE(converted->GetVideoInfo().IsPlanarRGBA()) << format_name;
  const PVideoFrame output = converted->GetFrame(0, environment.get());
  expect_active_plane_value(output, PLANAR_A, opaque, format_name);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, vi), source_before)
      << format_name << " modified its source";
}

TEST(ConvertYuv444ToPlanarRgba, FillsMissingUint8AlphaWithFullOpacity) {
  verify_missing_yuv_alpha_becomes_opaque<std::uint8_t>(VideoInfo::CS_YV24, 16, 128, 128, 255,
                                                        "B2 Yuv444ToRgbap8");
}

TEST(ConvertYuv444ToPlanarRgba, FillsMissingUint16AlphaWithFullOpacity) {
  verify_missing_yuv_alpha_becomes_opaque<std::uint16_t>(
      VideoInfo::CS_YUV444P16, 4096, 32768, 32768, std::numeric_limits<std::uint16_t>::max(),
      "B2 Yuv444ToRgbap16");
}

TEST(ConvertYuv444ToPlanarRgba, FillsMissingFloatAlphaWithFullOpacity) {
  verify_missing_yuv_alpha_becomes_opaque<float>(VideoInfo::CS_YUV444PS, 0.5F, 0.0F, 0.0F, 1.0F,
                                                 "B2 Yuv444ToRgbaps");
}

void fill_float_pattern(PVideoFrame& frame, int plane, const std::array<float, 6>& pattern) {
  ASSERT_EQ(frame->GetRowSize(plane), 3 * static_cast<int>(sizeof(float)));
  ASSERT_EQ(frame->GetHeight(plane), 2);
  for (int y = 0; y < 2; ++y) {
    auto* row = reinterpret_cast<float*>(frame->GetWritePtr(plane) +
                                         static_cast<std::size_t>(y) * frame->GetPitch(plane));
    for (int x = 0; x < 3; ++x) {
      row[x] = pattern[static_cast<std::size_t>(y * 3 + x)];
    }
  }
}

void expect_finite_float_plane(const PVideoFrame& frame, int plane, const char* operation) {
  const int width = frame->GetRowSize(plane) / static_cast<int>(sizeof(float));
  for (int y = 0; y < frame->GetHeight(plane); ++y) {
    const auto* row = reinterpret_cast<const float*>(
        frame->GetReadPtr(plane) + static_cast<std::size_t>(y) * frame->GetPitch(plane));
    for (int x = 0; x < width; ++x) {
      EXPECT_TRUE(std::isfinite(row[x])) << operation << " plane=" << plane << " row=" << y
                                         << " column=" << x << " value=" << row[x];
    }
  }
}

TEST(FloatPlanarMatrixConversion, PreservesFiniteNeutralRgbRoundTrip) {
  AviSynthEnvironment environment;
  constexpr std::array<float, 6> kGreySamples{0.0F, 0.125F, 0.25F, 0.5F, 0.75F, 1.0F};
  const auto vi = make_video_info(VideoInfoSpec{3, 2, VideoInfo::CS_RGBPS, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source_frame, 0xa5, PLANAR_G);
  fill_plane_full_pitch(source_frame, 0xb6, PLANAR_B);
  fill_plane_full_pitch(source_frame, 0xc7, PLANAR_R);
  fill_float_pattern(source_frame, PLANAR_G, kGreySamples);
  fill_float_pattern(source_frame, PLANAR_B, kGreySamples);
  fill_float_pattern(source_frame, PLANAR_R, kGreySamples);
  const auto source_before = FrameSnapshot::capture(source_frame, vi);

  const PClip source(new StaticFrameClip(vi, source_frame));
  const PClip yuv(new ConvertRGBToYUV444(source, "601:full", false, environment.get()));
  const PClip round_trip(new ConvertYUV444ToRGB(yuv, "601:full", -1, environment.get()));

  ASSERT_TRUE(yuv->GetVideoInfo().IsYUV());
  ASSERT_TRUE(round_trip->GetVideoInfo().IsPlanarRGB());
  const PVideoFrame yuv_frame = yuv->GetFrame(0, environment.get());
  const PVideoFrame output = round_trip->GetFrame(0, environment.get());
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    expect_finite_float_plane(yuv_frame, plane, "B2 RGBPS to YUV444PS");
  }
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    expect_finite_float_plane(output, plane, "B2 YUV444PS to RGBPS");
    const auto* rows = reinterpret_cast<const float*>(output->GetReadPtr(plane));
    const int pitch_samples = output->GetPitch(plane) / static_cast<int>(sizeof(float));
    for (int y = 0; y < 2; ++y) {
      for (int x = 0; x < 3; ++x) {
        const float expected = kGreySamples[static_cast<std::size_t>(y * 3 + x)];
        EXPECT_NEAR(rows[y * pitch_samples + x], expected, 2.0e-5F)
            << "B2 float RGB/YUV round trip plane=" << plane << " row=" << y << " column=" << x;
      }
    }
  }
  EXPECT_EQ(FrameSnapshot::capture(source_frame, vi), source_before)
      << "B2 float matrix conversion modified its source";
}

TEST(AddAlphaPlane, FillsDefaultFloatAlphaWithFullOpacity) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_RGBPS, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source_frame, 0xa1, PLANAR_G);
  fill_plane_full_pitch(source_frame, 0xb2, PLANAR_B);
  fill_plane_full_pitch(source_frame, 0xc3, PLANAR_R);
  fill_active_plane(source_frame, PLANAR_G, 0.25F);
  fill_active_plane(source_frame, PLANAR_B, 0.5F);
  fill_active_plane(source_frame, PLANAR_R, 0.75F);
  const auto source_before = FrameSnapshot::capture(source_frame, vi);

  const PClip source(new StaticFrameClip(vi, source_frame));
  const PClip with_alpha(new AddAlphaPlane(source, nullptr, 0.0F, false, environment.get()));

  ASSERT_TRUE(with_alpha->GetVideoInfo().IsPlanarRGBA());
  const PVideoFrame output = with_alpha->GetFrame(0, environment.get());
  expect_active_plane_value(output, PLANAR_A, 1.0F, "B2 AddAlphaPlane RGBPS default alpha");
  EXPECT_EQ(FrameSnapshot::capture(source_frame, vi), source_before)
      << "B2 AddAlphaPlane modified its source";
}

AVSValue script_generated_nan(IScriptEnvironment* environment) {
  const AVSValue negative_one(-1.0F);
  return Sqrt(AVSValue(&negative_one, 1), nullptr, environment);
}

PClip create_add_alpha(PClip clip, const AVSValue& mask, const AVSValue& opacity,
                       IScriptEnvironment* environment) {
  const AVSValue args[3] = {clip, mask, opacity};
  return AddAlphaPlane::Create(AVSValue(args, 3), nullptr, environment).AsClip();
}

TEST(AddAlphaPlaneFactory, RejectsScriptGeneratedNanOpacityAndMask) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{4, 2, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source_frame, 16, PLANAR_Y);
  fill_plane_full_pitch(source_frame, 128, PLANAR_U);
  fill_plane_full_pitch(source_frame, 128, PLANAR_V);
  const PClip source(new StaticFrameClip(vi, source_frame));
  const AVSValue nan = script_generated_nan(environment.get());

  ASSERT_TRUE(std::isnan(nan.AsFloat()));
  EXPECT_THROW(create_add_alpha(source, AVSValue(), nan, environment.get()), AvisynthError);
  EXPECT_THROW(create_add_alpha(source, nan, AVSValue(), environment.get()), AvisynthError);
}

PClip create_interlaced_yuv420(PClip clip, IScriptEnvironment* environment) {
  const AVSValue args[11] = {clip,       true,       AVSValue(), AVSValue(), AVSValue(), AVSValue(),
                             AVSValue(), AVSValue(), AVSValue(), AVSValue(), AVSValue()};
  return ConvertToPlanarGeneric::CreateYUV420(
             AVSValue(args, 11), reinterpret_cast<void*>(static_cast<std::intptr_t>(1)),
             environment)
      .AsClip();
}

TEST(ConvertToYuv420Factory, RejectsInterlacedHeightNotMultipleOfFour) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 6, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source_frame, 16, PLANAR_Y);
  fill_plane_full_pitch(source_frame, 128, PLANAR_U);
  fill_plane_full_pitch(source_frame, 128, PLANAR_V);
  const auto source_before = FrameSnapshot::capture(source_frame, vi);
  const PClip source(new StaticFrameClip(vi, source_frame));

  EXPECT_THROW(create_interlaced_yuv420(source, environment.get()), AvisynthError);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, vi), source_before)
      << "B2 interlaced factory construction modified its source";
}

}  // namespace
}  // namespace avsut::test
