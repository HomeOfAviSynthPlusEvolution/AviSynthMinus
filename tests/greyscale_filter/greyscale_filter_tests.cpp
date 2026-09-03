#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_GREYSCALE_UNDEF_AVS_UNUSED
#endif
#include "filters/greyscale.h"
#ifdef AVSUT_GREYSCALE_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_GREYSCALE_UNDEF_AVS_UNUSED
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

template <typename Pixel>
Pixel rec601_greyscale(Pixel blue, Pixel green, Pixel red) {
  constexpr std::int64_t kBlue = 3736;
  constexpr std::int64_t kGreen = 19234;
  constexpr std::int64_t kRed = 9798;
  constexpr std::int64_t kRound = 1 << 14;
  const auto value = kBlue * static_cast<std::int64_t>(blue) +
                     kGreen * static_cast<std::int64_t>(green) +
                     kRed * static_cast<std::int64_t>(red) + kRound;
  return static_cast<Pixel>(value >> 15);
}

template <typename Pixel>
void expect_neutral_plane(const PVideoFrame& frame, int plane, Pixel expected,
                          const char* label) {
  const auto values = read_frame_plane_active<Pixel>(frame, plane);
  ASSERT_FALSE(values.empty()) << label;
  for (std::size_t index = 0; index < values.size(); ++index) {
    EXPECT_EQ(values[index], expected) << label << " active_index=" << index;
  }
}

TEST(Greyscale, NeutralizesPlanarChromaAndPreservesLuma) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 4, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(source, 0xb2, PLANAR_U);
  fill_plane_full_pitch(source, 0xc3, PLANAR_V);
  write_values<std::uint8_t>(source, PLANAR_Y, vi.width, vi.height,
                             std::array<std::uint8_t, 8>{0, 16, 32, 64, 128, 192, 235, 255});
  write_values<std::uint8_t>(source, PLANAR_U, source->GetRowSize(PLANAR_U),
                             source->GetHeight(PLANAR_U),
                             std::array<std::uint8_t, 8>{0, 32, 96, 127, 128, 160, 224, 255});
  write_values<std::uint8_t>(source, PLANAR_V, source->GetRowSize(PLANAR_V),
                             source->GetHeight(PLANAR_V),
                             std::array<std::uint8_t, 8>{255, 224, 160, 128, 127, 96, 32, 0});
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Greyscale filter(clip, nullptr, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const int luma_pitch = output->GetPitch(PLANAR_Y);
  const int source_luma_pitch = source->GetPitch(PLANAR_Y);
  for (int y = 0; y < vi.height; ++y) {
    const auto* output_row = output->GetReadPtr(PLANAR_Y) + y * luma_pitch;
    const auto* source_row = source->GetReadPtr(PLANAR_Y) + y * source_luma_pitch;
    EXPECT_EQ(std::vector<std::uint8_t>(output_row, output_row + vi.width),
              std::vector<std::uint8_t>(source_row, source_row + vi.width))
        << "luma row=" << y;
  }
  for (const int plane : {PLANAR_U, PLANAR_V}) {
    const int width = output->GetRowSize(plane);
    const int height = output->GetHeight(plane);
    const int pitch = output->GetPitch(plane);
    for (int y = 0; y < height; ++y) {
      const auto* row = output->GetReadPtr(plane) + y * pitch;
      for (int x = 0; x < width; ++x) {
        EXPECT_EQ(row[x], 128) << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Greyscale, NeutralizesInterleavedChromaForYuy2) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 2, VideoInfo::CS_YUY2, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xd4, DEFAULT_PLANE);
  for (int y = 0; y < vi.height; ++y) {
    auto* row = source->GetWritePtr() + y * source->GetPitch();
    for (int x = 0; x < vi.width; ++x) {
      row[x * 2] = static_cast<std::uint8_t>(7 + x * 23 + y * 17);
      row[x * 2 + 1] = static_cast<std::uint8_t>(19 + x * 29 + y * 11);
    }
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Greyscale filter(clip, nullptr, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (int y = 0; y < vi.height; ++y) {
    const auto* row = output->GetReadPtr() + y * output->GetPitch();
    const auto* source_row = source->GetReadPtr() + y * source->GetPitch();
    for (int x = 0; x < vi.width; ++x) {
      EXPECT_EQ(row[x * 2], source_row[x * 2]) << "luma x=" << x << " y=" << y;
      EXPECT_EQ(row[x * 2 + 1], 128) << "chroma x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Greyscale, NeutralizesYv16ChromaAndKeepsHorizontalOnlyGeometry) {
  AviSynthEnvironment environment;
  constexpr int width = 10;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(source, 0xb2, PLANAR_U);
  fill_plane_full_pitch(source, 0xc3, PLANAR_V);
  write_frame_plane<std::uint8_t>(source, PLANAR_Y,
                                  [](int x, int y) { return 7 + x * 19 + y * 23; });
  write_frame_plane<std::uint8_t>(source, PLANAR_U,
                                  [](int x, int y) { return 31 + x * 17 + y * 29; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V,
                                  [](int x, int y) { return 227 - x * 13 - y * 11; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Greyscale filter(clip, nullptr, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  EXPECT_EQ(output->GetRowSize(PLANAR_Y), width);
  EXPECT_EQ(output->GetHeight(PLANAR_Y), height);
  EXPECT_EQ(output->GetRowSize(PLANAR_U), width / 2);
  EXPECT_EQ(output->GetRowSize(PLANAR_V), width / 2);
  EXPECT_EQ(output->GetHeight(PLANAR_U), height);
  EXPECT_EQ(output->GetHeight(PLANAR_V), height);
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_Y),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_Y));
  expect_neutral_plane<std::uint8_t>(output, PLANAR_U, 128, "YV16 U");
  expect_neutral_plane<std::uint8_t>(output, PLANAR_V, 128, "YV16 V");
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Greyscale, NeutralizesYuv420P16ChromaAndKeepsSixteenBitLuma) {
  AviSynthEnvironment environment;
  constexpr int width = 10;
  constexpr int height = 4;
  const auto vi =
      make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUV420P16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x91, PLANAR_Y);
  fill_plane_full_pitch(source, 0xa2, PLANAR_U);
  fill_plane_full_pitch(source, 0xb3, PLANAR_V);
  write_frame_plane<std::uint16_t>(source, PLANAR_Y,
                                   [](int x, int y) { return 1000 + x * 701 + y * 1301; });
  write_frame_plane<std::uint16_t>(source, PLANAR_U,
                                   [](int x, int y) { return 4000 + x * 503 + y * 607; });
  write_frame_plane<std::uint16_t>(source, PLANAR_V,
                                   [](int x, int y) { return 61000 - x * 401 - y * 509; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Greyscale filter(clip, nullptr, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  EXPECT_EQ(output->GetRowSize(PLANAR_U) / static_cast<int>(sizeof(std::uint16_t)), width / 2);
  EXPECT_EQ(output->GetHeight(PLANAR_U), height / 2);
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, PLANAR_Y),
            read_frame_plane_active<std::uint16_t>(source, PLANAR_Y));
  expect_neutral_plane<std::uint16_t>(output, PLANAR_U, 32768, "YUV420P16 U");
  expect_neutral_plane<std::uint16_t>(output, PLANAR_V, 32768, "YUV420P16 V");
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Greyscale, NeutralizesYuv444FloatChromaAndPreservesLuma) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi =
      make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUV444PS, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x81, PLANAR_Y);
  fill_plane_full_pitch(source, 0x92, PLANAR_U);
  fill_plane_full_pitch(source, 0xa3, PLANAR_V);
  write_frame_plane<float>(source, PLANAR_Y,
                           [](int x, int y) { return 0.05F + x * 0.11F + y * 0.07F; });
  write_frame_plane<float>(source, PLANAR_U,
                           [](int x, int y) { return -0.4F + x * 0.13F + y * 0.09F; });
  write_frame_plane<float>(source, PLANAR_V,
                           [](int x, int y) { return 0.4F - x * 0.08F - y * 0.06F; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Greyscale filter(clip, nullptr, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  EXPECT_EQ(read_frame_plane_active<float>(output, PLANAR_Y),
            read_frame_plane_active<float>(source, PLANAR_Y));
  expect_neutral_plane<float>(output, PLANAR_U, 0.0F, "YUV444PS U");
  expect_neutral_plane<float>(output, PLANAR_V, 0.0F, "YUV444PS V");
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Greyscale, NeutralizesYuva420ChromaAndPreservesAlpha) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x40 + plane * 0x11), plane);
  }
  write_frame_plane<std::uint8_t>(source, PLANAR_Y,
                                  [](int x, int y) { return 9 + x * 21 + y * 31; });
  write_frame_plane<std::uint8_t>(source, PLANAR_U,
                                  [](int x, int y) { return 17 + x * 33 + y * 7; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V,
                                  [](int x, int y) { return 239 - x * 29 - y * 5; });
  write_frame_plane<std::uint8_t>(source, PLANAR_A,
                                  [](int x, int y) { return 3 + x * 37 + y * 19; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  const auto alpha_before = read_frame_plane_active<std::uint8_t>(source, PLANAR_A);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Greyscale filter(clip, nullptr, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_Y),
            read_frame_plane_active<std::uint8_t>(source, PLANAR_Y));
  EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, PLANAR_A), alpha_before);
  expect_neutral_plane<std::uint8_t>(output, PLANAR_U, 128, "YUVA420 U");
  expect_neutral_plane<std::uint8_t>(output, PLANAR_V, 128, "YUVA420 V");
  EXPECT_EQ(output->GetHeight(PLANAR_U), height / 2);
  EXPECT_EQ(output->GetHeight(PLANAR_A), height);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Greyscale, AppliesRec601ToPackedBgr24WithoutTouchingRowPadding) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 2;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_BGR24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xd4, DEFAULT_PLANE);
  write_frame_plane<std::uint8_t>(source, DEFAULT_PLANE, [](int byte, int y) {
    constexpr std::array<std::array<std::uint8_t, 3>, 5> pixels{{
        {{0, 0, 0}}, {{255, 0, 0}}, {{0, 255, 0}}, {{0, 0, 255}}, {{211, 97, 43}}}};
    return pixels[static_cast<std::size_t>(byte / 3)][static_cast<std::size_t>(byte % 3)] +
           static_cast<std::uint8_t>(y * 3);
  });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Greyscale filter(clip, nullptr, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const auto pixels = std::array<std::array<std::uint8_t, 3>, 5>{{
      {{0, 0, 0}}, {{255, 0, 0}}, {{0, 255, 0}}, {{0, 0, 255}}, {{211, 97, 43}}}};
  for (int y = 0; y < height; ++y) {
    const auto* row = output->GetReadPtr() + y * output->GetPitch();
    for (int x = 0; x < width; ++x) {
      const auto& pixel = pixels[static_cast<std::size_t>(x)];
      const auto expected = rec601_greyscale<std::uint8_t>(
          static_cast<std::uint8_t>(pixel[0] + y * 3),
          static_cast<std::uint8_t>(pixel[1] + y * 3),
          static_cast<std::uint8_t>(pixel[2] + y * 3));
      EXPECT_EQ(row[x * 3 + 0], expected) << "blue x=" << x << " y=" << y;
      EXPECT_EQ(row[x * 3 + 1], expected) << "green x=" << x << " y=" << y;
      EXPECT_EQ(row[x * 3 + 2], expected) << "red x=" << x << " y=" << y;
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(Greyscale, AppliesRec601ToPlanarRgb16InGbrPlaneOrder) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBP16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x50 + plane * 0x13), plane);
  }
  write_frame_plane<std::uint16_t>(source, PLANAR_B,
                                   [](int x, int y) { return 500 + x * 701 + y * 101; });
  write_frame_plane<std::uint16_t>(source, PLANAR_G,
                                   [](int x, int y) { return 1200 + x * 503 + y * 211; });
  write_frame_plane<std::uint16_t>(source, PLANAR_R,
                                   [](int x, int y) { return 3000 + x * 307 + y * 401; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Greyscale filter(clip, nullptr, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const auto blue = read_frame_plane_active<std::uint16_t>(source, PLANAR_B);
  const auto green = read_frame_plane_active<std::uint16_t>(source, PLANAR_G);
  const auto red = read_frame_plane_active<std::uint16_t>(source, PLANAR_R);
  const auto expected = [&]() {
    std::vector<std::uint16_t> values;
    values.reserve(blue.size());
    for (std::size_t index = 0; index < blue.size(); ++index) {
      values.push_back(rec601_greyscale(blue[index], green[index], red[index]));
    }
    return values;
  }();
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, PLANAR_B), expected);
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, PLANAR_G), expected);
  EXPECT_EQ(read_frame_plane_active<std::uint16_t>(output, PLANAR_R), expected);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>({0, 0}));
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

}  // namespace
