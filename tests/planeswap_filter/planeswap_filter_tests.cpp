#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_PLANESWAP_UNDEF_AVS_UNUSED
#endif
#include "filters/planeswap.h"
#ifdef AVSUT_PLANESWAP_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_PLANESWAP_UNDEF_AVS_UNUSED
#endif

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSnapshot;
using avsut::test::make_video_info;
using avsut::test::read_frame_plane_active;
using avsut::test::StaticFrameClip;
using avsut::test::VideoInfoSpec;
using avsut::test::write_frame_plane;

template <typename Pixel>
void write_values(PVideoFrame& frame, int plane, int width, int height,
                  const std::array<Pixel, 8>& values) {
  const int pitch = frame->GetPitch(plane);
  for (int y = 0; y < height; ++y) {
    auto* row = reinterpret_cast<Pixel*>(frame->GetWritePtr(plane) + y * pitch);
    for (int x = 0; x < width; ++x) {
      row[x] = values[static_cast<std::size_t>(x) % values.size()];
    }
  }
}

void expect_plane_equal(const PVideoFrame& expected, int expected_plane, const PVideoFrame& actual,
                        int actual_plane, int width, int height) {
  const int expected_pitch = expected->GetPitch(expected_plane);
  const int actual_pitch = actual->GetPitch(actual_plane);
  for (int y = 0; y < height; ++y) {
    const auto* expected_row = expected->GetReadPtr(expected_plane) + y * expected_pitch;
    const auto* actual_row = actual->GetReadPtr(actual_plane) + y * actual_pitch;
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(actual_row[x], expected_row[x]) << "x=" << x << " y=" << y;
    }
  }
}

TEST(PlaneSwap, SwapsPlanarUvPointersForYv24) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(source, 0xb2, PLANAR_U);
  fill_plane_full_pitch(source, 0xc3, PLANAR_V);
  const std::array<std::uint8_t, 8> luma_values{0, 16, 32, 64, 128, 192, 235, 255};
  const std::array<std::uint8_t, 8> u_values{1, 3, 5, 7, 11, 13, 17, 19};
  const std::array<std::uint8_t, 8> v_values{2, 4, 6, 8, 12, 14, 18, 20};
  write_values<std::uint8_t>(source, PLANAR_Y, vi.width, vi.height, luma_values);
  write_values<std::uint8_t>(source, PLANAR_U, vi.width, vi.height, u_values);
  write_values<std::uint8_t>(source, PLANAR_V, vi.width, vi.height, v_values);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  SwapUV filter(clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_plane_equal(source, PLANAR_Y, output, PLANAR_Y, vi.width, vi.height);
  expect_plane_equal(source, PLANAR_V, output, PLANAR_U, vi.width, vi.height);
  expect_plane_equal(source, PLANAR_U, output, PLANAR_V, vi.width, vi.height);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(PlaneSwap, SwapsYv16PlanesWithHorizontalOnlySubsampling) {
  AviSynthEnvironment environment;
  constexpr int width = 10;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x41, PLANAR_Y);
  fill_plane_full_pitch(source, 0x52, PLANAR_U);
  fill_plane_full_pitch(source, 0x63, PLANAR_V);
  write_frame_plane<std::uint8_t>(source, PLANAR_Y,
                                  [](int x, int y) { return 7 + x * 13 + y * 31; });
  write_frame_plane<std::uint8_t>(source, PLANAR_U,
                                  [](int x, int y) { return 11 + x * 17 + y * 19; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V,
                                  [](int x, int y) { return 211 - x * 7 - y * 23; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  SwapUV filter(clip, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  EXPECT_EQ(output->GetRowSize(PLANAR_U), width / 2);
  EXPECT_EQ(output->GetHeight(PLANAR_U), height);
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_Y),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_Y));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_U),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_V));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_V),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_U));
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(PlaneSwap, SwapsYuv420P16PlanesWithoutChangingComponentStorage) {
  AviSynthEnvironment environment;
  constexpr int width = 10;
  constexpr int height = 4;
  const auto vi =
      make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUV420P16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x71, PLANAR_Y);
  fill_plane_full_pitch(source, 0x82, PLANAR_U);
  fill_plane_full_pitch(source, 0x93, PLANAR_V);
  write_frame_plane<std::uint16_t>(source, PLANAR_Y,
                                   [](int x, int y) { return 900 + x * 401 + y * 701; });
  write_frame_plane<std::uint16_t>(source, PLANAR_U,
                                   [](int x, int y) { return 12000 + x * 503 + y * 607; });
  write_frame_plane<std::uint16_t>(source, PLANAR_V,
                                   [](int x, int y) { return 51000 - x * 307 - y * 409; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  SwapUV filter(clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  EXPECT_EQ(output->GetRowSize(PLANAR_U) / static_cast<int>(sizeof(std::uint16_t)), width / 2);
  EXPECT_EQ(output->GetHeight(PLANAR_U), height / 2);
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, PLANAR_Y),
            read_frame_plane_active<std::uint16_t>(source, PLANAR_Y));
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, PLANAR_U),
            read_frame_plane_active<std::uint16_t>(source, PLANAR_V));
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, PLANAR_V),
            read_frame_plane_active<std::uint16_t>(source, PLANAR_U));
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(PlaneSwap, SwapsYuva420PlanesAndPreservesAlphaPlane) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x20 + plane * 0x17), plane);
  }
  write_frame_plane<std::uint8_t>(source, PLANAR_Y,
                                  [](int x, int y) { return 5 + x * 11 + y * 23; });
  write_frame_plane<std::uint8_t>(source, PLANAR_U,
                                  [](int x, int y) { return 17 + x * 13 + y * 7; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V,
                                  [](int x, int y) { return 229 - x * 9 - y * 5; });
  write_frame_plane<std::uint8_t>(source, PLANAR_A,
                                  [](int x, int y) { return 3 + x * 31 + y * 19; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  SwapUV filter(clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_U),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_V));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_V),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_U));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_A),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_A));
  EXPECT_EQ(output->GetHeight(PLANAR_U), height / 2);
  EXPECT_EQ(output->GetHeight(PLANAR_A), height);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(PlaneSwap, DoubleSwapRestoresAllYuvaPlanes) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x30 + plane * 0x19), plane);
  }
  write_frame_plane<std::uint8_t>(source, PLANAR_Y,
                                  [](int x, int y) { return 7 + x * 13 + y * 29; });
  write_frame_plane<std::uint8_t>(source, PLANAR_U,
                                  [](int x, int y) { return 23 + x * 17 + y * 11; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V,
                                  [](int x, int y) { return 211 - x * 9 - y * 7; });
  write_frame_plane<std::uint8_t>(source, PLANAR_A,
                                  [](int x, int y) { return 31 + x * 19 + y * 23; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip_impl = new StaticFrameClip(vi, source);
  const PClip source_clip(source_clip_impl);
  const PClip swapped(new SwapUV(source_clip, environment.get()));
  const PClip restored(new SwapUV(swapped, environment.get()));

  EXPECT_EQ(restored->SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = restored->GetFrame(0, environment.get());

  EXPECT_EQ(FrameSnapshot::capture(output, vi), source_before);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(CombinePlanesFilter, CombinesGreyClipsIntoYuv444Planes) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_Y8, 1, 25, 1});
  std::array<PVideoFrame, 3> frames;
  std::array<std::uint8_t, 3> bases{13, 71, 149};
  std::array<StaticFrameClip*, 3> clip_impls{};
  std::array<PClip, 3> clips;
  std::array<FrameSnapshot, 3> snapshots;
  for (std::size_t i = 0; i < frames.size(); ++i) {
    frames[i] = environment.get()->NewVideoFrame(vi);
    fill_plane_full_pitch(frames[i], static_cast<std::uint8_t>(0x91 + i), PLANAR_Y);
    write_frame_plane<std::uint8_t>(frames[i], PLANAR_Y,
                                    [base = bases[i]](int x, int y) {
                                      return base + x * 9 + y * 17;
                                    });
    snapshots[i] = FrameSnapshot::capture(frames[i], vi);
    auto* impl = new StaticFrameClip(vi, frames[i]);
    clips[i] = PClip(impl);
    clip_impls[i] = impl;
  }

  CombinePlanes filter(clips[0], clips[1], clips[2], PClip(), PClip(), "YUV", "", "",
                       environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int plane_index = 0; plane_index < 3; ++plane_index) {
    EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, plane_index == 0 ? PLANAR_Y
                                                                            : plane_index == 1 ? PLANAR_U
                                                                                              : PLANAR_V),
              read_frame_plane_active<std::uint8_t>(frames[static_cast<std::size_t>(plane_index)],
                                                    PLANAR_Y))
        << "plane=" << plane_index;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  for (const auto& clip_impl : clip_impls) {
    EXPECT_EQ(clip_impl->frame_requests(), std::vector<int>{0});
  }
  for (std::size_t i = 0; i < frames.size(); ++i) {
    EXPECT_EQ(FrameSnapshot::capture(frames[i], vi), snapshots[i]) << "source=" << i;
  }
}

TEST(CombinePlanesFilter, ReordersPlanesThroughSingleClipSubframePath) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(source, 0xb2, PLANAR_U);
  fill_plane_full_pitch(source, 0xc3, PLANAR_V);
  write_frame_plane<std::uint8_t>(source, PLANAR_Y,
                                  [](int x, int y) { return 11 + x * 7 + y * 19; });
  write_frame_plane<std::uint8_t>(source, PLANAR_U,
                                  [](int x, int y) { return 61 + x * 9 + y * 13; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V,
                                  [](int x, int y) { return 191 - x * 5 - y * 11; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip_impl = new StaticFrameClip(vi, source);
  const PClip clip(source_clip_impl);

  CombinePlanes filter(clip, PClip(), PClip(), PClip(), PClip(), "YVU", "YUV", "YV24",
                       environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_Y),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_Y));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_U),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_V));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_V),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_U));
  const PVideoFrame repeat = filter.GetFrame(0, environment.get());
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(repeat, PLANAR_Y),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_Y));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(repeat, PLANAR_U),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_V));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(repeat, PLANAR_V),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_U));
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_NE(repeat->CheckMemory(), 1);
  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(CombinePlanesFilter, RejectsInvalidTargetPlaneCountAndDimensionsBeforeFrameRequest) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_Y8, 1, 25, 1});
  const auto short_vi = make_video_info(
      VideoInfoSpec{width - 1, height, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  PVideoFrame short_source = environment.get()->NewVideoFrame(short_vi);
  fill_plane_full_pitch(source, 0x91, PLANAR_Y);
  fill_plane_full_pitch(short_source, 0xa2, PLANAR_Y);
  const auto source_before = FrameSnapshot::capture(source, vi);
  const auto short_source_before = FrameSnapshot::capture(short_source, short_vi);
  auto* source_clip_impl = new StaticFrameClip(vi, source);
  auto* short_clip_impl = new StaticFrameClip(short_vi, short_source);
  const PClip source_clip(source_clip_impl);
  const PClip short_clip(short_clip_impl);

  EXPECT_THROW(
      {
        CombinePlanes filter(source_clip, PClip(), PClip(), PClip(), PClip(), "YQ", "", "",
                             environment.get());
      },
      AvisynthError);
  EXPECT_THROW(
      {
        CombinePlanes filter(source_clip, PClip(), PClip(), PClip(), PClip(), "YUV", "YQY", "",
                             environment.get());
      },
      AvisynthError);
  EXPECT_THROW(
      {
        CombinePlanes filter(source_clip, source_clip, source_clip, PClip(), PClip(), "YU", "", "",
                             environment.get());
      },
      AvisynthError);
  EXPECT_THROW(
      {
        CombinePlanes filter(source_clip, short_clip, PClip(), PClip(), PClip(), "YUV", "", "",
                             environment.get());
      },
      AvisynthError);
  EXPECT_TRUE(source_clip_impl->frame_requests().empty());
  EXPECT_TRUE(short_clip_impl->frame_requests().empty());
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
  EXPECT_EQ(FrameSnapshot::capture(short_source, short_vi), short_source_before);
}

class SwapUvToYTest : public ::testing::TestWithParam<int> {};

TEST_P(SwapUvToYTest, ExtractsSubsampledPlanarChannel) {
  const int mode = GetParam();
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 4, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xd4, PLANAR_Y);
  fill_plane_full_pitch(source, 0xe5, PLANAR_U);
  fill_plane_full_pitch(source, 0xf6, PLANAR_V);
  write_values<std::uint8_t>(source, PLANAR_Y, vi.width, vi.height,
                             std::array<std::uint8_t, 8>{0, 16, 32, 64, 128, 192, 235, 255});
  const std::array<std::uint8_t, 8> u_values{11, 22, 33, 44, 55, 66, 77, 88};
  const std::array<std::uint8_t, 8> v_values{101, 102, 103, 104, 105, 106, 107, 108};
  write_values<std::uint8_t>(source, PLANAR_U, source->GetRowSize(PLANAR_U),
                             source->GetHeight(PLANAR_U), u_values);
  write_values<std::uint8_t>(source, PLANAR_V, source->GetRowSize(PLANAR_V),
                             source->GetHeight(PLANAR_V), v_values);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  SwapUVToY filter(clip, mode, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetRowSize(), vi.width / 2);
  ASSERT_EQ(output->GetHeight(), vi.height / 2);
  const int source_plane = mode == SwapUVToY::UToY8 ? PLANAR_U : PLANAR_V;
  expect_plane_equal(source, source_plane, output, PLANAR_Y, vi.width / 2, vi.height / 2);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

class SwapUvToY16Test : public ::testing::TestWithParam<int> {};

TEST_P(SwapUvToY16Test, ExtractsSixteenBitYuv420ChannelWithSubsampledGeometry) {
  const int mode = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 10;
  constexpr int height = 4;
  const auto vi =
      make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUV420P16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x91, PLANAR_Y);
  fill_plane_full_pitch(source, 0xa2, PLANAR_U);
  fill_plane_full_pitch(source, 0xb3, PLANAR_V);
  write_frame_plane<std::uint16_t>(source, PLANAR_U,
                                   [](int x, int y) { return 1000 + x * 401 + y * 701; });
  write_frame_plane<std::uint16_t>(source, PLANAR_V,
                                   [](int x, int y) { return 60000 - x * 307 - y * 503; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  SwapUVToY filter(clip, mode, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  EXPECT_EQ(output->GetRowSize() / static_cast<int>(sizeof(std::uint16_t)), width / 2);
  EXPECT_EQ(output->GetHeight(), height / 2);
  const int source_plane = mode == SwapUVToY::UToY8 ? PLANAR_U : PLANAR_V;
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, DEFAULT_PLANE),
            read_frame_plane_active<std::uint16_t>(source, source_plane));
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(Channels, SwapUvToY16Test,
                         ::testing::Values(SwapUVToY::UToY8, SwapUVToY::VToY8),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return info.param == SwapUVToY::UToY8 ? "UToY8" : "VToY8";
                         });

class PackedRgbExtractTest : public ::testing::TestWithParam<int> {};

TEST_P(PackedRgbExtractTest, ExtractsBgr24ChannelInLogicalTopDownOrder) {
  const int mode = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xc4, DEFAULT_PLANE);
  write_frame_plane<std::uint8_t>(source, DEFAULT_PLANE, [&](int byte, int raw_y) {
    const int x = byte / 3;
    const int channel = byte % 3;
    const int logical_y = height - 1 - raw_y;
    return static_cast<std::uint8_t>(10 + x * 17 + logical_y * 29 + channel * 41);
  });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  SwapUVToY filter(clip, mode, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const int channel = mode == SwapUVToY::BToY8 ? 0 : mode == SwapUVToY::GToY8 ? 1 : 2;
  const auto values = read_frame_plane_active<std::uint8_t>(output, DEFAULT_PLANE);
  ASSERT_EQ(values.size(), static_cast<std::size_t>(width * height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(values[static_cast<std::size_t>(y * width + x)],
                static_cast<std::uint8_t>(10 + x * 17 + y * 29 + channel * 41))
          << "channel=" << channel << " x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(Channels, PackedRgbExtractTest,
                         ::testing::Values(SwapUVToY::BToY8, SwapUVToY::GToY8,
                                           SwapUVToY::RToY8),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return info.param == SwapUVToY::BToY8
                                      ? "BToY8"
                                      : info.param == SwapUVToY::GToY8 ? "GToY8" : "RToY8";
                         });

class PackedRgb48ExtractTest : public ::testing::TestWithParam<int> {};

TEST_P(PackedRgb48ExtractTest, ExtractsBgr48ChannelWithSixteenBitSamples) {
  const int mode = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 3;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR48, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xd5, DEFAULT_PLANE);
  write_frame_plane<std::uint16_t>(source, DEFAULT_PLANE, [&](int component, int raw_y) {
    const int x = component / 3;
    const int channel = component % 3;
    const int logical_y = height - 1 - raw_y;
    return static_cast<std::uint16_t>(1000 + x * 7001 + logical_y * 3001 + channel * 5003);
  });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  SwapUVToY filter(clip, mode, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const int channel = mode == SwapUVToY::BToY8 ? 0 : mode == SwapUVToY::GToY8 ? 1 : 2;
  const auto values = read_frame_plane_active<std::uint16_t>(output, DEFAULT_PLANE);
  ASSERT_EQ(values.size(), static_cast<std::size_t>(width * height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      EXPECT_EQ(values[static_cast<std::size_t>(y * width + x)],
                static_cast<std::uint16_t>(1000 + x * 7001 + y * 3001 + channel * 5003))
          << "channel=" << channel << " x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(Channels, PackedRgb48ExtractTest,
                         ::testing::Values(SwapUVToY::BToY8, SwapUVToY::GToY8,
                                           SwapUVToY::RToY8),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return info.param == SwapUVToY::BToY8
                                      ? "BToY8"
                                      : info.param == SwapUVToY::GToY8 ? "GToY8" : "RToY8";
                         });

class PlanarRgb16ExtractTest : public ::testing::TestWithParam<int> {};

TEST_P(PlanarRgb16ExtractTest, ExtractsGbrPlaneAsSixteenBitY) {
  const int mode = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBP16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x40 + plane * 0x13), plane);
  }
  write_frame_plane<std::uint16_t>(source, PLANAR_G,
                                   [](int x, int y) { return 3000 + x * 401 + y * 701; });
  write_frame_plane<std::uint16_t>(source, PLANAR_B,
                                   [](int x, int y) { return 9000 + x * 503 + y * 607; });
  write_frame_plane<std::uint16_t>(source, PLANAR_R,
                                   [](int x, int y) { return 15000 + x * 307 + y * 409; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  SwapUVToY filter(clip, mode, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const int source_plane = mode == SwapUVToY::BToY8
                               ? PLANAR_B
                               : mode == SwapUVToY::GToY8 ? PLANAR_G : PLANAR_R;
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, DEFAULT_PLANE),
            read_frame_plane_active<std::uint16_t>(source, source_plane));
  EXPECT_EQ(output->GetRowSize() / static_cast<int>(sizeof(std::uint16_t)), width);
  EXPECT_EQ(output->GetHeight(), height);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(Channels, PlanarRgb16ExtractTest,
                         ::testing::Values(SwapUVToY::BToY8, SwapUVToY::GToY8,
                                           SwapUVToY::RToY8),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return info.param == SwapUVToY::BToY8
                                      ? "BToY8"
                                      : info.param == SwapUVToY::GToY8 ? "GToY8" : "RToY8";
                         });

INSTANTIATE_TEST_SUITE_P(Channels, SwapUvToYTest,
                         ::testing::Values(SwapUVToY::UToY8, SwapUVToY::VToY8),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return info.param == SwapUVToY::UToY8 ? "UToY8" : "VToY8";
                         });

}  // namespace
