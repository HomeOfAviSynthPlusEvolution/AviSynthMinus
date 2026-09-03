#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_RESIZE_FILTER_UNDEF_AVS_UNUSED
#endif
#include "filters/resize.h"
#include "filters/resample.h"
#ifdef AVSUT_RESIZE_FILTER_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_RESIZE_FILTER_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_helper.h"

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <ostream>
#include <utility>
#include <vector>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSnapshot;
using avsut::test::get_frame_property_int;
using avsut::test::make_video_info;
using avsut::test::set_frame_property_int;
using avsut::test::StaticFrameClip;
using avsut::test::VideoInfoSpec;
using avsut::test::video_frame_planes;
using avsut::test::write_frame_plane;

struct TriangleTap {
  int source_index;
  int coefficient;
};

enum class ExtraResizeKernel { MitchellNetravali, Lanczos3, Spline36, Sinc4 };

double mitchell_netravali_value(double value) {
  constexpr double b = 1.0 / 3.0;
  constexpr double c = 1.0 / 3.0;
  const double p0 = (6.0 - 2.0 * b) / 6.0;
  const double p2 = (-18.0 + 12.0 * b + 6.0 * c) / 6.0;
  const double p3 = (12.0 - 9.0 * b - 6.0 * c) / 6.0;
  const double q0 = (8.0 * b + 24.0 * c) / 6.0;
  const double q1 = (-12.0 * b - 48.0 * c) / 6.0;
  const double q2 = (6.0 * b + 30.0 * c) / 6.0;
  const double q3 = (-b - 6.0 * c) / 6.0;
  value = std::abs(value);
  return value < 1.0 ? p0 + value * value * (p2 + value * p3)
                     : value < 2.0 ? q0 + value * (q1 + value * (q2 + value * q3)) : 0.0;
}

double lanczos3_value(double value) {
  constexpr double pi = 3.14159265358979323846;
  constexpr double taps = 3.0;
  const auto sinc = [pi](double input) {
    if (input > 0.000001) {
      input *= pi;
      return std::sin(input) / input;
    }
    return 1.0;
  };
  value = std::abs(value);
  return value < taps ? sinc(value) * sinc(value / taps) : 0.0;
}

double spline36_value(double value) {
  value = std::abs(value);
  if (value < 1.0) {
    return ((13.0 / 11.0 * value - 453.0 / 209.0) * value - 3.0 / 209.0) * value + 1.0;
  }
  if (value < 2.0) {
    const double offset = value - 1.0;
    return ((-6.0 / 11.0 * offset + 270.0 / 209.0) * offset - 156.0 / 209.0) * offset;
  }
  if (value < 3.0) {
    const double offset = value - 2.0;
    return ((1.0 / 11.0 * offset - 45.0 / 209.0) * offset + 26.0 / 209.0) * offset;
  }
  return 0.0;
}

double sinc4_value(double value) {
  constexpr double pi = 3.14159265358979323846;
  value = std::abs(value);
  if (value > 0.000001) {
    value *= pi;
    return std::sin(value) / value;
  }
  return 1.0;
}

double extra_resize_kernel_value(ExtraResizeKernel kernel, double value) {
  switch (kernel) {
    case ExtraResizeKernel::MitchellNetravali:
      return mitchell_netravali_value(value);
    case ExtraResizeKernel::Lanczos3:
      return lanczos3_value(value);
    case ExtraResizeKernel::Spline36:
      return spline36_value(value);
    case ExtraResizeKernel::Sinc4:
      return sinc4_value(value);
  }
  return 0.0;
}

double extra_resize_kernel_support(ExtraResizeKernel kernel) {
  if (kernel == ExtraResizeKernel::MitchellNetravali) {
    return 2.0;
  }
  return kernel == ExtraResizeKernel::Sinc4 ? 4.0 : 3.0;
}

std::vector<TriangleTap> extra_resize_taps(ExtraResizeKernel kernel, int source_size,
                                           double crop_start, double crop_size, int target_size,
                                           int target_index, int fixed_point_scale = FPScale) {
  const double source_step = crop_size / static_cast<double>(target_size);
  const double filter_unit = std::max(source_step, 1.0);
  const double impulse_step = 1.0 / filter_unit;
  const double filter_support = extra_resize_kernel_support(kernel) * filter_unit;
  const int fir_filter_size = std::max(static_cast<int>(std::ceil(filter_support * 2.0)), 1);
  const int last_source = source_size - 1;
  const double position = crop_start + source_step * 0.5 - 0.5 +
                          source_step * static_cast<double>(target_index);
  const int start_position = static_cast<int>(position + filter_support) - fir_filter_size + 1;

  std::vector<double> raw_coefficients;
  raw_coefficients.reserve(static_cast<std::size_t>(fir_filter_size));
  double total = 0.0;
  for (int k = 0; k < fir_filter_size; ++k) {
    const double coefficient = extra_resize_kernel_value(
        kernel, (position - (start_position + k)) * impulse_step);
    raw_coefficients.push_back(coefficient);
    total += coefficient;
  }
  if (total == 0.0) {
    total = 1.0;
  }

  std::vector<TriangleTap> taps;
  double accumulated = 0.0;
  double previous = 0.0;
  for (int k = 0; k < fir_filter_size; ++k) {
    const int source_index = start_position + k;
    accumulated += raw_coefficients[static_cast<std::size_t>(k)];
    if (source_index >= 0 && source_index <= last_source) {
      const double current = previous + accumulated / total;
      const int current_fixed = static_cast<int>(current * fixed_point_scale + 0.5);
      const int previous_fixed = static_cast<int>(previous * fixed_point_scale + 0.5);
      taps.push_back(TriangleTap{source_index, current_fixed - previous_fixed});
      previous = current;
      accumulated = 0.0;
    }
  }

  if (accumulated != 0.0) {
    const double current = previous + accumulated / total;
    const int current_fixed = static_cast<int>(current * fixed_point_scale + 0.5);
    const int previous_fixed = static_cast<int>(previous * fixed_point_scale + 0.5);
    if (!taps.empty()) {
      taps.back().coefficient += current_fixed - previous_fixed;
    } else {
      taps.push_back(TriangleTap{std::clamp(start_position, 0, last_source), current_fixed});
    }
  }

  if (taps.empty()) {
    taps.push_back(TriangleTap{std::clamp(start_position, 0, last_source), FPScale});
  }
  return taps;
}

std::vector<TriangleTap> triangle_taps(int source_size, double crop_start, double crop_size,
                                       int target_size, int target_index, double center,
                                       int bits_per_pixel) {
  const double source_step = crop_size / static_cast<double>(target_size);
  const double filter_unit = std::max(source_step, 1.0);
  const double impulse_step = 1.0 / filter_unit;
  const double filter_support = filter_unit;
  const int fir_filter_size = std::max(static_cast<int>(std::ceil(filter_support * 2.0)), 1);
  const int last_source = source_size - 1;
  const int fixed_point_scale =
      bits_per_pixel > 8 && bits_per_pixel <= 16 ? FPScale16 : FPScale;

  // Mirror the public triangle-kernel setup, including edge folding and its fixed-point
  // differential rounding, without using the upstream coefficient table as the oracle.
  const double position = crop_start + source_step * center - center +
                          source_step * static_cast<double>(target_index);
  const int start_position = static_cast<int>(position + filter_support) - fir_filter_size + 1;
  std::vector<double> raw_coefficients;
  double total = 0.0;
  for (int k = 0; k < fir_filter_size; ++k) {
    const int source_index = start_position + k;
    const double distance = std::abs((position - source_index) * impulse_step);
    const double coefficient = distance < 1.0 ? 1.0 - distance : 0.0;
    raw_coefficients.push_back(coefficient);
    total += coefficient;
  }
  if (total == 0.0) {
    total = 1.0;
  }

  std::vector<TriangleTap> taps;
  double accumulated = 0.0;
  double previous = 0.0;
  for (int k = 0; k < fir_filter_size; ++k) {
    const int source_index = start_position + k;
    accumulated += raw_coefficients[static_cast<std::size_t>(k)];
    if (source_index >= 0 && source_index <= last_source) {
      const double current = previous + accumulated / total;
      const int current_fixed = static_cast<int>(current * fixed_point_scale + 0.5);
      const int previous_fixed = static_cast<int>(previous * fixed_point_scale + 0.5);
      taps.push_back(TriangleTap{source_index, current_fixed - previous_fixed});
      previous = current;
      accumulated = 0.0;
    }
  }

  if (accumulated != 0.0) {
    const double current = previous + accumulated / total;
    const int current_fixed = static_cast<int>(current * fixed_point_scale + 0.5);
    const int previous_fixed = static_cast<int>(previous * fixed_point_scale + 0.5);
    if (!taps.empty()) {
      taps.back().coefficient += current_fixed - previous_fixed;
    } else {
      taps.push_back(TriangleTap{std::clamp(start_position, 0, last_source), current_fixed});
    }
  }

  if (taps.empty()) {
    taps.push_back(TriangleTap{std::clamp(start_position, 0, last_source), fixed_point_scale});
  }
  return taps;
}

void fill_yv24_pattern(PVideoFrame& frame) {
  constexpr std::array<std::uint8_t, 3> bases{3, 67, 131};
  for (std::size_t plane_index = 0; plane_index < bases.size(); ++plane_index) {
    const int plane = static_cast<int>(PLANAR_Y + plane_index);
    fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0xa0 + plane_index), plane);
    const int pitch = frame->GetPitch(plane);
    const int width = frame->GetRowSize(plane);
    const int height = frame->GetHeight(plane);
    for (int y = 0; y < height; ++y) {
      auto* row = frame->GetWritePtr(plane) + y * pitch;
      for (int x = 0; x < width; ++x) {
        row[x] = static_cast<std::uint8_t>(bases[plane_index] + x * 19 + y * 37);
      }
    }
  }
}

std::uint8_t vertical_reduce_reference(const PVideoFrame& source, int plane, int x, int y,
                                       int output_height) {
  const int pitch = source->GetPitch(plane);
  const auto* first = source->GetReadPtr(plane) + (2 * y) * pitch;
  if (y + 1 == output_height) {
    const auto* second = first + pitch;
    return static_cast<std::uint8_t>((first[x] + 3 * second[x] + 2) / 4);
  }
  const auto* second = first + pitch;
  const auto* third = second + pitch;
  return static_cast<std::uint8_t>((first[x] + 2 * second[x] + third[x] + 2) / 4);
}

std::uint8_t horizontal_reduce_reference(const PVideoFrame& source, int plane, int x, int y,
                                         int output_width) {
  const int pitch = source->GetPitch(plane);
  const auto* row = source->GetReadPtr(plane) + y * pitch;
  if (x + 1 == output_width) {
    return static_cast<std::uint8_t>((row[2 * x] + row[2 * x + 1] + 1) / 2);
  }
  return static_cast<std::uint8_t>((row[2 * x] + 2 * row[2 * x + 1] + row[2 * x + 2] + 2) / 4);
}

TEST(ReduceBy2Filter, ReducesYv24VerticallyWithFinalRowWeight) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int source_height = 6;
  const auto vi =
      make_video_info(VideoInfoSpec{width, source_height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_yv24_pattern(source);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  VerticalReduceBy2 filter(clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetHeight(), source_height / 2);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    ASSERT_EQ(output->GetRowSize(plane), width);
    for (int y = 0; y < output->GetHeight(plane); ++y) {
      const auto* row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int x = 0; x < width; ++x) {
        EXPECT_EQ(row[x], vertical_reduce_reference(source, plane, x, y, output->GetHeight(plane)))
            << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(ReduceBy2Filter, ReducesYv24HorizontallyWithFinalColumnWeight) {
  AviSynthEnvironment environment;
  constexpr int source_width = 8;
  constexpr int height = 3;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_yv24_pattern(source);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  HorizontalReduceBy2 filter(clip, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetRowSize(PLANAR_Y), source_width / 2);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    ASSERT_EQ(output->GetRowSize(plane), source_width / 2);
    for (int y = 0; y < height; ++y) {
      const auto* row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int x = 0; x < source_width / 2; ++x) {
        EXPECT_EQ(row[x], horizontal_reduce_reference(source, plane, x, y, source_width / 2))
            << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

std::uint8_t point_resize_reference(const PVideoFrame& source, int plane, int source_width,
                                    int source_height, int target_width, int target_height, int x,
                                    int y) {
  const int source_x =
      std::clamp(static_cast<int>(std::floor(static_cast<double>(x) * source_width / target_width)),
                 0, source_width - 1);
  const int source_y = std::clamp(
      static_cast<int>(std::floor(static_cast<double>(y) * source_height / target_height)), 0,
      source_height - 1);
  const auto* row = source->GetReadPtr(plane) + source_y * source->GetPitch(plane);
  return row[source_x];
}

TEST(FilteredResizeFilter, PointResizeHorizontalUsesNearestSourceCoordinates) {
  AviSynthEnvironment environment;
  constexpr int source_width = 7;
  constexpr int source_height = 4;
  constexpr int target_width = 4;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_yv24_pattern(source);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  PointFilter point_filter;

  FilteredResizeH filter(clip, 0.0, static_cast<double>(source_width), target_width, &point_filter,
                         false, -1, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetRowSize(PLANAR_Y), target_width);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    for (int y = 0; y < source_height; ++y) {
      const auto* row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int x = 0; x < target_width; ++x) {
        EXPECT_EQ(row[x], point_resize_reference(source, plane, source_width, source_height,
                                                 target_width, source_height, x, y))
            << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(FilteredResizeFilter, PointResizeVerticalUsesNearestSourceCoordinates) {
  AviSynthEnvironment environment;
  constexpr int source_width = 5;
  constexpr int source_height = 7;
  constexpr int target_height = 4;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_yv24_pattern(source);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  PointFilter point_filter;

  FilteredResizeV filter(clip, 0.0, static_cast<double>(source_height), target_height,
                         &point_filter, false, -1, environment.get());
  const PVideoFrame output = filter.GetFrame(0, environment.get());

  ASSERT_EQ(output->GetHeight(), target_height);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    for (int y = 0; y < target_height; ++y) {
      const auto* row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int x = 0; x < source_width; ++x) {
        EXPECT_EQ(row[x], point_resize_reference(source, plane, source_width, source_height,
                                                 source_width, target_height, x, y))
            << "plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

template <typename Pixel>
Pixel triangle_integer_sample(const PVideoFrame& source, int plane, bool horizontal, int fixed_index,
                              const std::vector<TriangleTap>& taps, int bits_per_pixel) {
  constexpr int scale_bits = sizeof(Pixel) == 1 ? FPScale8bits : FPScale16bits;
  int result = 1 << (scale_bits - 1);
  for (const auto& tap : taps) {
    const auto* row = reinterpret_cast<const Pixel*>(source->GetReadPtr(plane) +
                                                     (horizontal ? fixed_index : tap.source_index) *
                                                         source->GetPitch(plane));
    int value = row[horizontal ? tap.source_index : fixed_index];
    if constexpr (sizeof(Pixel) == sizeof(std::uint16_t)) {
      if (bits_per_pixel == 16) {
        value -= 32768;
      }
    }
    result += value * tap.coefficient;
  }
  if constexpr (sizeof(Pixel) == sizeof(std::uint16_t)) {
    if (bits_per_pixel == 16) {
      result += 32768 << FPScale16bits;
    }
  }
  result >>= scale_bits;
  const int limit = (1 << bits_per_pixel) - 1;
  return static_cast<Pixel>(std::clamp(result, 0, limit));
}

template <typename Pixel>
Pixel extra_resize_integer_sample(const PVideoFrame& source, int plane, bool horizontal,
                                  int fixed_index, const std::vector<TriangleTap>& taps,
                                  int bits_per_pixel) {
  constexpr int scale_bits =
      sizeof(Pixel) == sizeof(std::uint8_t) ? FPScale8bits : FPScale16bits;
  int result = 1 << (scale_bits - 1);
  for (const auto& tap : taps) {
    const auto* row = reinterpret_cast<const Pixel*>(
        source->GetReadPtr(plane) +
        (horizontal ? fixed_index : tap.source_index) * source->GetPitch(plane));
    int value = row[horizontal ? tap.source_index : fixed_index];
    if constexpr (sizeof(Pixel) == sizeof(std::uint16_t)) {
      if (bits_per_pixel == 16) {
        value -= 32768;
      }
    }
    result += value * tap.coefficient;
  }
  if constexpr (sizeof(Pixel) == sizeof(std::uint16_t)) {
    if (bits_per_pixel == 16) {
      result += 32768 << scale_bits;
    }
  }
  result >>= scale_bits;
  const int limit = (1 << bits_per_pixel) - 1;
  return static_cast<Pixel>(std::clamp(result, 0, limit));
}

double plane_center_for_placement(int plane, int placement, const VideoInfo& vi, bool horizontal) {
  if ((!vi.IsYUV() && !vi.IsYUVA()) || (plane != PLANAR_U && plane != PLANAR_V)) {
    return 0.5;
  }
  const int subsampling = horizontal ? vi.GetPlaneWidthSubsampling(plane)
                                     : vi.GetPlaneHeightSubsampling(plane);
  if (subsampling == 0) {
    return 0.5;
  }
  if (horizontal) {
    return placement == AVS_CHROMA_LEFT || placement == AVS_CHROMA_TOP_LEFT ? 0.25 : 0.5;
  }
  return placement == AVS_CHROMA_TOP_LEFT ? 0.25 : 0.5;
}

template <typename Pixel>
void fill_planar_resize_pattern(PVideoFrame& frame, const VideoInfo& vi) {
  for (const int plane : video_frame_planes(vi)) {
    fill_plane_full_pitch(frame, static_cast<std::uint8_t>(0x80 + plane), plane);
    write_frame_plane<Pixel>(frame, plane, [plane](int x, int y) {
      return static_cast<Pixel>(17 + plane * 3 + x * 37 + y * 53);
    });
  }
}

enum class ResizeDirection { Horizontal, Vertical };

struct PlanarResizeCase {
  int pixel_type;
  ResizeDirection direction;
  int placement;
  const char* name;
};

template <typename Pixel>
void run_planar_triangle_case(const PlanarResizeCase& test_case) {
  AviSynthEnvironment environment;
  const bool horizontal = test_case.direction == ResizeDirection::Horizontal;
  const bool planar_rgb = test_case.pixel_type == VideoInfo::CS_RGBP16;
  const int source_width = planar_rgb ? 7 : 10;
  const int source_height = planar_rgb || test_case.pixel_type == VideoInfo::CS_YV16 ? 5 : 6;
  const int target_width = horizontal ? (planar_rgb ? 4 : 6) : source_width;
  const int target_height = horizontal ? source_height : (planar_rgb ||
                                                           test_case.pixel_type == VideoInfo::CS_YV16
                                                               ? 3
                                                               : 4);
  const int bits_per_pixel = sizeof(Pixel) == sizeof(std::uint8_t) ? 8 : 16;
  const auto vi = make_video_info(
      VideoInfoSpec{source_width, source_height, test_case.pixel_type, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_planar_resize_pattern<Pixel>(source, vi);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  TriangleFilter triangle_filter;

  PVideoFrame output;
  if (horizontal) {
    FilteredResizeH filter(clip, 0.0, static_cast<double>(source_width), target_width,
                           &triangle_filter, true, test_case.placement, environment.get());
    EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
    output = filter.GetFrame(0, environment.get());
  } else {
    FilteredResizeV filter(clip, 0.0, static_cast<double>(source_height), target_height,
                           &triangle_filter, true, test_case.placement, environment.get());
    EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
    output = filter.GetFrame(0, environment.get());
  }

  for (const int plane : video_frame_planes(vi)) {
    const bool chroma = !planar_rgb && (plane == PLANAR_U || plane == PLANAR_V);
    const int subsampling = chroma
                                ? (horizontal ? vi.GetPlaneWidthSubsampling(plane)
                                              : vi.GetPlaneHeightSubsampling(plane))
                                : 0;
    const double crop_start = 0.0;
    const double crop_size = horizontal ? static_cast<double>(source_width)
                                        : static_cast<double>(source_height);
    const double plane_crop_start = chroma ? crop_start / (1 << subsampling) : crop_start;
    const double plane_crop_size = chroma ? crop_size / (1 << subsampling) : crop_size;
    const int source_axis = horizontal
                                ? source->GetRowSize(plane) / static_cast<int>(sizeof(Pixel))
                                : source->GetHeight(plane);
    const int target_axis = horizontal
                                ? output->GetRowSize(plane) / static_cast<int>(sizeof(Pixel))
                                : output->GetHeight(plane);
    for (int fixed = 0; fixed < (horizontal ? output->GetHeight(plane)
                                              : output->GetRowSize(plane) /
                                                    static_cast<int>(sizeof(Pixel)));
         ++fixed) {
      for (int index = 0; index < target_axis; ++index) {
        const auto taps = triangle_taps(
            source_axis, plane_crop_start, plane_crop_size, target_axis, index,
            chroma ? plane_center_for_placement(plane, test_case.placement, vi, horizontal) : 0.5,
            bits_per_pixel);
        const Pixel expected = triangle_integer_sample<Pixel>(
            source, plane, horizontal, fixed, taps, bits_per_pixel);
        const int output_x = horizontal ? index : fixed;
        const int output_y = horizontal ? fixed : index;
        const auto* output_row = reinterpret_cast<const Pixel*>(
            output->GetReadPtr(plane) + output_y * output->GetPitch(plane));
        EXPECT_EQ(output_row[output_x], expected)
            << "case=" << test_case.name << " plane=" << plane << " x=" << output_x
            << " y=" << output_y << " direction=" << (horizontal ? "horizontal" : "vertical");
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

class PlanarTriangleResizeTest : public ::testing::TestWithParam<PlanarResizeCase> {};

TEST_P(PlanarTriangleResizeTest, AppliesIndependentTriangleReferenceAcrossPlanes) {
  const auto& test_case = GetParam();
  if (test_case.pixel_type == VideoInfo::CS_RGBP16) {
    run_planar_triangle_case<std::uint16_t>(test_case);
  } else {
    run_planar_triangle_case<std::uint8_t>(test_case);
  }
}

void PrintTo(const PlanarResizeCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(
    Formats, PlanarTriangleResizeTest,
    ::testing::Values(
        PlanarResizeCase{VideoInfo::CS_YV12, ResizeDirection::Horizontal, AVS_CHROMA_CENTER,
                         "Yv12HorizontalMpeg1"},
        PlanarResizeCase{VideoInfo::CS_YV12, ResizeDirection::Vertical, AVS_CHROMA_TOP_LEFT,
                         "Yv12VerticalTopLeft"},
        PlanarResizeCase{VideoInfo::CS_YV16, ResizeDirection::Horizontal, AVS_CHROMA_LEFT,
                         "Yv16HorizontalMpeg2"},
        PlanarResizeCase{VideoInfo::CS_YV16, ResizeDirection::Vertical, AVS_CHROMA_CENTER,
                         "Yv16VerticalMpeg1"},
        PlanarResizeCase{VideoInfo::CS_YUVA420, ResizeDirection::Horizontal, AVS_CHROMA_LEFT,
                         "Yuva420HorizontalMpeg2"},
        PlanarResizeCase{VideoInfo::CS_YUVA420, ResizeDirection::Vertical, AVS_CHROMA_CENTER,
                         "Yuva420VerticalMpeg1"},
        PlanarResizeCase{VideoInfo::CS_RGBP16, ResizeDirection::Horizontal, AVS_CHROMA_UNUSED,
                         "Rgbp16Horizontal"},
        PlanarResizeCase{VideoInfo::CS_RGBP16, ResizeDirection::Vertical, AVS_CHROMA_UNUSED,
                         "Rgbp16Vertical"}),
    [](const ::testing::TestParamInfo<PlanarResizeCase>& info) { return info.param.name; });

struct ExtraResizeCase {
  ExtraResizeKernel kernel;
  ResizeDirection direction;
  const char* name;
};

void PrintTo(const ExtraResizeCase& test_case, std::ostream* stream) { *stream << test_case.name; }

void run_extra_resize_case(const ExtraResizeCase& test_case) {
  AviSynthEnvironment environment;
  const bool horizontal = test_case.direction == ResizeDirection::Horizontal;
  constexpr int source_width = 9;
  constexpr int source_height = 9;
  const int target_width = horizontal ? 5 : source_width;
  const int target_height = horizontal ? source_height : 5;
  const auto vi = make_video_info(
      VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_yv24_pattern(source);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);

  PVideoFrame output;
  std::unique_ptr<ResamplingFunction> filter_function;
  switch (test_case.kernel) {
    case ExtraResizeKernel::MitchellNetravali:
      filter_function = std::make_unique<MitchellNetravaliFilter>();
      break;
    case ExtraResizeKernel::Lanczos3:
      filter_function = std::make_unique<LanczosFilter>(3);
      break;
    case ExtraResizeKernel::Spline36:
      filter_function = std::make_unique<Spline36Filter>();
      break;
    case ExtraResizeKernel::Sinc4:
      filter_function = std::make_unique<SincFilter>(4);
      break;
  }
  if (horizontal) {
    FilteredResizeH filter(clip, 0.0, static_cast<double>(source_width), target_width,
                           filter_function.get(), true, AVS_CHROMA_UNUSED, environment.get());
    EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
    output = filter.GetFrame(0, environment.get());
  } else {
    FilteredResizeV filter(clip, 0.0, static_cast<double>(source_height), target_height,
                           filter_function.get(), true, AVS_CHROMA_UNUSED, environment.get());
    EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
    output = filter.GetFrame(0, environment.get());
  }

  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int source_axis = horizontal ? source->GetRowSize(plane) : source->GetHeight(plane);
    const int target_axis = horizontal ? output->GetRowSize(plane) : output->GetHeight(plane);
    const int fixed_count = horizontal ? output->GetHeight(plane) : output->GetRowSize(plane);
    for (int fixed = 0; fixed < fixed_count; ++fixed) {
      for (int index = 0; index < target_axis; ++index) {
        const auto taps = extra_resize_taps(test_case.kernel, source_axis, 0.0,
                                            static_cast<double>(source_axis), target_axis, index);
        const auto expected =
            extra_resize_integer_sample<std::uint8_t>(source, plane, horizontal, fixed, taps, 8);
        const int output_x = horizontal ? index : fixed;
        const int output_y = horizontal ? fixed : index;
        const auto* output_row = output->GetReadPtr(plane) + output_y * output->GetPitch(plane);
        EXPECT_EQ(output_row[output_x], expected)
            << "case=" << test_case.name << " plane=" << plane << " x=" << output_x
            << " y=" << output_y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

class ExtraResizeTest : public ::testing::TestWithParam<ExtraResizeCase> {};

TEST_P(ExtraResizeTest, AppliesIndependentKernelReferenceAcrossDirections) {
  run_extra_resize_case(GetParam());
}

INSTANTIATE_TEST_SUITE_P(
    Kernels, ExtraResizeTest,
    ::testing::Values(
        ExtraResizeCase{ExtraResizeKernel::MitchellNetravali, ResizeDirection::Horizontal,
                        "MitchellHorizontal"},
        ExtraResizeCase{ExtraResizeKernel::MitchellNetravali, ResizeDirection::Vertical,
                        "MitchellVertical"},
        ExtraResizeCase{ExtraResizeKernel::Lanczos3, ResizeDirection::Horizontal,
                        "Lanczos3Horizontal"},
        ExtraResizeCase{ExtraResizeKernel::Lanczos3, ResizeDirection::Vertical,
                        "Lanczos3Vertical"},
        ExtraResizeCase{ExtraResizeKernel::Spline36, ResizeDirection::Horizontal,
                        "Spline36Horizontal"},
        ExtraResizeCase{ExtraResizeKernel::Spline36, ResizeDirection::Vertical,
                        "Spline36Vertical"},
        ExtraResizeCase{ExtraResizeKernel::Sinc4, ResizeDirection::Horizontal,
                        "Sinc4Horizontal"},
        ExtraResizeCase{ExtraResizeKernel::Sinc4, ResizeDirection::Vertical,
                        "Sinc4Vertical"}),
    [](const ::testing::TestParamInfo<ExtraResizeCase>& info) { return info.param.name; });

struct Spline36Rgb16Case {
  ResizeDirection direction;
  const char* name;
};

void PrintTo(const Spline36Rgb16Case& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

class Spline36Rgb16ResizeTest : public ::testing::TestWithParam<Spline36Rgb16Case> {};

TEST_P(Spline36Rgb16ResizeTest, AppliesSpline36ReferenceToSixteenBitPlanarRgb) {
  const auto& test_case = GetParam();
  const bool horizontal = test_case.direction == ResizeDirection::Horizontal;
  AviSynthEnvironment environment;
  constexpr int source_width = 7;
  constexpr int source_height = 5;
  const int target_width = horizontal ? 4 : source_width;
  const int target_height = horizontal ? source_height : 3;
  const auto vi = make_video_info(
      VideoInfoSpec{source_width, source_height, VideoInfo::CS_RGBP16, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_planar_resize_pattern<std::uint16_t>(source, vi);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  Spline36Filter filter_function;

  PVideoFrame output;
  if (horizontal) {
    FilteredResizeH filter(clip, 0.0, static_cast<double>(source_width), target_width,
                           &filter_function, true, AVS_CHROMA_UNUSED, environment.get());
    EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
    output = filter.GetFrame(0, environment.get());
  } else {
    FilteredResizeV filter(clip, 0.0, static_cast<double>(source_height), target_height,
                           &filter_function, true, AVS_CHROMA_UNUSED, environment.get());
    EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
    output = filter.GetFrame(0, environment.get());
  }

  for (const int plane : video_frame_planes(vi)) {
    const int source_axis = horizontal ? source->GetRowSize(plane) / sizeof(std::uint16_t)
                                       : source->GetHeight(plane);
    const int target_axis = horizontal ? output->GetRowSize(plane) / sizeof(std::uint16_t)
                                       : output->GetHeight(plane);
    const int fixed_count = horizontal ? output->GetHeight(plane)
                                       : output->GetRowSize(plane) / sizeof(std::uint16_t);
    for (int fixed = 0; fixed < fixed_count; ++fixed) {
      for (int index = 0; index < target_axis; ++index) {
        const auto taps = extra_resize_taps(ExtraResizeKernel::Spline36, source_axis, 0.0,
                                            static_cast<double>(source_axis), target_axis, index,
                                            FPScale16);
        const auto expected = extra_resize_integer_sample<std::uint16_t>(
            source, plane, horizontal, fixed, taps, 16);
        const int output_x = horizontal ? index : fixed;
        const int output_y = horizontal ? fixed : index;
        const auto* output_row = reinterpret_cast<const std::uint16_t*>(
            output->GetReadPtr(plane) + output_y * output->GetPitch(plane));
        EXPECT_EQ(output_row[output_x], expected)
            << "case=" << test_case.name << " plane=" << plane << " x=" << output_x
            << " y=" << output_y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(
    SixteenBitPlanarRgb, Spline36Rgb16ResizeTest,
    ::testing::Values(Spline36Rgb16Case{ResizeDirection::Horizontal, "Spline36Horizontal"},
                      Spline36Rgb16Case{ResizeDirection::Vertical, "Spline36Vertical"}),
    [](const ::testing::TestParamInfo<Spline36Rgb16Case>& info) { return info.param.name; });

template <typename Pixel>
void fill_packed_resize_pattern(PVideoFrame& frame, int components) {
  fill_plane_full_pitch(frame, 0x6d, DEFAULT_PLANE);
  write_frame_plane<Pixel>(frame, DEFAULT_PLANE, [components, height = frame->GetHeight()](int index,
                                                                                           int raw_y) {
    const int x = index / components;
    const int component = index % components;
    const int logical_y = height - 1 - raw_y;
    return static_cast<Pixel>(13 + x * 41 + logical_y * 67 + component * 101);
  });
}

template <typename Pixel>
Pixel packed_triangle_sample(const PVideoFrame& source, int components, bool horizontal,
                             int logical_y, int logical_x, int component,
                             const std::vector<TriangleTap>& taps, int bits_per_pixel) {
  constexpr int scale_bits = sizeof(Pixel) == 1 ? FPScale8bits : FPScale16bits;
  int result = 1 << (scale_bits - 1);
  for (const auto& tap : taps) {
    const int raw_y = horizontal ? source->GetHeight() - 1 - logical_y : tap.source_index;
    const int x = horizontal ? tap.source_index : logical_x;
    const auto* row = reinterpret_cast<const Pixel*>(source->GetReadPtr() + raw_y * source->GetPitch());
    int value = row[x * components + component];
    if constexpr (sizeof(Pixel) == sizeof(std::uint16_t)) {
      if (bits_per_pixel == 16) {
        value -= 32768;
      }
    }
    result += value * tap.coefficient;
  }
  if constexpr (sizeof(Pixel) == sizeof(std::uint16_t)) {
    if (bits_per_pixel == 16) {
      result += 32768 << FPScale16bits;
    }
  }
  result >>= scale_bits;
  const int limit = (1 << bits_per_pixel) - 1;
  return static_cast<Pixel>(std::clamp(result, 0, limit));
}

struct PackedResizeCase {
  int pixel_type;
  int components;
  ResizeDirection direction;
  const char* name;
};

template <typename Pixel>
void run_packed_triangle_case(const PackedResizeCase& test_case) {
  AviSynthEnvironment environment;
  const bool horizontal = test_case.direction == ResizeDirection::Horizontal;
  const int source_width = test_case.pixel_type == VideoInfo::CS_BGR24 ? 7 : 5;
  const int source_height = horizontal ? 4 : 5;
  const int target_width = horizontal ? 4 : source_width;
  const int target_height = horizontal ? source_height : 3;
  const int bits_per_pixel = sizeof(Pixel) == sizeof(std::uint8_t) ? 8 : 16;
  const auto vi = make_video_info(
      VideoInfoSpec{source_width, source_height, test_case.pixel_type, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_packed_resize_pattern<Pixel>(source, test_case.components);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  TriangleFilter triangle_filter;

  PVideoFrame output;
  if (horizontal) {
    FilteredResizeH filter(clip, 0.0, static_cast<double>(source_width), target_width,
                           &triangle_filter, true, AVS_CHROMA_UNUSED, environment.get());
    EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
    output = filter.GetFrame(0, environment.get());
  } else {
    FilteredResizeV filter(clip, 0.0, static_cast<double>(source_height), target_height,
                           &triangle_filter, true, AVS_CHROMA_UNUSED, environment.get());
    EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
    output = filter.GetFrame(0, environment.get());
  }

  const int source_axis = horizontal ? source_width : source_height;
  const int target_axis = horizontal ? target_width : target_height;
  for (int y = 0; y < target_height; ++y) {
    const int output_raw_y = target_height - 1 - y;
    const auto* output_row = reinterpret_cast<const Pixel*>(
        output->GetReadPtr() + output_raw_y * output->GetPitch());
    for (int x = 0; x < target_width; ++x) {
      for (int component = 0; component < test_case.components; ++component) {
        const int target_index = horizontal ? x : output_raw_y;
        const auto taps = triangle_taps(source_axis, 0.0, static_cast<double>(source_axis),
                                        target_axis, target_index, 0.5, bits_per_pixel);
        const Pixel expected = packed_triangle_sample<Pixel>(
            source, test_case.components, horizontal, y, x, component, taps, bits_per_pixel);
        EXPECT_EQ(output_row[x * test_case.components + component], expected)
            << "case=" << test_case.name << " component=" << component << " x=" << x
            << " y=" << y;
      }
    }
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

class PackedTriangleResizeTest : public ::testing::TestWithParam<PackedResizeCase> {};

TEST_P(PackedTriangleResizeTest, PreservesPackedComponentsAndBottomUpRows) {
  const auto& test_case = GetParam();
  if (test_case.pixel_type == VideoInfo::CS_BGR24) {
    run_packed_triangle_case<std::uint8_t>(test_case);
  } else {
    run_packed_triangle_case<std::uint16_t>(test_case);
  }
}

void PrintTo(const PackedResizeCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(
    Formats, PackedTriangleResizeTest,
    ::testing::Values(PackedResizeCase{VideoInfo::CS_BGR24, 3, ResizeDirection::Horizontal,
                                       "Bgr24Horizontal"},
                      PackedResizeCase{VideoInfo::CS_BGR24, 3, ResizeDirection::Vertical,
                                       "Bgr24Vertical"},
                      PackedResizeCase{VideoInfo::CS_BGR64, 4, ResizeDirection::Horizontal,
                                       "Bgr64Horizontal"},
                      PackedResizeCase{VideoInfo::CS_BGR64, 4, ResizeDirection::Vertical,
                                       "Bgr64Vertical"}),
    [](const ::testing::TestParamInfo<PackedResizeCase>& info) { return info.param.name; });

class ResizeFramePropertiesTest : public ::testing::TestWithParam<bool> {};

TEST_P(ResizeFramePropertiesTest, PreservesChromaRangeAndFieldProperties) {
  const bool horizontal = GetParam();
  AviSynthEnvironment environment;
  constexpr int source_width = 8;
  constexpr int source_height = 6;
  const auto vi =
      make_video_info(VideoInfoSpec{source_width, source_height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(source, static_cast<std::uint8_t>(0x70 + plane * 23), plane);
  }
  set_frame_property_int(environment.get(), source, "_ChromaLocation", AVS_CHROMA_LEFT);
  set_frame_property_int(environment.get(), source, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source, "_FieldBased", 1);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  PointFilter point_filter;

  PVideoFrame output;
  if (horizontal) {
    FilteredResizeH filter(clip, 0.0, static_cast<double>(source_width), 6, &point_filter, true,
                           AVS_CHROMA_CENTER, environment.get());
    output = filter.GetFrame(0, environment.get());
  } else {
    FilteredResizeV filter(clip, 0.0, static_cast<double>(source_height), 4, &point_filter, true,
                           AVS_CHROMA_CENTER, environment.get());
    output = filter.GetFrame(0, environment.get());
  }

  for (const auto& property : std::array<std::pair<const char*, int>, 3>{
           std::pair{"_ChromaLocation", AVS_CHROMA_LEFT},
           std::pair{"_ColorRange", AVS_COLORRANGE_FULL},
           std::pair{"_FieldBased", 1}}) {
    const auto actual = get_frame_property_int(environment.get(), output, property.first);
    ASSERT_TRUE(actual.has_value()) << property.first;
    EXPECT_EQ(*actual, property.second) << property.first;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(source_clip->frame_requests(), std::vector<int>{0});
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

INSTANTIATE_TEST_SUITE_P(
    Directions, ResizeFramePropertiesTest, ::testing::Values(true, false),
    [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "Horizontal" : "Vertical"; });

TEST(FilteredResizeFilter, RejectsOddYv12HorizontalTargetWidth) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_planar_resize_pattern<std::uint8_t>(source, vi);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  TriangleFilter triangle_filter;

  EXPECT_THROW(
      FilteredResizeH(clip, 0.0, static_cast<double>(width), 7, &triangle_filter, true,
                      AVS_CHROMA_CENTER, environment.get()),
      AvisynthError);
  EXPECT_TRUE(source_clip->frame_requests().empty());
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

TEST(FilteredResizeFilter, RejectsOddYv12VerticalTargetHeight) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  constexpr int height = 6;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source = environment.get()->NewVideoFrame(vi);
  fill_planar_resize_pattern<std::uint8_t>(source, vi);
  const auto source_before = FrameSnapshot::capture(source, vi);
  auto* source_clip = new StaticFrameClip(vi, source);
  const PClip clip(source_clip);
  TriangleFilter triangle_filter;

  EXPECT_THROW(
      FilteredResizeV(clip, 0.0, static_cast<double>(height), 5, &triangle_filter, true,
                      AVS_CHROMA_CENTER, environment.get()),
      AvisynthError);
  EXPECT_TRUE(source_clip->frame_requests().empty());
  EXPECT_EQ(FrameSnapshot::capture(source, vi), source_before);
}

}  // namespace
