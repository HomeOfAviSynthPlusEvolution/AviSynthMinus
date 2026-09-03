#include <gtest/gtest.h>

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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace avsut::test {
namespace {

constexpr int kSourceWidth = 8;
constexpr int kSourceHeight = 4;
constexpr int kYv12Width = 8;
constexpr int kYv12Height = 6;
constexpr int kInterlacedYv12Height = 8;

VideoInfo yv16_video_info() {
  return make_video_info(VideoInfoSpec{kSourceWidth, kSourceHeight, VideoInfo::CS_YV16, 1, 25, 1});
}

void fill_yuv8_source(PVideoFrame& frame) {
  fill_plane_full_pitch(frame, 0xe1, PLANAR_Y);
  fill_plane_full_pitch(frame, 0xd2, PLANAR_U);
  fill_plane_full_pitch(frame, 0xc3, PLANAR_V);
  write_frame_plane<std::uint8_t>(frame, PLANAR_Y,
                                  [](int x, int y) { return 7 + x * 13 + y * 19; });
  write_frame_plane<std::uint8_t>(frame, PLANAR_U,
                                  [](int x, int y) { return 31 + x * 29 + y * 37; });
  write_frame_plane<std::uint8_t>(frame, PLANAR_V,
                                  [](int x, int y) { return 83 + x * 17 + y * 23; });
}

VideoInfo yv12_video_info() {
  return make_video_info(VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_YV12, 1, 25, 1});
}

void fill_y8_source(PVideoFrame& frame) {
  fill_plane_full_pitch(frame, 0x6f, PLANAR_Y);
  write_frame_plane<std::uint8_t>(frame, PLANAR_Y,
                                  [](int x, int y) { return 9 + x * 23 + y * 31; });
}

struct ResamplingTap {
  int source_index;
  int coefficient;
};

double triangle_kernel(double distance) {
  return distance < 1.0 ? 1.0 - distance : 0.0;
}

double mitchell_netravali_kernel(double distance) {
  constexpr double b = 1.0 / 3.0;
  constexpr double c = 1.0 / 3.0;
  constexpr double p0 = (6.0 - 2.0 * b) / 6.0;
  constexpr double p2 = (-18.0 + 12.0 * b + 6.0 * c) / 6.0;
  constexpr double p3 = (12.0 - 9.0 * b - 6.0 * c) / 6.0;
  constexpr double q0 = (8.0 * b + 24.0 * c) / 6.0;
  constexpr double q1 = (-12.0 * b - 48.0 * c) / 6.0;
  constexpr double q2 = (6.0 * b + 30.0 * c) / 6.0;
  constexpr double q3 = (-b - 6.0 * c) / 6.0;
  distance = std::abs(distance);
  return distance < 1.0
             ? p0 + distance * distance * (p2 + distance * p3)
             : distance < 2.0
                   ? q0 + distance * (q1 + distance * (q2 + distance * q3))
                   : 0.0;
}

using ResamplingKernel = double (*)(double);

std::vector<ResamplingTap> resampling_taps(int source_size, double crop_start, double crop_size,
                                           int target_size, int target_index, double support,
                                           ResamplingKernel kernel) {
  constexpr int scale = 1 << 14;
  const double source_step = crop_size / static_cast<double>(target_size);
  const double filter_support = support * std::max(source_step, 1.0);
  const int filter_size = std::max(static_cast<int>(std::ceil(filter_support * 2.0)), 1);
  const int last_source = source_size - 1;
  const double position = crop_start + source_step * 0.5 - 0.5 +
                          source_step * static_cast<double>(target_index);
  const int start_position = static_cast<int>(position + filter_support) - filter_size + 1;

  std::vector<double> raw_coefficients;
  raw_coefficients.reserve(static_cast<std::size_t>(filter_size));
  double total = 0.0;
  for (int tap = 0; tap < filter_size; ++tap) {
    const double distance =
        std::abs((position - (start_position + tap)) / std::max(source_step, 1.0));
    const double coefficient = kernel(distance);
    raw_coefficients.push_back(coefficient);
    total += coefficient;
  }
  if (total == 0.0) {
    total = 1.0;
  }

  std::vector<ResamplingTap> taps;
  double accumulated = 0.0;
  double previous = 0.0;
  for (int tap = 0; tap < filter_size; ++tap) {
    const int source_index = start_position + tap;
    accumulated += raw_coefficients[static_cast<std::size_t>(tap)];
    if (source_index >= 0 && source_index <= last_source) {
      const double current = previous + accumulated / total;
      const int current_fixed = static_cast<int>(current * scale + 0.5);
      const int previous_fixed = static_cast<int>(previous * scale + 0.5);
      taps.push_back(ResamplingTap{source_index, current_fixed - previous_fixed});
      previous = current;
      accumulated = 0.0;
    }
  }

  if (accumulated != 0.0) {
    const double current = previous + accumulated / total;
    const int current_fixed = static_cast<int>(current * scale + 0.5);
    const int previous_fixed = static_cast<int>(previous * scale + 0.5);
    if (!taps.empty()) {
      taps.back().coefficient += current_fixed - previous_fixed;
    } else {
      taps.push_back(ResamplingTap{std::clamp(start_position, 0, last_source), current_fixed});
    }
  }

  if (taps.empty()) {
    taps.push_back(ResamplingTap{std::clamp(start_position, 0, last_source), scale});
  }
  return taps;
}

std::vector<ResamplingTap> bilinear_taps(int source_size, double crop_start, double crop_size,
                                         int target_size, int target_index) {
  return resampling_taps(source_size, crop_start, crop_size, target_size, target_index, 1.0,
                         triangle_kernel);
}

std::vector<ResamplingTap> bicubic_taps(int source_size, double crop_start, double crop_size,
                                        int target_size, int target_index) {
  return resampling_taps(source_size, crop_start, crop_size, target_size, target_index, 2.0,
                         mitchell_netravali_kernel);
}

double sinc_kernel(double distance) {
  constexpr double pi = 3.14159265358979323846;
  distance = std::abs(distance);
  if (distance > 0.000001) {
    distance *= pi;
    return std::sin(distance) / distance;
  }
  return 1.0;
}

double lanczos_kernel(double distance) {
  constexpr double taps = 3.0;
  distance = std::abs(distance);
  return distance < taps ? sinc_kernel(distance) * sinc_kernel(distance / taps) : 0.0;
}

std::vector<ResamplingTap> lanczos_taps(int source_size, double crop_start, double crop_size,
                                        int target_size, int target_index) {
  return resampling_taps(source_size, crop_start, crop_size, target_size, target_index, 3.0,
                         lanczos_kernel);
}

std::vector<ResamplingTap> sinc_taps(int source_size, double crop_start, double crop_size,
                                     int target_size, int target_index) {
  return resampling_taps(source_size, crop_start, crop_size, target_size, target_index, 4.0,
                         sinc_kernel);
}

using ResamplingTapFactory = std::vector<ResamplingTap> (*)(int, double, double, int, int);

std::vector<std::uint8_t> expected_resampled_chroma(const PVideoFrame& source, int plane,
                                                    int source_width, int source_height,
                                                    int target_width, int target_height,
                                                    double crop_left, double crop_top,
                                                    ResamplingTapFactory make_taps) {
  constexpr int scale_bits = 14;
  std::vector<std::uint8_t> horizontal(
      static_cast<std::size_t>(target_width) * static_cast<std::size_t>(source_height));
  for (int y = 0; y < source_height; ++y) {
    const auto* source_row = source->GetReadPtr(plane) + y * source->GetPitch(plane);
    for (int x = 0; x < target_width; ++x) {
      int result = 1 << (scale_bits - 1);
      for (const auto& tap : make_taps(source_width, crop_left, source_width, target_width, x)) {
        result += source_row[tap.source_index] * tap.coefficient;
      }
      horizontal[static_cast<std::size_t>(y * target_width + x)] = static_cast<std::uint8_t>(
          std::clamp(result >> scale_bits, 0, 255));
    }
  }

  std::vector<std::uint8_t> expected(
      static_cast<std::size_t>(target_width) * static_cast<std::size_t>(target_height));
  for (int y = 0; y < target_height; ++y) {
    const auto taps = make_taps(source_height, crop_top, source_height, target_height, y);
    for (int x = 0; x < target_width; ++x) {
      int result = 1 << (scale_bits - 1);
      for (const auto& tap : taps) {
        result += horizontal[static_cast<std::size_t>(tap.source_index * target_width + x)] *
                  tap.coefficient;
      }
      expected[static_cast<std::size_t>(y * target_width + x)] = static_cast<std::uint8_t>(
          std::clamp(result >> scale_bits, 0, 255));
    }
  }
  return expected;
}

std::vector<std::uint8_t> expected_bilinear_chroma(const PVideoFrame& source, int plane,
                                                    int source_width, int source_height,
                                                    int target_width, int target_height,
                                                    double crop_left, double crop_top) {
  return expected_resampled_chroma(source, plane, source_width, source_height, target_width,
                                   target_height, crop_left, crop_top, bilinear_taps);
}

std::vector<std::uint8_t> expected_bicubic_chroma(const PVideoFrame& source, int plane,
                                                   int source_width, int source_height,
                                                   int target_width, int target_height,
                                                   double crop_left, double crop_top) {
  return expected_resampled_chroma(source, plane, source_width, source_height, target_width,
                                   target_height, crop_left, crop_top, bicubic_taps);
}

std::vector<std::uint8_t> expected_interlaced_resampled_chroma(
    const PVideoFrame& source, int plane, int source_width, int source_height, int target_width,
    int target_height, double crop_left, double top_crop, double bottom_crop,
    ResamplingTapFactory make_taps) {
  constexpr int scale_bits = 14;
  const int source_field_height = source_height / 2;
  const int target_field_height = target_height / 2;
  std::vector<std::uint8_t> expected(
      static_cast<std::size_t>(target_width) * static_cast<std::size_t>(target_height));

  for (int field = 0; field < 2; ++field) {
    std::vector<std::uint8_t> horizontal(
        static_cast<std::size_t>(target_width) * static_cast<std::size_t>(source_field_height));
    for (int field_y = 0; field_y < source_field_height; ++field_y) {
      const int source_y = field + field_y * 2;
      const auto* source_row = source->GetReadPtr(plane) + source_y * source->GetPitch(plane);
      for (int x = 0; x < target_width; ++x) {
        int result = 1 << (scale_bits - 1);
        for (const auto& tap : make_taps(source_width, crop_left, source_width, target_width, x)) {
          result += source_row[tap.source_index] * tap.coefficient;
        }
        horizontal[static_cast<std::size_t>(field_y * target_width + x)] =
            static_cast<std::uint8_t>(std::clamp(result >> scale_bits, 0, 255));
      }
    }

    const double crop_top = field == 0 ? top_crop : bottom_crop;
    for (int field_y = 0; field_y < target_field_height; ++field_y) {
      const auto taps = make_taps(source_field_height, crop_top, source_field_height,
                                  target_field_height, field_y);
      const int output_y = field_y * 2 + field;
      for (int x = 0; x < target_width; ++x) {
        int result = 1 << (scale_bits - 1);
        for (const auto& tap : taps) {
          result += horizontal[static_cast<std::size_t>(tap.source_index * target_width + x)] *
                    tap.coefficient;
        }
        expected[static_cast<std::size_t>(output_y * target_width + x)] =
            static_cast<std::uint8_t>(std::clamp(result >> scale_bits, 0, 255));
      }
    }
  }
  return expected;
}

std::vector<std::uint8_t> expected_interlaced_bilinear_chroma(const PVideoFrame& source, int plane,
                                                              int source_width, int source_height,
                                                              int target_width, int target_height,
                                                              double crop_left, double top_crop,
                                                              double bottom_crop) {
  return expected_interlaced_resampled_chroma(source, plane, source_width, source_height,
                                              target_width, target_height, crop_left, top_crop,
                                              bottom_crop, bilinear_taps);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv16ChromaWithPointFilterAndCopiesProperties) {
  AviSynthEnvironment environment;
  const auto source_vi = yv16_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_LEFT, point_resampler,
                                no_parameter, no_parameter, no_parameter, AVS_CHROMA_UNUSED,
                                environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kSourceWidth);
  ASSERT_EQ(filter.GetVideoInfo().height, kSourceHeight);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kSourceWidth);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kSourceWidth);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kSourceWidth);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kSourceHeight);

  for (int y = 0; y < kSourceHeight; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < kSourceWidth; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
    }

    const auto* source_u =
        source_frame->GetReadPtr(PLANAR_U) + y * source_frame->GetPitch(PLANAR_U);
    const auto* source_v =
        source_frame->GetReadPtr(PLANAR_V) + y * source_frame->GetPitch(PLANAR_V);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kSourceWidth; ++x) {
      EXPECT_EQ(output_u[x], source_u[x / 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x / 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12ChromaAcrossBothAxesWithTopLeftPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_TOP_LEFT,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* source_u =
        source_frame->GetReadPtr(PLANAR_U) + (y / 2) * source_frame->GetPitch(PLANAR_U);
    const auto* source_v =
        source_frame->GetReadPtr(PLANAR_V) + (y / 2) * source_frame->GetPitch(PLANAR_V);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], source_u[x / 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x / 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12ChromaWithBilinearTopLeftPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue bilinear_resampler("bilinear");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_TOP_LEFT,
                                bilinear_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  const auto expected_u = expected_bilinear_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      0.25);
  const auto expected_v = expected_bilinear_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      0.25);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12ChromaWithBilinearCenterPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_CENTER);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue bilinear_resampler("bilinear");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_CENTER,
                                bilinear_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  const auto expected_u = expected_bilinear_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.0,
      0.0);
  const auto expected_v = expected_bilinear_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.0,
      0.0);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12ChromaWithBilinearTopPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_TOP);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue bilinear_resampler("bilinear");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_TOP,
                                bilinear_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  const auto expected_u = expected_bilinear_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.0,
      0.25);
  const auto expected_v = expected_bilinear_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.0,
      0.25);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12ChromaWithBilinearBottomPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_BOTTOM);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue bilinear_resampler("bilinear");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_BOTTOM,
                                bilinear_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  const auto expected_u = expected_bilinear_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.0,
      -0.25);
  const auto expected_v = expected_bilinear_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.0,
      -0.25);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12ChromaWithBilinearBottomLeftPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation",
                         AVS_CHROMA_BOTTOM_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue bilinear_resampler("bilinear");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_BOTTOM_LEFT,
                                bilinear_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  const auto expected_u = expected_bilinear_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      -0.25);
  const auto expected_v = expected_bilinear_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      -0.25);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12ChromaWithBilinearLeftPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue bilinear_resampler("bilinear");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_LEFT,
                                bilinear_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  const auto expected_u = expected_bilinear_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      0.0);
  const auto expected_v = expected_bilinear_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      0.0);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12ChromaWithDefaultBicubicResampler) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_TOP_LEFT,
                                no_parameter, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  const auto expected_u = expected_bicubic_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      0.25);
  const auto expected_v = expected_bicubic_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      0.25);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12ChromaWithLanczosTopLeftPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue lanczos_resampler("lanczos");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_TOP_LEFT,
                                lanczos_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  const auto expected_u = expected_resampled_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      0.25, lanczos_taps);
  const auto expected_v = expected_resampled_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      0.25, lanczos_taps);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12ChromaWithSincTopLeftPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue sinc_resampler("sinc");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_TOP_LEFT,
                                sinc_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  const auto expected_u = expected_resampled_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      0.25, sinc_taps);
  const auto expected_v = expected_resampled_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kYv12Height / 2, kYv12Width, kYv12Height, 0.25,
      0.25, sinc_taps);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, DownsamplesYv24ChromaWithBilinearTopLeftPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue bilinear_resampler("bilinear");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV12, false, AVS_CHROMA_UNUSED,
                                bilinear_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_TOP_LEFT, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV12);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height / 2);

  const auto expected_u = expected_bilinear_chroma(
      source_frame, PLANAR_U, kYv12Width, kYv12Height, kYv12Width / 2, kYv12Height / 2, -0.5,
      -0.5);
  const auto expected_v = expected_bilinear_chroma(
      source_frame, PLANAR_V, kYv12Width, kYv12Height, kYv12Width / 2, kYv12Height / 2, -0.5,
      -0.5);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
    }
  }
  for (int y = 0; y < kYv12Height / 2; ++y) {
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width / 2; ++x) {
      const auto index = static_cast<std::size_t>(y * (kYv12Width / 2) + x);
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_TOP_LEFT});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, DownsamplesYv24ChromaWithBilinearCenterPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue bilinear_resampler("bilinear");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV12, false, AVS_CHROMA_UNUSED,
                                bilinear_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_CENTER, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV12);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height / 2);

  const auto expected_u = expected_bilinear_chroma(
      source_frame, PLANAR_U, kYv12Width, kYv12Height, kYv12Width / 2, kYv12Height / 2, 0.0,
      0.0);
  const auto expected_v = expected_bilinear_chroma(
      source_frame, PLANAR_V, kYv12Width, kYv12Height, kYv12Width / 2, kYv12Height / 2, 0.0,
      0.0);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
    }
  }
  for (int y = 0; y < kYv12Height / 2; ++y) {
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width / 2; ++x) {
      const auto index = static_cast<std::size_t>(y * (kYv12Width / 2) + x);
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_CENTER});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, DownsamplesYv24ChromaWithBilinearLeftPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue bilinear_resampler("bilinear");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV12, false, AVS_CHROMA_UNUSED,
                                bilinear_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_LEFT, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV12);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height / 2);

  const auto expected_u = expected_bilinear_chroma(
      source_frame, PLANAR_U, kYv12Width, kYv12Height, kYv12Width / 2, kYv12Height / 2, -0.5,
      0.0);
  const auto expected_v = expected_bilinear_chroma(
      source_frame, PLANAR_V, kYv12Width, kYv12Height, kYv12Width / 2, kYv12Height / 2, -0.5,
      0.0);
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
    }
  }
  for (int y = 0; y < kYv12Height / 2; ++y) {
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width / 2; ++x) {
      const auto index = static_cast<std::size_t>(y * (kYv12Width / 2) + x);
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_LEFT});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, ConvertsYv12ToYv16AndSetsOutputChromaPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV16, false, AVS_CHROMA_TOP_LEFT,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_CENTER, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV16);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* source_u =
        source_frame->GetReadPtr(PLANAR_U) + (y / 2) * source_frame->GetPitch(PLANAR_U);
    const auto* source_v =
        source_frame->GetReadPtr(PLANAR_V) + (y / 2) * source_frame->GetPitch(PLANAR_V);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
    }
    for (int x = 0; x < kYv12Width / 2; ++x) {
      EXPECT_EQ(output_u[x], source_u[x]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_CENTER});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, ExpandsY8ToYuva420WithNeutralChromaAndOpaqueAlpha) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_y8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YUVA420, false, AVS_CHROMA_UNUSED,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_LEFT, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YUVA420);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_A), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_A), kYv12Height);

  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_a = output->GetReadPtr(PLANAR_A) + y * output->GetPitch(PLANAR_A);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_a[x], 255) << "plane=A row=" << y << " column=" << x;
    }
  }
  for (int y = 0; y < kYv12Height / 2; ++y) {
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width / 2; ++x) {
      EXPECT_EQ(output_u[x], 128) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], 128) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_LEFT});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYuva420AndPreservesAlphaPlane) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_YUVA420, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  fill_plane_full_pitch(source_frame, 0x4d, PLANAR_A);
  write_frame_plane<std::uint8_t>(source_frame, PLANAR_A,
                                  [](int x, int y) { return 23 + x * 11 + y * 17; });
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YUVA444, false, AVS_CHROMA_TOP_LEFT,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YUVA444);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_A), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_A), kYv12Height);

  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* source_a =
        source_frame->GetReadPtr(PLANAR_A) + y * source_frame->GetPitch(PLANAR_A);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_a = output->GetReadPtr(PLANAR_A) + y * output->GetPitch(PLANAR_A);
    const auto* source_u =
        source_frame->GetReadPtr(PLANAR_U) + (y / 2) * source_frame->GetPitch(PLANAR_U);
    const auto* source_v =
        source_frame->GetReadPtr(PLANAR_V) + (y / 2) * source_frame->GetPitch(PLANAR_V);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_a[x], source_a[x]) << "plane=A row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], source_u[x / 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x / 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYuv420P16ToYuv444P16WithPointFilter) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_YUV420P16, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_plane_full_pitch(source_frame, 0x21, PLANAR_Y);
  fill_plane_full_pitch(source_frame, 0x32, PLANAR_U);
  fill_plane_full_pitch(source_frame, 0x43, PLANAR_V);
  write_frame_plane<std::uint16_t>(source_frame, PLANAR_Y,
                                   [](int x, int y) { return 1000 + x * 701 + y * 907; });
  write_frame_plane<std::uint16_t>(source_frame, PLANAR_U,
                                   [](int x, int y) { return 20000 + x * 1003 + y * 1201; });
  write_frame_plane<std::uint16_t>(source_frame, PLANAR_V,
                                   [](int x, int y) { return 40000 + x * 503 + y * 1307; });
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YUV444P16, false, AVS_CHROMA_TOP_LEFT,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YUV444P16);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width * sizeof(std::uint16_t));
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width * sizeof(std::uint16_t));
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width * sizeof(std::uint16_t));
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y = reinterpret_cast<const std::uint16_t*>(
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y));
    const auto* source_u = reinterpret_cast<const std::uint16_t*>(
        source_frame->GetReadPtr(PLANAR_U) + (y / 2) * source_frame->GetPitch(PLANAR_U));
    const auto* source_v = reinterpret_cast<const std::uint16_t*>(
        source_frame->GetReadPtr(PLANAR_V) + (y / 2) * source_frame->GetPitch(PLANAR_V));
    const auto* output_y = reinterpret_cast<const std::uint16_t*>(output->GetReadPtr(PLANAR_Y) +
                                                                  y * output->GetPitch(PLANAR_Y));
    const auto* output_u = reinterpret_cast<const std::uint16_t*>(output->GetReadPtr(PLANAR_U) +
                                                                  y * output->GetPitch(PLANAR_U));
    const auto* output_v = reinterpret_cast<const std::uint16_t*>(output->GetReadPtr(PLANAR_V) +
                                                                  y * output->GetPitch(PLANAR_V));
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], source_u[x / 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x / 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYuv420PsToYuv444PsWithPointFilter) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_YUV420PS, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_plane_full_pitch(source_frame, 0x51, PLANAR_Y);
  fill_plane_full_pitch(source_frame, 0x62, PLANAR_U);
  fill_plane_full_pitch(source_frame, 0x73, PLANAR_V);
  write_frame_plane<float>(source_frame, PLANAR_Y,
                           [](int x, int y) { return 0.1F + x * 0.07F + y * 0.11F; });
  write_frame_plane<float>(source_frame, PLANAR_U,
                           [](int x, int y) { return -0.8F + x * 0.13F + y * 0.17F; });
  write_frame_plane<float>(source_frame, PLANAR_V,
                           [](int x, int y) { return 0.6F - x * 0.09F + y * 0.07F; });
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YUV444PS, false, AVS_CHROMA_TOP_LEFT,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YUV444PS);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width * sizeof(float));
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width * sizeof(float));
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width * sizeof(float));
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y = reinterpret_cast<const float*>(source_frame->GetReadPtr(PLANAR_Y) +
                                                          y * source_frame->GetPitch(PLANAR_Y));
    const auto* source_u = reinterpret_cast<const float*>(
        source_frame->GetReadPtr(PLANAR_U) + (y / 2) * source_frame->GetPitch(PLANAR_U));
    const auto* source_v = reinterpret_cast<const float*>(
        source_frame->GetReadPtr(PLANAR_V) + (y / 2) * source_frame->GetPitch(PLANAR_V));
    const auto* output_y = reinterpret_cast<const float*>(output->GetReadPtr(PLANAR_Y) +
                                                          y * output->GetPitch(PLANAR_Y));
    const auto* output_u = reinterpret_cast<const float*>(output->GetReadPtr(PLANAR_U) +
                                                          y * output->GetPitch(PLANAR_U));
    const auto* output_v = reinterpret_cast<const float*>(output->GetReadPtr(PLANAR_V) +
                                                          y * output->GetPitch(PLANAR_V));
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_FLOAT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_FLOAT_EQ(output_u[x], source_u[x / 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_FLOAT_EQ(output_v[x], source_v[x / 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, ExtractsYv24LumaAsY8Subframe) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kSourceWidth, kSourceHeight, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_Y8, false, AVS_CHROMA_UNUSED, point_resampler,
                                no_parameter, no_parameter, no_parameter, AVS_CHROMA_UNUSED,
                                environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_Y8);
  ASSERT_EQ(filter.GetVideoInfo().width, kSourceWidth);
  ASSERT_EQ(filter.GetVideoInfo().height, kSourceHeight);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kSourceWidth);
  ASSERT_EQ(output->GetHeight(PLANAR_Y), kSourceHeight);
  for (int y = 0; y < kSourceHeight; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < kSourceWidth; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, ExpandsY16ToYuv444P16WithNeutralChroma) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kSourceWidth, kSourceHeight, VideoInfo::CS_Y16, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_plane_full_pitch(source_frame, 0x2a, PLANAR_Y);
  write_frame_plane<std::uint16_t>(source_frame, PLANAR_Y,
                                   [](int x, int y) { return 1000 + x * 701 + y * 907; });
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YUV444P16, false, AVS_CHROMA_UNUSED,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YUV444P16);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kSourceWidth * sizeof(std::uint16_t));
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kSourceWidth * sizeof(std::uint16_t));
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kSourceWidth * sizeof(std::uint16_t));
  for (int y = 0; y < kSourceHeight; ++y) {
    const auto* source_y = reinterpret_cast<const std::uint16_t*>(
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y));
    const auto* output_y = reinterpret_cast<const std::uint16_t*>(output->GetReadPtr(PLANAR_Y) +
                                                                  y * output->GetPitch(PLANAR_Y));
    const auto* output_u = reinterpret_cast<const std::uint16_t*>(output->GetReadPtr(PLANAR_U) +
                                                                  y * output->GetPitch(PLANAR_U));
    const auto* output_v = reinterpret_cast<const std::uint16_t*>(output->GetReadPtr(PLANAR_V) +
                                                                  y * output->GetPitch(PLANAR_V));
    for (int x = 0; x < kSourceWidth; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], 32768) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], 32768) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, ExpandsY32ToYuv444PsWithNeutralChroma) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kSourceWidth, kSourceHeight, VideoInfo::CS_Y32, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_plane_full_pitch(source_frame, 0x3b, PLANAR_Y);
  write_frame_plane<float>(source_frame, PLANAR_Y,
                           [](int x, int y) { return 0.1F + x * 0.07F + y * 0.11F; });
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YUV444PS, false, AVS_CHROMA_UNUSED,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YUV444PS);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kSourceWidth * sizeof(float));
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kSourceWidth * sizeof(float));
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kSourceWidth * sizeof(float));
  for (int y = 0; y < kSourceHeight; ++y) {
    const auto* source_y = reinterpret_cast<const float*>(source_frame->GetReadPtr(PLANAR_Y) +
                                                          y * source_frame->GetPitch(PLANAR_Y));
    const auto* output_y = reinterpret_cast<const float*>(output->GetReadPtr(PLANAR_Y) +
                                                          y * output->GetPitch(PLANAR_Y));
    const auto* output_u = reinterpret_cast<const float*>(output->GetReadPtr(PLANAR_U) +
                                                          y * output->GetPitch(PLANAR_U));
    const auto* output_v = reinterpret_cast<const float*>(output->GetReadPtr(PLANAR_V) +
                                                          y * output->GetPitch(PLANAR_V));
    for (int x = 0; x < kSourceWidth; ++x) {
      EXPECT_FLOAT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_FLOAT_EQ(output_u[x], 0.0F) << "plane=U row=" << y << " column=" << x;
      EXPECT_FLOAT_EQ(output_v[x], 0.0F) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, DownsamplesYv24ToYv16WithPointFilter) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kSourceWidth, kSourceHeight, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV16, false, AVS_CHROMA_UNUSED,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_CENTER, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV16);
  ASSERT_EQ(filter.GetVideoInfo().width, kSourceWidth);
  ASSERT_EQ(filter.GetVideoInfo().height, kSourceHeight);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kSourceWidth);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kSourceWidth / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kSourceWidth / 2);
  for (int y = 0; y < kSourceHeight; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* source_u =
        source_frame->GetReadPtr(PLANAR_U) + y * source_frame->GetPitch(PLANAR_U);
    const auto* source_v =
        source_frame->GetReadPtr(PLANAR_V) + y * source_frame->GetPitch(PLANAR_V);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kSourceWidth; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
    }
    for (int x = 0; x < kSourceWidth / 2; ++x) {
      EXPECT_EQ(output_u[x], source_u[x * 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x * 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_CENTER});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, DownsamplesYv24ToYv12WithTopLeftPointFilter) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV12, false, AVS_CHROMA_UNUSED,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_TOP_LEFT, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV12);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height / 2);

  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
    }
  }
  for (int y = 0; y < kYv12Height / 2; ++y) {
    const auto* source_u = source_frame->GetReadPtr(PLANAR_U) +
                           (y * 2) * source_frame->GetPitch(PLANAR_U);
    const auto* source_v = source_frame->GetReadPtr(PLANAR_V) +
                           (y * 2) * source_frame->GetPitch(PLANAR_V);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width / 2; ++x) {
      EXPECT_EQ(output_u[x], source_u[x * 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x * 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_TOP_LEFT});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, DownsamplesYv16ToYv12WithPointFilter) {
  AviSynthEnvironment environment;
  const auto source_vi = yv16_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV12, false, AVS_CHROMA_LEFT,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_TOP_LEFT, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV12);
  ASSERT_EQ(filter.GetVideoInfo().width, kSourceWidth);
  ASSERT_EQ(filter.GetVideoInfo().height, kSourceHeight);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kSourceWidth);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kSourceWidth / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kSourceWidth / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kSourceHeight / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kSourceHeight / 2);

  for (int y = 0; y < kSourceHeight; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < kSourceWidth; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
    }
  }
  for (int y = 0; y < kSourceHeight / 2; ++y) {
    const auto* source_u = source_frame->GetReadPtr(PLANAR_U) +
                           (y * 2) * source_frame->GetPitch(PLANAR_U);
    const auto* source_v = source_frame->GetReadPtr(PLANAR_V) +
                           (y * 2) * source_frame->GetPitch(PLANAR_V);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kSourceWidth / 2; ++x) {
      EXPECT_EQ(output_u[x], source_u[x]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_TOP_LEFT});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, DownsamplesYuva444ToYuva420AndPreservesAlpha) {
  AviSynthEnvironment environment;
  const auto source_vi = make_video_info(
      VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_YUVA444, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  fill_plane_full_pitch(source_frame, 0x4d, PLANAR_A);
  write_frame_plane<std::uint8_t>(source_frame, PLANAR_A,
                                  [](int x, int y) { return 23 + x * 11 + y * 17; });
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YUVA420, false, AVS_CHROMA_UNUSED,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_TOP_LEFT, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YUVA420);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_A), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_A), kYv12Height);

  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* source_a =
        source_frame->GetReadPtr(PLANAR_A) + y * source_frame->GetPitch(PLANAR_A);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_a = output->GetReadPtr(PLANAR_A) + y * output->GetPitch(PLANAR_A);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_a[x], source_a[x]) << "plane=A row=" << y << " column=" << x;
    }
  }
  for (int y = 0; y < kYv12Height / 2; ++y) {
    const auto* source_u = source_frame->GetReadPtr(PLANAR_U) +
                           (y * 2) * source_frame->GetPitch(PLANAR_U);
    const auto* source_v = source_frame->GetReadPtr(PLANAR_V) +
                           (y * 2) * source_frame->GetPitch(PLANAR_V);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width / 2; ++x) {
      EXPECT_EQ(output_u[x], source_u[x * 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x * 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_TOP_LEFT});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesYv12DvChromaToYv24WithSeparateUvPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = yv12_video_info();
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_DV);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, false, AVS_CHROMA_DV,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height);

  constexpr int source_u_rows[kYv12Height] = {0, 0, 0, 1, 1, 2};
  constexpr int source_v_rows[kYv12Height] = {0, 0, 1, 1, 2, 2};
  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* source_u = source_frame->GetReadPtr(PLANAR_U) +
                           source_u_rows[y] * source_frame->GetPitch(PLANAR_U);
    const auto* source_v = source_frame->GetReadPtr(PLANAR_V) +
                           source_v_rows[y] * source_frame->GetPitch(PLANAR_V);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], source_u[x / 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x / 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, DownsamplesYv24ToYv12WithDvOutputPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi =
      make_video_info(VideoInfoSpec{kYv12Width, kYv12Height, VideoInfo::CS_YV24, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV12, false, AVS_CHROMA_UNUSED,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_DV, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV12);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kYv12Height / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kYv12Height / 2);

  for (int y = 0; y < kYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
    }
  }
  for (int y = 0; y < kYv12Height / 2; ++y) {
    const auto* source_u = source_frame->GetReadPtr(PLANAR_U) +
                           (y * 2 + 1) * source_frame->GetPitch(PLANAR_U);
    const auto* source_v = source_frame->GetReadPtr(PLANAR_V) +
                           (y * 2) * source_frame->GetPitch(PLANAR_V);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width / 2; ++x) {
      EXPECT_EQ(output_u[x], source_u[x * 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x * 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_DV});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, PreservesInterlacedYv12WithMatchingPointPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = make_video_info(VideoInfoSpec{
      kYv12Width, kInterlacedYv12Height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation", AVS_CHROMA_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV12, true, AVS_CHROMA_LEFT,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_LEFT, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV12);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kInterlacedYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width / 2);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kInterlacedYv12Height / 2);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kInterlacedYv12Height / 2);

  for (int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int width = output->GetRowSize(plane);
    const int height = output->GetHeight(plane);
    for (int y = 0; y < height; ++y) {
      const auto* source_row = source_frame->GetReadPtr(plane) +
                               y * source_frame->GetPitch(plane);
      const auto* output_row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int x = 0; x < width; ++x) {
        EXPECT_EQ(output_row[x], source_row[x])
            << "plane=" << plane << " row=" << y << " column=" << x;
      }
    }
  }

  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ChromaLocation"),
            std::optional<int>{AVS_CHROMA_LEFT});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesInterlacedYv12TopLeftChromaToYv24) {
  AviSynthEnvironment environment;
  const auto source_vi = make_video_info(VideoInfoSpec{
      kYv12Width, kInterlacedYv12Height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation",
                         AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue point_resampler("point");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, true, AVS_CHROMA_TOP_LEFT,
                                point_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kInterlacedYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kInterlacedYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kInterlacedYv12Height);

  // Resize each field independently: source rows 0/2 feed the top field and
  // source rows 1/3 feed the bottom field before the fields are interleaved.
  constexpr int source_chroma_rows[kInterlacedYv12Height] = {0, 1, 0, 1, 2, 3, 2, 3};
  for (int y = 0; y < kInterlacedYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* source_u = source_frame->GetReadPtr(PLANAR_U) +
                           source_chroma_rows[y] * source_frame->GetPitch(PLANAR_U);
    const auto* source_v = source_frame->GetReadPtr(PLANAR_V) +
                           source_chroma_rows[y] * source_frame->GetPitch(PLANAR_V);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], source_u[x / 2]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], source_v[x / 2]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesInterlacedYv12ChromaWithBilinearTopLeftPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = make_video_info(VideoInfoSpec{
      kYv12Width, kInterlacedYv12Height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation",
                         AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue bilinear_resampler("bilinear");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, true, AVS_CHROMA_TOP_LEFT,
                                bilinear_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kInterlacedYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kInterlacedYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kInterlacedYv12Height);

  const auto expected_u = expected_interlaced_bilinear_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kInterlacedYv12Height / 2, kYv12Width,
      kInterlacedYv12Height, 0.25, 0.25, 0.0);
  const auto expected_v = expected_interlaced_bilinear_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kInterlacedYv12Height / 2, kYv12Width,
      kInterlacedYv12Height, 0.25, 0.25, 0.0);
  for (int y = 0; y < kInterlacedYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

TEST(ConvertToPlanarGeneric, UpsamplesInterlacedYv12ChromaWithLanczosTopLeftPlacement) {
  AviSynthEnvironment environment;
  const auto source_vi = make_video_info(VideoInfoSpec{
      kYv12Width, kInterlacedYv12Height, VideoInfo::CS_YV12, 1, 25, 1});
  PVideoFrame source_frame = environment.get()->NewVideoFrame(source_vi);
  fill_yuv8_source(source_frame);
  set_frame_property_int(environment.get(), source_frame, "_ChromaLocation",
                         AVS_CHROMA_TOP_LEFT);
  set_frame_property_int(environment.get(), source_frame, "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), source_frame, "_FieldBased", AVS_FIELD_TOP);
  const auto source_before = FrameSnapshot::capture(source_frame, source_vi);
  auto* source_clip_impl = new StaticFrameClip(source_vi, source_frame);
  const PClip source(source_clip_impl);

  const AVSValue lanczos_resampler("lanczos");
  const AVSValue no_parameter;
  ConvertToPlanarGeneric filter(source, VideoInfo::CS_YV24, true, AVS_CHROMA_TOP_LEFT,
                                lanczos_resampler, no_parameter, no_parameter, no_parameter,
                                AVS_CHROMA_UNUSED, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().pixel_type, VideoInfo::CS_YV24);
  ASSERT_EQ(filter.GetVideoInfo().width, kYv12Width);
  ASSERT_EQ(filter.GetVideoInfo().height, kInterlacedYv12Height);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);

  const PVideoFrame output = filter.GetFrame(0, environment.get());
  ASSERT_EQ(output->GetRowSize(PLANAR_Y), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_U), kYv12Width);
  ASSERT_EQ(output->GetRowSize(PLANAR_V), kYv12Width);
  ASSERT_EQ(output->GetHeight(PLANAR_U), kInterlacedYv12Height);
  ASSERT_EQ(output->GetHeight(PLANAR_V), kInterlacedYv12Height);

  const auto expected_u = expected_interlaced_resampled_chroma(
      source_frame, PLANAR_U, kYv12Width / 2, kInterlacedYv12Height / 2, kYv12Width,
      kInterlacedYv12Height, 0.25, 0.25, 0.0, lanczos_taps);
  const auto expected_v = expected_interlaced_resampled_chroma(
      source_frame, PLANAR_V, kYv12Width / 2, kInterlacedYv12Height / 2, kYv12Width,
      kInterlacedYv12Height, 0.25, 0.25, 0.0, lanczos_taps);
  for (int y = 0; y < kInterlacedYv12Height; ++y) {
    const auto* source_y =
        source_frame->GetReadPtr(PLANAR_Y) + y * source_frame->GetPitch(PLANAR_Y);
    const auto* output_y = output->GetReadPtr(PLANAR_Y) + y * output->GetPitch(PLANAR_Y);
    const auto* output_u = output->GetReadPtr(PLANAR_U) + y * output->GetPitch(PLANAR_U);
    const auto* output_v = output->GetReadPtr(PLANAR_V) + y * output->GetPitch(PLANAR_V);
    for (int x = 0; x < kYv12Width; ++x) {
      const auto index = static_cast<std::size_t>(y * kYv12Width + x);
      EXPECT_EQ(output_y[x], source_y[x]) << "plane=Y row=" << y << " column=" << x;
      EXPECT_EQ(output_u[x], expected_u[index]) << "plane=U row=" << y << " column=" << x;
      EXPECT_EQ(output_v[x], expected_v[index]) << "plane=V row=" << y << " column=" << x;
    }
  }

  EXPECT_FALSE(get_frame_property_int(environment.get(), output, "_ChromaLocation").has_value());
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_ColorRange"),
            std::optional<int>{AVS_COLORRANGE_FULL});
  EXPECT_EQ(get_frame_property_int(environment.get(), output, "_FieldBased"),
            std::optional<int>{AVS_FIELD_TOP});
  const std::vector<int> expected_requests{0, 0, 0, 0, 0};
  EXPECT_EQ(source_clip_impl->frame_requests(), expected_requests);
  EXPECT_NE(source_frame->CheckMemory(), 1);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(FrameSnapshot::capture(source_frame, source_vi), source_before);
}

}  // namespace
}  // namespace avsut::test
