#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_TRANSFORM_FILTER_UNDEF_AVS_UNUSED
#endif
#include "filters/transform.h"
#ifdef AVSUT_TRANSFORM_FILTER_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_TRANSFORM_FILTER_UNDEF_AVS_UNUSED
#endif

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_COMBINE_FILTER_UNDEF_AVS_UNUSED
#endif
#include "filters/combine.h"
#ifdef AVSUT_COMBINE_FILTER_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_COMBINE_FILTER_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_helper.h"

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <ostream>
#include <vector>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSequenceClip;
using avsut::test::FrameSnapshot;
using avsut::test::get_frame_property_int;
using avsut::test::make_video_info;
using avsut::test::read_frame_plane_active;
using avsut::test::set_frame_property_int;
using avsut::test::StaticFrameClip;
using avsut::test::VideoInfoSpec;
using avsut::test::write_frame_plane;

void fill_yv24_pattern(PVideoFrame& frame) {
  constexpr std::array<std::uint8_t, 3> bases{3, 67, 131};
  for (std::size_t plane_index = 0; plane_index < bases.size(); ++plane_index) {
    const int plane = static_cast<int>(PLANAR_Y + plane_index);
    fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0xb0 + plane_index), plane);
    const int pitch = frame->GetPitch(plane);
    const int width = frame->GetRowSize(plane);
    const int height = frame->GetHeight(plane);
    for (int y = 0; y < height; ++y) {
      auto* row = frame->GetWritePtr(plane) + y * pitch;
      for (int x = 0; x < width; ++x) {
        row[x] = static_cast<std::uint8_t>(bases[plane_index] + x * 17 + y * 31);
      }
    }
  }
}

template <typename Pixel>
void expect_flipped_plane(const PVideoFrame& source, const PVideoFrame& output, int plane,
                          bool vertical) {
  const int source_width = source->GetRowSize(plane) / static_cast<int>(sizeof(Pixel));
  const int source_height = source->GetHeight(plane);
  ASSERT_EQ(output->GetRowSize(plane) / static_cast<int>(sizeof(Pixel)), source_width);
  ASSERT_EQ(output->GetHeight(plane), source_height);
  const int source_pitch = source->GetPitch(plane);
  const int output_pitch = output->GetPitch(plane);
  for (int y = 0; y < source_height; ++y) {
    const auto* source_row =
        reinterpret_cast<const Pixel*>(source->GetReadPtr(plane) + y * source_pitch);
    const auto* output_row =
        reinterpret_cast<const Pixel*>(output->GetReadPtr(plane) + y * output_pitch);
    for (int x = 0; x < source_width; ++x) {
      const int source_x = vertical ? x : source_width - 1 - x;
      const int source_y = vertical ? source_height - 1 - y : y;
      const auto* expected_row =
          reinterpret_cast<const Pixel*>(source->GetReadPtr(plane) + source_y * source_pitch);
      EXPECT_EQ(output_row[x], expected_row[source_x])
          << "plane=" << plane << " x=" << x << " y=" << y;
    }
  }
}

template <typename Pixel>
void expect_cropped_plane(const PVideoFrame& source, const PVideoFrame& output, int plane, int left,
                          int top, int xsub, int ysub) {
  const int plane_left = left >> xsub;
  const int plane_top = top >> ysub;
  const int output_width = output->GetRowSize(plane) / static_cast<int>(sizeof(Pixel));
  const int output_height = output->GetHeight(plane);
  const int source_pitch = source->GetPitch(plane);
  const int output_pitch = output->GetPitch(plane);
  for (int y = 0; y < output_height; ++y) {
    const auto* output_row =
        reinterpret_cast<const Pixel*>(output->GetReadPtr(plane) + y * output_pitch);
    const auto* source_row =
        reinterpret_cast<const Pixel*>(source->GetReadPtr(plane) + (plane_top + y) * source_pitch);
    for (int x = 0; x < output_width; ++x) {
      EXPECT_EQ(output_row[x], source_row[plane_left + x])
          << "plane=" << plane << " x=" << x << " y=" << y;
    }
  }
}

template <typename Pixel>
Pixel read_packed_logical(const PVideoFrame& frame, int x, int y, int component, int components) {
  const int raw_y = frame->GetHeight() - 1 - y;
  const auto* row = reinterpret_cast<const Pixel*>(frame->GetReadPtr() + raw_y * frame->GetPitch());
  return row[x * components + component];
}

template <typename Pixel>
void expect_cropped_packed(const PVideoFrame& source, const PVideoFrame& output, int left, int top,
                           int width, int height, int components) {
  const int source_width = source->GetRowSize() / (components * static_cast<int>(sizeof(Pixel)));
  const int source_height = source->GetHeight();
  const int output_width = output->GetRowSize() / (components * static_cast<int>(sizeof(Pixel)));
  const int output_height = output->GetHeight();
  ASSERT_EQ(output_width, width);
  ASSERT_EQ(output_height, height);
  ASSERT_LE(left + width, source_width);
  ASSERT_LE(top + height, source_height);
  for (int y = 0; y < output_height; ++y) {
    for (int x = 0; x < output_width; ++x) {
      for (int component = 0; component < components; ++component) {
        EXPECT_EQ(read_packed_logical<Pixel>(output, x, y, component, components),
                  read_packed_logical<Pixel>(source, x + left, y + top, component, components))
            << "component=" << component << " x=" << x << " y=" << y;
      }
    }
  }
}

template <typename Pixel>
void expect_flipped_packed(const PVideoFrame& source, const PVideoFrame& output, bool vertical,
                           int components) {
  const int source_width = source->GetRowSize() / (components * static_cast<int>(sizeof(Pixel)));
  const int source_height = source->GetHeight();
  ASSERT_EQ(output->GetRowSize() / (components * static_cast<int>(sizeof(Pixel))), source_width);
  ASSERT_EQ(output->GetHeight(), source_height);
  for (int y = 0; y < source_height; ++y) {
    for (int x = 0; x < source_width; ++x) {
      const int source_x = vertical ? x : source_width - 1 - x;
      const int source_y = vertical ? source_height - 1 - y : y;
      for (int component = 0; component < components; ++component) {
        EXPECT_EQ(read_packed_logical<Pixel>(output, x, y, component, components),
                  read_packed_logical<Pixel>(source, source_x, source_y, component, components))
            << "component=" << component << " x=" << x << " y=" << y;
      }
    }
  }
}

template <typename Pixel>
void fill_packed_pattern(PVideoFrame& frame, int components, Pixel base) {
  const int height = frame->GetHeight();
  write_frame_plane<Pixel>(frame, DEFAULT_PLANE, [&](int component, int raw_y) {
    const int x = component / components;
    const int channel = component % components;
    const int logical_y = height - 1 - raw_y;
    return static_cast<Pixel>(static_cast<std::uint32_t>(base) + x * 1701U + logical_y * 2901U +
                              channel * 4301U);
  });
}

template <typename Pixel>
void expect_bordered_plane(const PVideoFrame& source, const PVideoFrame& output, int plane,
                           int left, int top, int right, int bottom, int xsub, int ysub,
                           Pixel border) {
  const int source_width = source->GetRowSize(plane) / static_cast<int>(sizeof(Pixel));
  const int source_height = source->GetHeight(plane);
  const int plane_left = left >> xsub;
  const int plane_top = top >> ysub;
  const int plane_right = right >> xsub;
  const int plane_bottom = bottom >> ysub;
  const int output_width = output->GetRowSize(plane) / static_cast<int>(sizeof(Pixel));
  const int output_height = output->GetHeight(plane);
  ASSERT_EQ(output_width, source_width + plane_left + plane_right);
  ASSERT_EQ(output_height, source_height + plane_top + plane_bottom);
  const int source_pitch = source->GetPitch(plane);
  const int output_pitch = output->GetPitch(plane);
  for (int y = 0; y < output_height; ++y) {
    const auto* output_row =
        reinterpret_cast<const Pixel*>(output->GetReadPtr(plane) + y * output_pitch);
    for (int x = 0; x < output_width; ++x) {
      const bool inside = y >= plane_top && y < plane_top + source_height && x >= plane_left &&
                          x < plane_left + source_width;
      Pixel expected = border;
      if (inside) {
        const auto* source_row = reinterpret_cast<const Pixel*>(source->GetReadPtr(plane) +
                                                                (y - plane_top) * source_pitch);
        expected = source_row[x - plane_left];
      }
      EXPECT_EQ(output_row[x], expected) << "plane=" << plane << " x=" << x << " y=" << y;
    }
  }
}

template <typename Pixel>
void expect_bordered_packed(const PVideoFrame& source, const PVideoFrame& output, int left, int top,
                            int right, int bottom, int components,
                            const std::array<Pixel, 4>& border) {
  const int source_width = source->GetRowSize() / (components * static_cast<int>(sizeof(Pixel)));
  const int source_height = source->GetHeight();
  const int output_width = output->GetRowSize() / (components * static_cast<int>(sizeof(Pixel)));
  const int output_height = output->GetHeight();
  ASSERT_EQ(output_width, source_width + left + right);
  ASSERT_EQ(output_height, source_height + top + bottom);
  for (int y = 0; y < output_height; ++y) {
    for (int x = 0; x < output_width; ++x) {
      const bool inside =
          y >= top && y < top + source_height && x >= left && x < left + source_width;
      for (int component = 0; component < components; ++component) {
        const Pixel expected =
            inside ? read_packed_logical<Pixel>(source, x - left, y - top, component, components)
                   : border[static_cast<std::size_t>(component)];
        EXPECT_EQ(read_packed_logical<Pixel>(output, x, y, component, components), expected)
            << "component=" << component << " x=" << x << " y=" << y;
      }
    }
  }
}

TEST(FlipFilter, FlipsYv24VerticallyAcrossAllPlanes) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_yv24_pattern(source);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  FlipVertical filter(clip);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int pitch = source->GetPitch(plane);
    const int output_pitch = output->GetPitch(plane);
    for (int y = 0; y < height; ++y) {
      const auto* source_row = source->GetReadPtr(plane) + (height - 1 - y) * pitch;
      const auto* output_row = output->GetReadPtr(plane) + y * output_pitch;
      for (int x = 0; x < width; ++x) {
        EXPECT_EQ(output_row[x], source_row[x]) << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(FlipFilter, FlipsYv24HorizontallyAcrossAllPlanes) {
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_yv24_pattern(source);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  FlipHorizontal filter(clip);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int pitch = source->GetPitch(plane);
    const int output_pitch = output->GetPitch(plane);
    for (int y = 0; y < height; ++y) {
      const auto* source_row = source->GetReadPtr(plane) + y * pitch;
      const auto* output_row = output->GetReadPtr(plane) + y * output_pitch;
      for (int x = 0; x < width; ++x) {
        EXPECT_EQ(output_row[x], source_row[width - 1 - x])
            << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(CropFilter, ReturnsRequestedYv24Subrectangle) {
  AviSynthEnvironment environment;
  constexpr int source_width = 8;
  constexpr int source_height = 6;
  constexpr int left = 2;
  constexpr int top = 2;
  constexpr int width = 4;
  constexpr int height = 2;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_yv24_pattern(source);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Crop filter(left, top, width, height, true, clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetRowSize(PLANAR_Y), width);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), height);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int source_pitch = source->GetPitch(plane);
    const int output_pitch = output->GetPitch(plane);
    for (int y = 0; y < height; ++y) {
      const auto* source_row = source->GetReadPtr(plane) + (top + y) * source_pitch + left;
      const auto* output_row = output->GetReadPtr(plane) + y * output_pitch;
      for (int x = 0; x < width; ++x) {
        EXPECT_EQ(output_row[x], source_row[x]) << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(CropFilter, PreservesChromaRangeAndFieldPropertiesOnSubframe) {
  AviSynthEnvironment environment;
  constexpr int source_width = 8;
  constexpr int source_height = 6;
  constexpr int left = 2;
  constexpr int top = 2;
  constexpr int width = 4;
  constexpr int height = 4;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x40 + plane * 17), plane);
  }
  set_frame_property_int(environment.get(), source, "_ChromaLocation", AVS_CHROMA_LEFT);
  set_frame_property_int(environment.get(), source, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source, "_FieldBased", 1);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Crop filter(left, top, width, height, true, clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const auto& property : std::array<std::pair<const char*, int>, 3>{
           std::pair{"_ChromaLocation", AVS_CHROMA_LEFT},
           std::pair{"_ColorRange", AVS_COLORRANGE_FULL}, std::pair{"_FieldBased", 1}}) {
    const auto actual = get_frame_property_int(environment.get(), output, property.first);
    ASSERT_TRUE(actual.has_value()) << property.first;
    EXPECT_EQ(*actual, property.second) << property.first;
    const auto source_value = get_frame_property_int(environment.get(), source, property.first);
    ASSERT_TRUE(source_value.has_value()) << property.first;
    EXPECT_EQ(*source_value, property.second) << property.first;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(AddBordersFilter, AddsExplicitYuvColorAroundYv24Source) {
  AviSynthEnvironment environment;
  constexpr int source_width = 5;
  constexpr int source_height = 3;
  constexpr int left = 2;
  constexpr int top = 2;
  constexpr int right = 2;
  constexpr int bottom = 2;
  constexpr std::uint8_t border_y = 18;
  constexpr std::uint8_t border_u = 52;
  constexpr std::uint8_t border_v = 86;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_yv24_pattern(source);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  const int yuv_color = (static_cast<int>(border_y) << 16) | (static_cast<int>(border_u) << 8) |
                        static_cast<int>(border_v);
  AddBorders filter(left, top, right, bottom, yuv_color, true, clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetRowSize(PLANAR_Y), source_width + left + right);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), source_height + top + bottom);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const std::uint8_t border = plane == PLANAR_Y   ? border_y
                                : plane == PLANAR_U ? border_u
                                                    : border_v;
    const int output_width = output->GetRowSize(plane);
    const int output_height = output->GetHeight(plane);
    const int source_pitch = source->GetPitch(plane);
    const int output_pitch = output->GetPitch(plane);
    for (int y = 0; y < output_height; ++y) {
      const auto* output_row = output->GetReadPtr(plane) + y * output_pitch;
      for (int x = 0; x < output_width; ++x) {
        const bool inside =
            y >= top && y < top + source_height && x >= left && x < left + source_width;
        const auto expected =
            inside ? source->GetReadPtr(plane)[(y - top) * source_pitch + x - left] : border;
        EXPECT_EQ(output_row[x], expected) << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

class FlipYuv420Test : public ::testing::TestWithParam<bool> {};

TEST_P(FlipYuv420Test, FlipsSubsampledYv12AcrossEveryPlane) {
  const bool vertical = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x31 + plane * 0x11), plane);
    write_frame_plane<std::uint8_t>(
        source, plane, [plane](int x, int y) { return 5 + plane * 37 + x * 17 + y * 29; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  PVideoFrame output;
  if (vertical) {
    FlipVertical filter(clip);
    EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
    output = filter.GetFrame(0, environment.get());
  } else {
    FlipHorizontal filter(clip);
    EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
    output = filter.GetFrame(0, environment.get());
  }

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    expect_flipped_plane<std::uint8_t>(source, output, plane, vertical);
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(Directions, FlipYuv420Test, ::testing::Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& info) {
                           return info.param ? "Vertical" : "Horizontal";
                         });

TEST(FlipFilter, FlipsYuva420AlphaWithTheFullResolutionPlanes) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x41 + plane * 0x13), plane);
    write_frame_plane<std::uint8_t>(
        source, plane, [plane](int x, int y) { return 7 + plane * 41 + x * 11 + y * 23; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  FlipVertical filter(clip);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    expect_flipped_plane<std::uint8_t>(source, output, plane, true);
  }
  EXPECT_EQ(output->GetHeight(PLANAR_A), height);
  EXPECT_EQ(output->GetHeight(PLANAR_U), height / 2);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(FlipFilter, DoubleHorizontalFlipRestoresYuva420ActivePlanes) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x52 + plane * 0x17), plane);
    write_frame_plane<std::uint8_t>(
        source, plane, [plane](int x, int y) { return 11 + plane * 37 + x * 13 + y * 29; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip_impl = new StaticFrameClip(vi, source);
  const PClip source_clip(source_clip_impl);
  const PClip flipped(new FlipHorizontal(source_clip));
  const PClip restored(new FlipHorizontal(flipped));

  EXPECT_EQ(restored->SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = restored->GetFrame(0, environment.get());

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    EXPECT_EQ(read_frame_plane_active<std::uint8_t>(output, plane),
              read_frame_plane_active<std::uint8_t>(source, plane))
        << "plane=" << plane;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

struct PackedFlipCase {
  int pixel_type;
  int component_bytes;
  bool vertical;
  const char* name;
};

class PackedFlipFilterTest : public ::testing::TestWithParam<PackedFlipCase> {};

TEST_P(PackedFlipFilterTest, FlipsPackedBgrRowsAndPixels) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  constexpr int width = 5;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, test_case.pixel_type, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x81, DEFAULT_PLANE);
  if (test_case.component_bytes == 1) {
    fill_packed_pattern<std::uint8_t>(source, 3, 7);
  } else {
    fill_packed_pattern<std::uint16_t>(source, 4, 1000);
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  PVideoFrame output;
  if (test_case.vertical) {
    FlipVertical filter(clip);
    output = filter.GetFrame(0, environment.get());
  } else {
    FlipHorizontal filter(clip);
    output = filter.GetFrame(0, environment.get());
  }
  if (test_case.component_bytes == 1) {
    expect_flipped_packed<std::uint8_t>(source, output, test_case.vertical, 3);
  } else {
    expect_flipped_packed<std::uint16_t>(source, output, test_case.vertical, 4);
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

void PrintTo(const PackedFlipCase& test_case, std::ostream* stream) { *stream << test_case.name; }

INSTANTIATE_TEST_SUITE_P(
    Formats, PackedFlipFilterTest,
    ::testing::Values(PackedFlipCase{VideoInfo::CS_BGR24, 1, true, "Bgr24Vertical"},
                      PackedFlipCase{VideoInfo::CS_BGR24, 1, false, "Bgr24Horizontal"},
                      PackedFlipCase{VideoInfo::CS_BGR64, 2, true, "Bgr64Vertical"},
                      PackedFlipCase{VideoInfo::CS_BGR64, 2, false, "Bgr64Horizontal"}),
    [](const ::testing::TestParamInfo<PackedFlipCase>& info) { return info.param.name; });

TEST(FlipFilter, FlipsPlanarRgb16GbrPlanesHorizontally) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 4;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_RGBP16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x92 + plane * 0x11), plane);
    write_frame_plane<std::uint16_t>(
        source, plane, [plane](int x, int y) { return 1000 + plane * 7001 + x * 401 + y * 503; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  FlipHorizontal filter(clip);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    expect_flipped_plane<std::uint16_t>(source, output, plane, false);
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(CropFilter, ReturnsRequestedYv12SubrectangleWithScaledChromaOffsets) {
  AviSynthEnvironment environment;
  constexpr int source_width = 8;
  constexpr int source_height = 6;
  constexpr int left = 2;
  constexpr int top = 2;
  constexpr int width = 4;
  constexpr int height = 4;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x22 + plane * 0x17), plane);
    write_frame_plane<std::uint8_t>(
        source, plane, [plane](int x, int y) { return 3 + plane * 43 + x * 17 + y * 29; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Crop filter(left, top, width, height, true, clip, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_cropped_plane<std::uint8_t>(source, output, PLANAR_Y, left, top, 0, 0);
  expect_cropped_plane<std::uint8_t>(source, output, PLANAR_U, left, top, 1, 1);
  expect_cropped_plane<std::uint8_t>(source, output, PLANAR_V, left, top, 1, 1);
  EXPECT_EQ(output->GetRowSize(PLANAR_U), width / 2);
  EXPECT_EQ(output->GetHeight(PLANAR_U), height / 2);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(CropFilter, ReturnsRequestedYv16SubrectangleWithHorizontalChromaOffset) {
  AviSynthEnvironment environment;
  constexpr int source_width = 10;
  constexpr int source_height = 5;
  constexpr int left = 2;
  constexpr int top = 1;
  constexpr int width = 4;
  constexpr int height = 3;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x32 + plane * 0x13), plane);
    write_frame_plane<std::uint8_t>(
        source, plane, [plane](int x, int y) { return 11 + plane * 31 + x * 19 + y * 23; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Crop filter(left, top, width, height, true, clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_cropped_plane<std::uint8_t>(source, output, PLANAR_Y, left, top, 0, 0);
  expect_cropped_plane<std::uint8_t>(source, output, PLANAR_U, left, top, 1, 0);
  expect_cropped_plane<std::uint8_t>(source, output, PLANAR_V, left, top, 1, 0);
  EXPECT_EQ(output->GetRowSize(PLANAR_U), width / 2);
  EXPECT_EQ(output->GetHeight(PLANAR_U), height);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(CropFilter, ReturnsRequestedYuva420SubrectangleAndAlpha) {
  AviSynthEnvironment environment;
  constexpr int source_width = 8;
  constexpr int source_height = 6;
  constexpr int left = 2;
  constexpr int top = 2;
  constexpr int width = 4;
  constexpr int height = 4;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x42 + plane * 0x11), plane);
    write_frame_plane<std::uint8_t>(
        source, plane, [plane](int x, int y) { return 7 + plane * 41 + x * 13 + y * 17; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Crop filter(left, top, width, height, true, clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_cropped_plane<std::uint8_t>(source, output, PLANAR_Y, left, top, 0, 0);
  expect_cropped_plane<std::uint8_t>(source, output, PLANAR_U, left, top, 1, 1);
  expect_cropped_plane<std::uint8_t>(source, output, PLANAR_V, left, top, 1, 1);
  expect_cropped_plane<std::uint8_t>(source, output, PLANAR_A, left, top, 0, 0);
  EXPECT_EQ(output->GetHeight(PLANAR_A), height);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(CropFilter, ReturnsRequestedPackedBgr24LogicalSubrectangle) {
  AviSynthEnvironment environment;
  constexpr int source_width = 7;
  constexpr int source_height = 5;
  constexpr int left = 1;
  constexpr int top = 1;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_BGR24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0x73, DEFAULT_PLANE);
  fill_packed_pattern<std::uint8_t>(source, 3, 9);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Crop filter(left, top, width, height, true, clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_cropped_packed<std::uint8_t>(source, output, left, top, width, height, 3);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(CropFilter, ReturnsRequestedPlanarRgb16Subrectangle) {
  AviSynthEnvironment environment;
  constexpr int source_width = 7;
  constexpr int source_height = 5;
  constexpr int left = 1;
  constexpr int top = 1;
  constexpr int width = 5;
  constexpr int height = 3;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_RGBP16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x82 + plane * 0x13), plane);
    write_frame_plane<std::uint16_t>(
        source, plane, [plane](int x, int y) { return 1000 + plane * 5001 + x * 701 + y * 307; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  Crop filter(left, top, width, height, true, clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    expect_cropped_plane<std::uint16_t>(source, output, plane, left, top, 0, 0);
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(AddBordersFilter, AddsYuvBordersToYv12WithSubsampledGeometry) {
  AviSynthEnvironment environment;
  constexpr int source_width = 8;
  constexpr int source_height = 4;
  constexpr int left = 2;
  constexpr int top = 2;
  constexpr int right = 2;
  constexpr int bottom = 2;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x91 + plane * 0x11), plane);
    write_frame_plane<std::uint8_t>(
        source, plane, [plane](int x, int y) { return 7 + plane * 31 + x * 13 + y * 19; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  AddBorders filter(left, top, right, bottom, 0x123456, true, clip, environment.get());
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_bordered_plane<std::uint8_t>(source, output, PLANAR_Y, left, top, right, bottom, 0, 0,
                                      0x12);
  expect_bordered_plane<std::uint8_t>(source, output, PLANAR_U, left, top, right, bottom, 1, 1,
                                      0x34);
  expect_bordered_plane<std::uint8_t>(source, output, PLANAR_V, left, top, right, bottom, 1, 1,
                                      0x56);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(AddBordersFilter, AddsYuva420BordersIncludingAlpha) {
  AviSynthEnvironment environment;
  constexpr int source_width = 6;
  constexpr int source_height = 4;
  constexpr int left = 2;
  constexpr int top = 2;
  constexpr int right = 2;
  constexpr int bottom = 2;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x51 + plane * 0x13), plane);
    write_frame_plane<std::uint8_t>(
        source, plane, [plane](int x, int y) { return 11 + plane * 37 + x * 17 + y * 23; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  AddBorders filter(left, top, right, bottom, static_cast<int>(0xA2345678U), true, clip,
                    environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_bordered_plane<std::uint8_t>(source, output, PLANAR_Y, left, top, right, bottom, 0, 0,
                                      0x34);
  expect_bordered_plane<std::uint8_t>(source, output, PLANAR_U, left, top, right, bottom, 1, 1,
                                      0x56);
  expect_bordered_plane<std::uint8_t>(source, output, PLANAR_V, left, top, right, bottom, 1, 1,
                                      0x78);
  expect_bordered_plane<std::uint8_t>(source, output, PLANAR_A, left, top, right, bottom, 0, 0,
                                      0xA2);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(AddBordersFilter, AddsPackedBgr24LogicalBorders) {
  AviSynthEnvironment environment;
  constexpr int source_width = 5;
  constexpr int source_height = 3;
  constexpr int left = 1;
  constexpr int top = 1;
  constexpr int right = 2;
  constexpr int bottom = 2;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_BGR24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xa1, DEFAULT_PLANE);
  fill_packed_pattern<std::uint8_t>(source, 3, 13);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  AddBorders filter(left, top, right, bottom, 0x00112233, false, clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  expect_bordered_packed<std::uint8_t>(source, output, left, top, right, bottom, 3,
                                       std::array<std::uint8_t, 4>{0x33, 0x22, 0x11, 0});
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(AddBordersFilter, AddsPackedBgr64SixteenBitBorders) {
  AviSynthEnvironment environment;
  constexpr int source_width = 3;
  constexpr int source_height = 3;
  constexpr int left = 1;
  constexpr int top = 1;
  constexpr int right = 1;
  constexpr int bottom = 1;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_BGR64, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(source, 0xb2, DEFAULT_PLANE);
  fill_packed_pattern<std::uint16_t>(source, 4, 1000);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  AddBorders filter(left, top, right, bottom, 0x44332211, false, clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const auto scale = [](std::uint8_t value) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(value) * 65535U) / 255U);
  };
  expect_bordered_packed<std::uint16_t>(
      source, output, left, top, right, bottom, 4,
      std::array<std::uint16_t, 4>{scale(0x11), scale(0x22), scale(0x33), scale(0x44)});
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(AddBordersFilter, AddsPlanarRgb16BordersInGbrPlaneOrder) {
  AviSynthEnvironment environment;
  constexpr int source_width = 5;
  constexpr int source_height = 3;
  constexpr int left = 1;
  constexpr int top = 1;
  constexpr int right = 1;
  constexpr int bottom = 1;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_RGBP16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0xc3 + plane * 0x11), plane);
    write_frame_plane<std::uint16_t>(
        source, plane, [plane](int x, int y) { return 2000 + plane * 5001 + x * 701 + y * 307; });
  }
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  AddBorders filter(left, top, right, bottom, 0x00112233, false, clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  const auto scale = [](std::uint8_t value) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(value) * 65535U) / 255U);
  };
  expect_bordered_plane<std::uint16_t>(source, output, PLANAR_G, left, top, right, bottom, 0, 0,
                                       scale(0x22));
  expect_bordered_plane<std::uint16_t>(source, output, PLANAR_B, left, top, right, bottom, 0, 0,
                                       scale(0x33));
  expect_bordered_plane<std::uint16_t>(source, output, PLANAR_R, left, top, right, bottom, 0, 0,
                                       scale(0x11));
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(StackFilter, StacksYv24VerticallyFromTwoSources) {
  AviSynthEnvironment environment;
  constexpr int width = 4;
  constexpr int top_height = 3;
  constexpr int bottom_height = 2;
  const auto top_vi =
      make_video_info(VideoInfoSpec{width, top_height, VideoInfo::CS_YV24, 1, 25, 1});
  const auto bottom_vi =
      make_video_info(VideoInfoSpec{width, bottom_height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame top = environment.get()->NewVideoFrame(top_vi);
  PVideoFrame bottom = environment.get()->NewVideoFrame(bottom_vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(top, 0xc0, plane);
    fill_plane_full_pitch(bottom, 0xd0, plane);
    write_frame_plane<std::uint8_t>(top, plane, [plane](int x, int y) {
      return static_cast<std::uint8_t>(10 + plane * 20 + y * 7 + x);
    });
    write_frame_plane<std::uint8_t>(bottom, plane, [plane](int x, int y) {
      return static_cast<std::uint8_t>(90 + plane * 11 + y * 5 + x * 2);
    });
  }
  const auto top_before = FrameSnapshot::capture(top, top_vi);
  const auto bottom_before = FrameSnapshot::capture(bottom, bottom_vi);
  auto* top_impl = new StaticFrameClip(top_vi, top);
  auto* bottom_impl = new StaticFrameClip(bottom_vi, bottom);
  const PClip top_clip(top_impl);
  const PClip bottom_clip(bottom_impl);

  StackVertical filter(std::vector<PClip>{top_clip, bottom_clip}, environment.get());
  EXPECT_EQ(filter.GetVideoInfo().width, width);
  EXPECT_EQ(filter.GetVideoInfo().height, top_height + bottom_height);
  EXPECT_EQ(filter.GetVideoInfo().num_frames, 1);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int pitch = output->GetPitch(plane);
    const auto* out = output->GetReadPtr(plane);
    const int top_pitch = top->GetPitch(plane);
    const int bottom_pitch = bottom->GetPitch(plane);
    const auto* top_ptr = top->GetReadPtr(plane);
    const auto* bottom_ptr = bottom->GetReadPtr(plane);
    const int plane_width = top->GetRowSize(plane);
    for (int y = 0; y < top_height; ++y) {
      for (int x = 0; x < plane_width; ++x) {
        EXPECT_EQ(out[x + y * pitch], top_ptr[x + y * top_pitch])
            << "plane=" << plane << " top x=" << x << " y=" << y;
      }
    }
    for (int y = 0; y < bottom_height; ++y) {
      for (int x = 0; x < plane_width; ++x) {
        EXPECT_EQ(out[x + (y + top_height) * pitch], bottom_ptr[x + y * bottom_pitch])
            << "plane=" << plane << " bottom x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(top_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(bottom_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(top, top_vi), top_before);
  EXPECT_EQ(FrameSnapshot::capture(bottom, bottom_vi), bottom_before);
}

TEST(StackFilter, StacksYv24HorizontallyFromTwoSources) {
  AviSynthEnvironment environment;
  constexpr int left_width = 3;
  constexpr int right_width = 2;
  constexpr int height = 4;
  const auto left_vi =
      make_video_info(VideoInfoSpec{left_width, height, VideoInfo::CS_YV24, 1, 25, 1});
  const auto right_vi =
      make_video_info(VideoInfoSpec{right_width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame left = environment.get()->NewVideoFrame(left_vi);
  PVideoFrame right = environment.get()->NewVideoFrame(right_vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(left, 0xa1, plane);
    fill_plane_full_pitch(right, 0xb2, plane);
    write_frame_plane<std::uint8_t>(left, plane, [plane](int x, int y) {
      return static_cast<std::uint8_t>(5 + plane * 13 + y * 9 + x * 4);
    });
    write_frame_plane<std::uint8_t>(right, plane, [plane](int x, int y) {
      return static_cast<std::uint8_t>(55 + plane * 7 + y * 3 + x * 6);
    });
  }
  const auto left_before = FrameSnapshot::capture(left, left_vi);
  const auto right_before = FrameSnapshot::capture(right, right_vi);
  auto* left_impl = new StaticFrameClip(left_vi, left);
  auto* right_impl = new StaticFrameClip(right_vi, right);
  const PClip left_clip(left_impl);
  const PClip right_clip(right_impl);

  StackHorizontal filter(std::vector<PClip>{left_clip, right_clip}, environment.get());
  EXPECT_EQ(filter.GetVideoInfo().width, left_width + right_width);
  EXPECT_EQ(filter.GetVideoInfo().height, height);
  EXPECT_EQ(filter.GetVideoInfo().num_frames, 1);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int pitch = output->GetPitch(plane);
    const auto* out = output->GetReadPtr(plane);
    const int left_pitch = left->GetPitch(plane);
    const int right_pitch = right->GetPitch(plane);
    const auto* left_ptr = left->GetReadPtr(plane);
    const auto* right_ptr = right->GetReadPtr(plane);
    const int left_plane_width = left->GetRowSize(plane);
    const int right_plane_width = right->GetRowSize(plane);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < left_plane_width; ++x) {
        EXPECT_EQ(out[x + y * pitch], left_ptr[x + y * left_pitch])
            << "plane=" << plane << " left x=" << x << " y=" << y;
      }
      for (int x = 0; x < right_plane_width; ++x) {
        EXPECT_EQ(out[x + left_plane_width + y * pitch], right_ptr[x + y * right_pitch])
            << "plane=" << plane << " right x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(left_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(right_impl->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(left, left_vi), left_before);
  EXPECT_EQ(FrameSnapshot::capture(right, right_vi), right_before);
}

}  // namespace
