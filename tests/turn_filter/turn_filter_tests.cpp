#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_TURN_UNDEF_AVS_UNUSED
#endif
#include "filters/turn.h"
#ifdef AVSUT_TURN_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_TURN_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_helper.h"

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSnapshot;
using avsut::test::get_frame_property_int;
using avsut::test::make_video_info;
using avsut::test::read_frame_plane_active;
using avsut::test::set_frame_property_int;
using avsut::test::StaticFrameClip;
using avsut::test::VideoInfoSpec;
using avsut::test::write_frame_plane;

constexpr int kTurnLeft = 0;
constexpr int kTurnRight = 1;
constexpr int kTurn180 = 2;

const char* direction_name(int direction) {
  switch (direction) {
    case kTurnLeft:
      return "Left";
    case kTurnRight:
      return "Right";
    case kTurn180:
      return "Half";
    default:
      return "Unknown";
  }
}

void fill_pattern(PVideoFrame& frame, int plane, int width, int height, std::uint8_t base) {
  const int pitch = frame->GetPitch(plane);
  for (int y = 0; y < height; ++y) {
    auto* row = frame->GetWritePtr(plane) + y * pitch;
    for (int x = 0; x < width; ++x) {
      row[x] = static_cast<std::uint8_t>(base + x * 17 + y * 31);
    }
  }
}

void expect_rotated_plane(const PVideoFrame& source, const PVideoFrame& output, int plane,
                          int direction, int width, int height) {
  const int destination_width = direction == kTurn180 ? width : height;
  const int destination_height = direction == kTurn180 ? height : width;
  ASSERT_EQ(output->GetRowSize(plane), destination_width);
  ASSERT_EQ(output->GetHeight(plane), destination_height);

  const int source_pitch = source->GetPitch(plane);
  const int output_pitch = output->GetPitch(plane);
  for (int destination_y = 0; destination_y < destination_height; ++destination_y) {
    const auto* output_row = output->GetReadPtr(plane) + destination_y * output_pitch;
    for (int destination_x = 0; destination_x < destination_width; ++destination_x) {
      int source_x = 0;
      int source_y = 0;
      if (direction == kTurnLeft) {
        source_x = width - 1 - destination_y;
        source_y = destination_x;
      } else if (direction == kTurnRight) {
        source_x = destination_y;
        source_y = height - 1 - destination_x;
      } else {
        source_x = width - 1 - destination_x;
        source_y = height - 1 - destination_y;
      }
      const auto* source_row = source->GetReadPtr(plane) + source_y * source_pitch;
      EXPECT_EQ(output_row[destination_x], source_row[source_x])
          << "direction=" << direction_name(direction) << " plane=" << plane
          << " x=" << destination_x << " y=" << destination_y;
    }
  }
}

template <typename Pixel>
void expect_rotated_typed_plane(const PVideoFrame& source, const PVideoFrame& output, int plane,
                                int direction) {
  const int source_width = source->GetRowSize(plane) / static_cast<int>(sizeof(Pixel));
  const int source_height = source->GetHeight(plane);
  const int destination_width = direction == kTurn180 ? source_width : source_height;
  const int destination_height = direction == kTurn180 ? source_height : source_width;
  ASSERT_EQ(output->GetRowSize(plane) / static_cast<int>(sizeof(Pixel)), destination_width);
  ASSERT_EQ(output->GetHeight(plane), destination_height);

  const int source_pitch = source->GetPitch(plane);
  const int output_pitch = output->GetPitch(plane);
  for (int destination_y = 0; destination_y < destination_height; ++destination_y) {
    const auto* output_row = reinterpret_cast<const Pixel*>(
        output->GetReadPtr(plane) + destination_y * output_pitch);
    for (int destination_x = 0; destination_x < destination_width; ++destination_x) {
      int source_x = 0;
      int source_y = 0;
      if (direction == kTurnLeft) {
        source_x = source_width - 1 - destination_y;
        source_y = destination_x;
      } else if (direction == kTurnRight) {
        source_x = destination_y;
        source_y = source_height - 1 - destination_x;
      } else {
        source_x = source_width - 1 - destination_x;
        source_y = source_height - 1 - destination_y;
      }
      const auto* source_row = reinterpret_cast<const Pixel*>(
          source->GetReadPtr(plane) + source_y * source_pitch);
      EXPECT_EQ(output_row[destination_x], source_row[source_x])
          << "direction=" << direction_name(direction) << " plane=" << plane
          << " x=" << destination_x << " y=" << destination_y;
    }
  }
}

template <typename Pixel>
void expect_packed_rotation(const PVideoFrame& source, const PVideoFrame& output, int direction,
                            int components) {
  const int source_width = source->GetRowSize() / (components * static_cast<int>(sizeof(Pixel)));
  const int source_height = source->GetHeight();
  const int destination_width = direction == kTurn180 ? source_width : source_height;
  const int destination_height = direction == kTurn180 ? source_height : source_width;
  ASSERT_EQ(output->GetRowSize() / (components * static_cast<int>(sizeof(Pixel))),
            destination_width);
  ASSERT_EQ(output->GetHeight(), destination_height);

  const auto read_pixel = [](const PVideoFrame& frame, int x, int y, int component,
                             int component_count) {
    const int logical_row = frame->GetHeight() - 1 - y;
    const auto* row = reinterpret_cast<const Pixel*>(
        frame->GetReadPtr() + logical_row * frame->GetPitch());
    return row[x * component_count + component];
  };

  for (int destination_y = 0; destination_y < destination_height; ++destination_y) {
    for (int destination_x = 0; destination_x < destination_width; ++destination_x) {
      int source_x = 0;
      int source_y = 0;
      if (direction == kTurnLeft) {
        source_x = source_width - 1 - destination_y;
        source_y = destination_x;
      } else if (direction == kTurnRight) {
        source_x = destination_y;
        source_y = source_height - 1 - destination_x;
      } else {
        source_x = source_width - 1 - destination_x;
        source_y = source_height - 1 - destination_y;
      }
      for (int component = 0; component < components; ++component) {
        EXPECT_EQ(read_pixel(output, destination_x, destination_y, component, components),
                  read_pixel(source, source_x, source_y, component, components))
            << "direction=" << direction_name(direction) << " component=" << component
            << " x=" << destination_x << " y=" << destination_y;
      }
    }
  }
}

template <typename Pixel>
void expect_plane_constant(const PVideoFrame& frame, int plane, Pixel value) {
  const auto values = read_frame_plane_active<Pixel>(frame, plane);
  ASSERT_FALSE(values.empty());
  for (std::size_t index = 0; index < values.size(); ++index) {
    EXPECT_EQ(values[index], value) << "plane=" << plane << " active_index=" << index;
  }
}

class TurnFilterTest : public ::testing::TestWithParam<int> {};

TEST_P(TurnFilterTest, RotatesAllYuv444PlanesWithExpectedGeometry) {
  const int direction = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(source, 0xb2, PLANAR_U);
  fill_plane_full_pitch(source, 0xc3, PLANAR_V);
  fill_pattern(source, PLANAR_Y, width, height, 3);
  fill_pattern(source, PLANAR_U, width, height, 67);
  fill_pattern(source, PLANAR_V, width, height, 131);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Turn filter(clip, direction, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    expect_rotated_plane(source, output, plane, direction, width, height);
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(Directions, TurnFilterTest,
                         ::testing::Values(kTurnLeft, kTurnRight, kTurn180),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return direction_name(info.param);
                         });

TEST(TurnFilter, LeftThenRightRestoresYv24ActivePlanes) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(source, 0xb2, PLANAR_U);
  fill_plane_full_pitch(source, 0xc3, PLANAR_V);
  fill_pattern(source, PLANAR_Y, width, height, 3);
  fill_pattern(source, PLANAR_U, width, height, 67);
  fill_pattern(source, PLANAR_V, width, height, 131);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip_impl = new StaticFrameClip(vi, source);
  const PClip source_clip(source_clip_impl);
  const PClip left(new Turn(source_clip, kTurnLeft, environment.get()));
  const PClip restored(new Turn(left, kTurnRight, environment.get()));

  EXPECT_EQ(restored->SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = restored->GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, plane),
              read_frame_plane_active<std::uint8_t>(source, plane))
        << "plane=" << plane;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(TurnFilter, PreservesChromaRangeAndFieldPropertiesOnRotatedFrame) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi =
      make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x50 + plane * 19), plane);
  }
  set_frame_property_int(environment.get(), source, "_ChromaLocation", AVS_CHROMA_CENTER);
  set_frame_property_int(environment.get(), source, "_ColorRange", AVS_COLORRANGE_LIMITED);
  set_frame_property_int(environment.get(), source, "_FieldBased", 0);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Turn filter(clip, kTurnRight, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const auto& property : std::array<std::pair<const char*, int>, 3>{
           std::pair{"_ChromaLocation", AVS_CHROMA_CENTER},
           std::pair{"_ColorRange", AVS_COLORRANGE_LIMITED},
           std::pair{"_FieldBased", 0}}) {
    const auto actual = get_frame_property_int(environment.get(), output, property.first);
    ASSERT_TRUE(actual.has_value()) << property.first;
    EXPECT_EQ(*actual, property.second) << property.first;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

class TurnYuv420P16Test : public ::testing::TestWithParam<int> {};

TEST_P(TurnYuv420P16Test, RotatesSixteenBitSubsampledPlanes) {
  const int direction = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 6;
  constexpr int height = 4;
  const auto vi =
      make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUV420P16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x41, PLANAR_Y);
  fill_plane_full_pitch(source, 0x52, PLANAR_U);
  fill_plane_full_pitch(source, 0x63, PLANAR_V);
  write_frame_plane<std::uint16_t>(source, PLANAR_Y,
                                   [](int x, int y) { return 1000 + x * 701 + y * 1301; });
  write_frame_plane<std::uint16_t>(source, PLANAR_U,
                                   [](int x, int y) { return 9000 + x * 503 + y * 607; });
  write_frame_plane<std::uint16_t>(source, PLANAR_V,
                                   [](int x, int y) { return 50000 - x * 307 - y * 409; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Turn filter(clip, direction, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    expect_rotated_typed_plane<std::uint16_t>(source, output, plane, direction);
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(Directions, TurnYuv420P16Test,
                         ::testing::Values(kTurnLeft, kTurnRight, kTurn180),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return direction_name(info.param);
                         });

class TurnYuva420Test : public ::testing::TestWithParam<int> {};

TEST_P(TurnYuva420Test, RotatesAlphaAlongsideYuvPlanes) {
  const int direction = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 6;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x20 + plane * 0x19), plane);
    write_frame_plane<std::uint8_t>(source, plane,
                                    [plane](int x, int y) {
                                      return 7 + plane * 31 + x * 13 + y * 17;
                                    });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Turn filter(clip, direction, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    expect_rotated_typed_plane<std::uint8_t>(source, output, plane, direction);
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(Directions, TurnYuva420Test,
                         ::testing::Values(kTurnLeft, kTurnRight, kTurn180),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return direction_name(info.param);
                         });

class TurnYv16Test : public ::testing::TestWithParam<int> {};

TEST_P(TurnYv16Test, RotatesLumaAndKeepsResampledChromaGeometry) {
  const int direction = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 6;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x31, PLANAR_Y);
  fill_plane_full_pitch(source, 0x42, PLANAR_U);
  fill_plane_full_pitch(source, 0x53, PLANAR_V);
  write_frame_plane<std::uint8_t>(source, PLANAR_Y,
                                  [](int x, int y) { return 3 + x * 19 + y * 31; });
  write_frame_plane<std::uint8_t>(source, PLANAR_U, [](int, int) { return 77; });
  write_frame_plane<std::uint8_t>(source, PLANAR_V, [](int, int) { return 181; });
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Turn filter(clip, direction, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_rotated_typed_plane<std::uint8_t>(source, output, PLANAR_Y, direction);
  EXPECT_EQ(output->GetRowSize(PLANAR_U), height / 2);
  EXPECT_EQ(output->GetHeight(PLANAR_U), width);
  expect_plane_constant<std::uint8_t>(output, PLANAR_U, 77);
  expect_plane_constant<std::uint8_t>(output, PLANAR_V, 181);
  EXPECT_NE(output->CheckMemory(), 1);
  ASSERT_FALSE(source_clip->frame_requests().empty());
  for (const int request : source_clip->frame_requests()) EXPECT_EQ(request, 0);
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(Directions, TurnYv16Test,
                         ::testing::Values(kTurnLeft, kTurnRight),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return direction_name(info.param);
                         });

struct PackedTurnCase {
  int pixel_type;
  int component_bytes;
  int direction;
  const char* name;
};

class PackedTurnFilterTest : public ::testing::TestWithParam<PackedTurnCase> {};

TEST_P(PackedTurnFilterTest, RotatesPackedBgrPixelsWithoutSplittingComponents) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, test_case.pixel_type, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa4, DEFAULT_PLANE);
  if (test_case.component_bytes == 1) {
    write_frame_plane<std::uint8_t>(source, DEFAULT_PLANE, [&](int component, int raw_y) {
      const int x = component / 3;
      const int channel = component % 3;
      const int logical_y = height - 1 - raw_y;
      return static_cast<std::uint8_t>(9 + x * 17 + logical_y * 29 + channel * 43);
    });
  } else {
    write_frame_plane<std::uint16_t>(source, DEFAULT_PLANE, [&](int component, int raw_y) {
      const int x = component / 3;
      const int channel = component % 3;
      const int logical_y = height - 1 - raw_y;
      return static_cast<std::uint16_t>(1000 + x * 1701 + logical_y * 2901 + channel * 4301);
    });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Turn filter(clip, test_case.direction, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  if (test_case.component_bytes == 1) {
    expect_packed_rotation<std::uint8_t>(source, output, test_case.direction, 3);
  } else {
    expect_packed_rotation<std::uint16_t>(source, output, test_case.direction, 3);
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

void PrintTo(const PackedTurnCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(
    Formats, PackedTurnFilterTest,
    ::testing::Values(PackedTurnCase{VideoInfo::CS_BGR24, 1, kTurnLeft, "Bgr24Left"},
                      PackedTurnCase{VideoInfo::CS_BGR24, 1, kTurnRight, "Bgr24Right"},
                      PackedTurnCase{VideoInfo::CS_BGR48, 2, kTurnLeft, "Bgr48Left"},
                      PackedTurnCase{VideoInfo::CS_BGR48, 2, kTurnRight, "Bgr48Right"}),
    [](const ::testing::TestParamInfo<PackedTurnCase>& info) { return info.param.name; });

}  // namespace
