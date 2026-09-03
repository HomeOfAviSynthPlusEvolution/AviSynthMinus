#include <gtest/gtest.h>

#include "convert_yuy2_test_helpers.h"

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVSUT_DEFINED_AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#endif
#include "convert/convert_planar.h"
#ifdef AVSUT_DEFINED_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_DEFINED_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <cstdint>
#include <vector>

namespace avsut::test {
namespace {

constexpr int kPublicYuy2Width = 10;
constexpr int kPublicYuy2Height = 3;

VideoInfo public_yuy2_video_info() {
  return make_video_info(VideoInfoSpec{kPublicYuy2Width, kPublicYuy2Height,
                                       VideoInfo::CS_YUY2, 1, 25, 1});
}

void fill_public_yuy2_frame(PVideoFrame& frame) {
  fill_plane_full_pitch(frame, 0xe7, DEFAULT_PLANE);
  for (int y = 0; y < kPublicYuy2Height; ++y) {
    auto* row = frame->GetWritePtr() + y * frame->GetPitch();
    for (int pair = 0; pair < kPublicYuy2Width / 2; ++pair) {
      row[pair * 4 + 0] = static_cast<std::uint8_t>(7 + pair * 11 + y * 17);
      row[pair * 4 + 1] = static_cast<std::uint8_t>(31 + pair * 13 + y * 19);
      row[pair * 4 + 2] = static_cast<std::uint8_t>(53 + pair * 23 + y * 29);
      row[pair * 4 + 3] = static_cast<std::uint8_t>(79 + pair * 31 + y * 37);
    }
  }
}

void fill_public_yv16_frame(PVideoFrame& frame) {
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0x90 + plane), plane);
  }
  write_frame_plane<std::uint8_t>(frame, PLANAR_Y, [](int x, int y) {
    return 5 + x * 17 + y * 23;
  });
  write_frame_plane<std::uint8_t>(frame, PLANAR_U, [](int x, int y) {
    return 41 + x * 19 + y * 29;
  });
  write_frame_plane<std::uint8_t>(frame, PLANAR_V, [](int x, int y) {
    return 83 + x * 31 + y * 37;
  });
}

TEST(PublicConvertYuy2ToY8, ExtractsLumaAndRemovesChromaLocation) {
  AviSynthEnvironment environment;
  const auto source_vi = public_yuy2_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_public_yuy2_frame(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_LEFT);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  ConvertYUY2ToYV16_or_Y filter(source, true, environment.get());
  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_Y8);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kPublicYuy2Width);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), kPublicYuy2Height);

  const auto* source_bytes = source_frame->GetReadPtr();
  const auto* output_y = output->GetReadPtr(PLANAR_Y);
  for (int y = 0; y < kPublicYuy2Height; ++y) {
    for (int x = 0; x < kPublicYuy2Width; ++x) {
      EXPECT_EQ(output_y[y * output->GetPitch(PLANAR_Y) + x],
                source_bytes[y * source_frame->GetPitch() + x * 2])
          << "row=" << y << " column=" << x;
    }
  }
  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(PublicConvertYuy2ToYv16, SplitsPackedChannelsIntoPlanarRows) {
  AviSynthEnvironment environment;
  const auto source_vi = public_yuy2_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_public_yuy2_frame(source_frame);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  ConvertYUY2ToYV16_or_Y filter(source, false, environment.get());
  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV16);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kPublicYuy2Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kPublicYuy2Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kPublicYuy2Width / 2);
  const auto* source_bytes = source_frame->GetReadPtr();
  const auto* output_y = output->GetReadPtr(PLANAR_Y);
  const auto* output_u = output->GetReadPtr(PLANAR_U);
  const auto* output_v = output->GetReadPtr(PLANAR_V);
  for (int y = 0; y < kPublicYuy2Height; ++y) {
    for (int pair = 0; pair < kPublicYuy2Width / 2; ++pair) {
      const auto source_offset = y * source_frame->GetPitch() + pair * 4;
      EXPECT_EQ(output_y[y * output->GetPitch(PLANAR_Y) + pair * 2], source_bytes[source_offset])
          << "Y first row=" << y << " pair=" << pair;
      EXPECT_EQ(output_y[y * output->GetPitch(PLANAR_Y) + pair * 2 + 1],
                source_bytes[source_offset + 2])
          << "Y second row=" << y << " pair=" << pair;
      EXPECT_EQ(output_u[y * output->GetPitch(PLANAR_U) + pair], source_bytes[source_offset + 1])
          << "U row=" << y << " pair=" << pair;
      EXPECT_EQ(output_v[y * output->GetPitch(PLANAR_V) + pair], source_bytes[source_offset + 3])
          << "V row=" << y << " pair=" << pair;
    }
  }
  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(PublicConvertYv16ToYuy2, InterleavesPlanarChannelsIntoPackedRows) {
  AviSynthEnvironment environment;
  const auto source_vi = make_video_info(VideoInfoSpec{kPublicYuy2Width, kPublicYuy2Height,
                                                       VideoInfo::CS_YV16, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_public_yv16_frame(source_frame);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  ConvertYV16ToYUY2 filter(source, environment.get());
  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YUY2);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(), kPublicYuy2Width * 2);
  const auto* output_bytes = output->GetReadPtr();
  const auto* source_y = source_frame->GetReadPtr(PLANAR_Y);
  const auto* source_u = source_frame->GetReadPtr(PLANAR_U);
  const auto* source_v = source_frame->GetReadPtr(PLANAR_V);
  for (int y = 0; y < kPublicYuy2Height; ++y) {
    for (int pair = 0; pair < kPublicYuy2Width / 2; ++pair) {
      const auto output_offset = y * output->GetPitch() + pair * 4;
      EXPECT_EQ(output_bytes[output_offset],
                source_y[y * source_frame->GetPitch(PLANAR_Y) + pair * 2])
          << "Y first row=" << y << " pair=" << pair;
      EXPECT_EQ(output_bytes[output_offset + 1],
                source_u[y * source_frame->GetPitch(PLANAR_U) + pair])
          << "U row=" << y << " pair=" << pair;
      EXPECT_EQ(output_bytes[output_offset + 2],
                source_y[y * source_frame->GetPitch(PLANAR_Y) + pair * 2 + 1])
          << "Y second row=" << y << " pair=" << pair;
      EXPECT_EQ(output_bytes[output_offset + 3],
                source_v[y * source_frame->GetPitch(PLANAR_V) + pair])
          << "V row=" << y << " pair=" << pair;
    }
  }
  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertYuy2ToY8, ExtractsLumaBytesFromPackedRows) {
  if (!CpuFeatures::detect().supports(IsaRequirement::Sse2)) {
    GTEST_SKIP() << "host does not support sse2";
  }
  run_yuy2_to_y8_case(make_yuy2_conversion_case("Yuy2ToY8", 32, 5, 80, 48, 32, "a4d7587a8bc4d848"));
}

TEST(ConvertYuy2ToYv16, SplitsPackedChannelsIntoPlanarRows) {
  if (!CpuFeatures::detect().supports(IsaRequirement::Sse2)) {
    GTEST_SKIP() << "host does not support sse2";
  }
  run_yuy2_to_yv16_case(make_yuy2_conversion_case(
      "Yuy2ToYv16", 32, 5, 80, 48, 32, "a4d7587a8bc4d848", "cad35c647b9798e8", "52f89684a5e68efa"));
}

TEST(ConvertYv16ToYuy2, InterleavesPlanarChannelsIntoPackedRows) {
  if (!CpuFeatures::detect().supports(IsaRequirement::Sse2)) {
    GTEST_SKIP() << "host does not support sse2";
  }
  run_yv16_to_yuy2_case(
      make_yuy2_conversion_case("Yv16ToYuy2", 32, 5, 80, 48, 32, {}, {}, {}, "990a205340cb835c"));
}

}  // namespace
}  // namespace avsut::test
