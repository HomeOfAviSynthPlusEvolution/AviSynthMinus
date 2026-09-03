#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_OVERLAY_FILTER_UNDEF_AVS_UNUSED
#endif
#include "filters/overlay/overlay.h"
#ifdef AVSUT_OVERLAY_FILTER_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_OVERLAY_FILTER_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_helper.h"

#include "support/video_filter_test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using avsut::test::AviSynthEnvironment;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSequenceClip;
using avsut::test::FrameSnapshot;
using avsut::test::get_frame_property_int;
using avsut::test::make_video_info;
using avsut::test::set_frame_property_int;
using avsut::test::video_frame_planes;
using avsut::test::VideoInfoSpec;
using avsut::test::write_frame_plane;

struct OverlayFormatCase {
  int pixel_type;
  int width;
  int height;
  const char* mode;
  const char* name;
  bool use444 = true;
};

void PrintTo(const OverlayFormatCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

enum class OverlayArithmeticOperation { Add, Subtract };

struct OverlayArithmeticCase {
  int pixel_type;
  int width;
  int height;
  OverlayArithmeticOperation operation;
  float opacity;
  const char* name;
};

void PrintTo(const OverlayArithmeticCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

const char* overlay_arithmetic_mode(OverlayArithmeticOperation operation) {
  return operation == OverlayArithmeticOperation::Add ? "Add" : "Subtract";
}

std::vector<PVideoFrame> make_yuv_frames(AviSynthEnvironment& environment, const VideoInfo& vi,
                                         std::uint8_t base) {
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < vi.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : video_frame_planes(vi)) {
      fill_plane_full_pitch(frame, static_cast<std::uint8_t>(base + plane * 23 + frame_index * 17),
                            plane);
      write_frame_plane<std::uint8_t>(frame, plane, [base, plane, frame_index](int x, int y) {
        return static_cast<std::uint8_t>(base + plane * 23 + frame_index * 17 + x * 11 + y * 7);
      });
    }
    frames.push_back(frame);
  }
  return frames;
}

std::vector<PVideoFrame> make_full_mask_frames(AviSynthEnvironment& environment, int width,
                                               int height) {
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 2, 25, 1});
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < 2; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
      fill_plane_full_pitch(frame, 0xff, plane);
    }
    frames.push_back(frame);
  }
  return frames;
}

std::vector<FrameSnapshot> snapshot_frames(const std::vector<PVideoFrame>& frames,
                                           const VideoInfo& vi) {
  std::vector<FrameSnapshot> snapshots;
  for (const auto& frame : frames) {
    snapshots.push_back(FrameSnapshot::capture(frame, vi));
  }
  return snapshots;
}

std::array<AVSValue, 14> make_overlay_args(const PClip& base, const PClip& overlay,
                                           const PClip& mask, const char* mode) {
  std::array<AVSValue, 14> args{};
  args[0] = base;
  args[1] = overlay;
  args[2] = 0;
  args[3] = 0;
  if (mask) {
    args[4] = mask;
  }
  args[5] = 0.5f;
  args[6] = mode;
  args[7] = true;
  args[9] = true;
  args[10] = false;
  args[11] = true;
  args[12] = "";
  args[13] = "mpeg2";
  return args;
}

PVideoFrame render_overlay(const OverlayFormatCase& test_case, AviSynthEnvironment& environment,
                           const VideoInfo& vi, const PClip& base, const PClip& overlay,
                           const PClip& mask) {
  auto args = make_overlay_args(base, overlay, mask, test_case.mode);
  args[11] = test_case.use444;
  Overlay filter(base, AVSValue(args.data(), static_cast<int>(args.size())), environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, vi.pixel_type);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  return filter.GetFrame(1, environment.get());
}

void expect_active_planes_equal(const PVideoFrame& expected, const PVideoFrame& actual,
                                const VideoInfo& vi, const char* case_name) {
  for (const int plane : video_frame_planes(vi)) {
    ASSERT_EQ(expected->GetRowSize(plane), actual->GetRowSize(plane)) << "case=" << case_name;
    ASSERT_EQ(expected->GetHeight(plane), actual->GetHeight(plane)) << "case=" << case_name;
    for (int y = 0; y < expected->GetHeight(plane); ++y) {
      const auto* expected_row = expected->GetReadPtr(plane) + y * expected->GetPitch(plane);
      const auto* actual_row = actual->GetReadPtr(plane) + y * actual->GetPitch(plane);
      for (int x = 0; x < expected->GetRowSize(plane); ++x) {
        EXPECT_EQ(actual_row[x], expected_row[x])
            << "case=" << case_name << " plane=" << plane << " x=" << x << " y=" << y;
      }
    }
  }
}

std::uint8_t clamp_u8(int value) { return static_cast<std::uint8_t>(std::clamp(value, 0, 255)); }

std::vector<PVideoFrame> make_yuv_arithmetic_frames(AviSynthEnvironment& environment,
                                                    const VideoInfo& vi, bool overlay) {
  constexpr std::array<std::uint8_t, 7> kBaseY{250, 240, 128, 7, 0, 64, 255};
  constexpr std::array<std::uint8_t, 7> kOverlayY{20, 40, 100, 32, 1, 200, 10};
  constexpr std::array<std::uint8_t, 7> kBaseU{0, 32, 96, 128, 160, 224, 255};
  constexpr std::array<std::uint8_t, 7> kOverlayU{255, 224, 160, 128, 96, 32, 0};
  constexpr std::array<std::uint8_t, 7> kBaseV{255, 224, 160, 128, 96, 32, 0};
  constexpr std::array<std::uint8_t, 7> kOverlayV{0, 32, 96, 128, 160, 224, 255};
  if (vi.pixel_type != VideoInfo::CS_YV24 || vi.width != 7 || vi.height != 3) {
    throw std::invalid_argument("unexpected YUV arithmetic test geometry");
  }

  const auto& y_values = overlay ? kOverlayY : kBaseY;
  const auto& u_values = overlay ? kOverlayU : kBaseU;
  const auto& v_values = overlay ? kOverlayV : kBaseV;
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < vi.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
      fill_plane_full_pitch(frame, overlay ? 0x6d : 0x93, plane);
      write_frame_plane<std::uint8_t>(frame, plane, [&](int x, int y) {
        const auto& values = plane == PLANAR_Y ? y_values : plane == PLANAR_U ? u_values : v_values;
        const int row_offset = y * (overlay ? 3 : 2) + frame_index * (overlay ? 5 : 3);
        return clamp_u8(static_cast<int>(values[static_cast<std::size_t>(x)]) + row_offset);
      });
    }
    frames.push_back(frame);
  }
  return frames;
}

std::vector<PVideoFrame> make_rgbp16_arithmetic_frames(AviSynthEnvironment& environment,
                                                       const VideoInfo& vi, bool overlay) {
  constexpr std::array<std::uint16_t, 5> kBase{0, 1000, 30000, 60000, 65535};
  constexpr std::array<std::uint16_t, 5> kOverlay{65535, 40000, 30000, 10000, 1000};
  if (vi.pixel_type != VideoInfo::CS_RGBP16 || vi.width != 5 || vi.height != 3) {
    throw std::invalid_argument("unexpected planar RGB arithmetic test geometry");
  }

  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < vi.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
      fill_plane_full_pitch(frame, overlay ? 0x4c : 0xa7, plane);
      write_frame_plane<std::uint16_t>(frame, plane, [&](int x, int y) {
        const int plane_offset = (plane - PLANAR_G) * (overlay ? 700 : 900);
        const int row_offset = y * (overlay ? 250 : 350) + frame_index * (overlay ? 500 : 650);
        const auto& values = overlay ? kOverlay : kBase;
        return static_cast<std::uint16_t>(std::clamp(
            static_cast<int>(values[static_cast<std::size_t>(x)]) + plane_offset + row_offset, 0,
            65535));
      });
    }
    frames.push_back(frame);
  }
  return frames;
}

int reference_yuv_arithmetic_value(int base, int overlay, bool add, float opacity) {
  const bool full_opacity = opacity == 1.0F;
  const int opacity_i = full_opacity ? 255 : static_cast<int>(opacity * 255.0F + 0.5F);
  const int weighted_y = full_opacity ? overlay : (overlay * opacity_i + 127) / 255;
  return add ? base + weighted_y : base - weighted_y;
}

int reference_yuv_chroma(int base, int overlay, bool add, float opacity, int y_value) {
  constexpr int kHalfPixel = 128;
  constexpr int kPixelRange = 256;
  constexpr int kOver32 = 32;
  constexpr int kShift = 5;
  const bool full_opacity = opacity == 1.0F;
  const int opacity_i = full_opacity ? 255 : static_cast<int>(opacity * 255.0F + 0.5F);
  const int d_chroma = overlay - kHalfPixel;
  const int t_chroma =
      full_opacity ? d_chroma
                   : (d_chroma >= 0 ? (d_chroma * opacity_i + 127) / 255
                                    : -((-d_chroma * opacity_i + 127) / 255));
  int value = add ? base + t_chroma : base - t_chroma;
  if (add && y_value > 255) {
    const int multiplier = std::max(0, kPixelRange + kOver32 - y_value);
    value = ((value * multiplier) + (kHalfPixel * (kOver32 - multiplier))) >> kShift;
  } else if (!add && y_value < 0) {
    const int multiplier = std::min(-y_value, kOver32);
    value = ((value * (kOver32 - multiplier)) + (kHalfPixel * multiplier)) >> kShift;
  }
  return value;
}

void expect_yuv_arithmetic_reference(const OverlayArithmeticCase& test_case,
                                     const PVideoFrame& base, const PVideoFrame& overlay,
                                     const PVideoFrame& output) {
  const bool add = test_case.operation == OverlayArithmeticOperation::Add;
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    ASSERT_EQ(output->GetRowSize(plane), base->GetRowSize(plane)) << "plane=" << plane;
    ASSERT_EQ(output->GetHeight(plane), base->GetHeight(plane)) << "plane=" << plane;
    const int width = base->GetRowSize(plane);
    for (int y = 0; y < base->GetHeight(plane); ++y) {
      const auto* base_row = base->GetReadPtr(plane) + y * base->GetPitch(plane);
      const auto* overlay_row = overlay->GetReadPtr(plane) + y * overlay->GetPitch(plane);
      const auto* output_row = output->GetReadPtr(plane) + y * output->GetPitch(plane);
      for (int x = 0; x < width; ++x) {
        const int y_value = reference_yuv_arithmetic_value(
            base->GetReadPtr(PLANAR_Y)[y * base->GetPitch(PLANAR_Y) + x],
            overlay->GetReadPtr(PLANAR_Y)[y * overlay->GetPitch(PLANAR_Y) + x], add,
            test_case.opacity);
        const int expected = plane == PLANAR_Y
                                 ? y_value
                                 : reference_yuv_chroma(base_row[x], overlay_row[x], add,
                                                        test_case.opacity, y_value);
        EXPECT_EQ(output_row[x], clamp_u8(expected))
            << "case=" << test_case.name << " plane=" << plane << " x=" << x << " y=" << y
            << " base_pitch=" << base->GetPitch(plane)
            << " overlay_pitch=" << overlay->GetPitch(plane);
      }
    }
  }
}

std::uint16_t reference_rgbp16_value(std::uint16_t base, std::uint16_t overlay, bool add,
                                     float opacity) {
  const float result = add ? static_cast<float>(base) + static_cast<float>(overlay) * opacity
                           : static_cast<float>(base) - static_cast<float>(overlay) * opacity;
  return static_cast<std::uint16_t>(std::clamp(static_cast<int>(result + 0.5F), 0, 65535));
}

void expect_rgbp16_arithmetic_reference(const OverlayArithmeticCase& test_case,
                                        const PVideoFrame& base, const PVideoFrame& overlay,
                                        const PVideoFrame& output) {
  const bool add = test_case.operation == OverlayArithmeticOperation::Add;
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    const int width = base->GetRowSize(plane) / static_cast<int>(sizeof(std::uint16_t));
    ASSERT_EQ(output->GetRowSize(plane), base->GetRowSize(plane)) << "plane=" << plane;
    ASSERT_EQ(output->GetHeight(plane), base->GetHeight(plane)) << "plane=" << plane;
    for (int y = 0; y < base->GetHeight(plane); ++y) {
      const auto* base_row = reinterpret_cast<const std::uint16_t*>(base->GetReadPtr(plane) +
                                                                    y * base->GetPitch(plane));
      const auto* overlay_row = reinterpret_cast<const std::uint16_t*>(
          overlay->GetReadPtr(plane) + y * overlay->GetPitch(plane));
      const auto* output_row = reinterpret_cast<const std::uint16_t*>(output->GetReadPtr(plane) +
                                                                      y * output->GetPitch(plane));
      for (int x = 0; x < width; ++x) {
        EXPECT_EQ(output_row[x],
                  reference_rgbp16_value(base_row[x], overlay_row[x], add, test_case.opacity))
            << "case=" << test_case.name << " plane=" << plane << " x=" << x << " y=" << y
            << " base_pitch=" << base->GetPitch(plane)
            << " overlay_pitch=" << overlay->GetPitch(plane);
      }
    }
  }
}

std::vector<PVideoFrame> make_yuv_float_arithmetic_frames(AviSynthEnvironment& environment,
                                                          const VideoInfo& vi, bool overlay) {
  constexpr std::array<float, 7> kBaseY{0.90F, 0.75F, 0.40F, 0.05F, 0.0F, 0.20F, 1.0F};
  constexpr std::array<float, 7> kOverlayY{0.20F, 0.40F, 0.80F, 0.20F, 0.10F, 0.90F, 0.30F};
  constexpr std::array<float, 7> kBaseU{-0.5F, -0.25F, -0.1F, 0.0F, 0.1F, 0.25F, 0.5F};
  constexpr std::array<float, 7> kOverlayU{0.5F, 0.25F, 0.1F, 0.0F, -0.1F, -0.25F, -0.5F};
  constexpr std::array<float, 7> kBaseV{0.5F, 0.25F, 0.1F, 0.0F, -0.1F, -0.25F, -0.5F};
  constexpr std::array<float, 7> kOverlayV{-0.5F, -0.1F, 0.05F, 0.15F, 0.25F, 0.35F, 0.5F};
  if (vi.pixel_type != VideoInfo::CS_YUV444PS || vi.width != 7 || vi.height != 3) {
    throw std::invalid_argument("unexpected float YUV arithmetic test geometry");
  }

  const auto& y_values = overlay ? kOverlayY : kBaseY;
  const auto& u_values = overlay ? kOverlayU : kBaseU;
  const auto& v_values = overlay ? kOverlayV : kBaseV;
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < vi.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
      fill_plane_full_pitch(frame, overlay ? 0x5b : 0xa4, plane);
      write_frame_plane<float>(frame, plane, [&](int x, int y) {
        const auto& values = plane == PLANAR_Y ? y_values : plane == PLANAR_U ? u_values : v_values;
        const float row_offset = static_cast<float>(y * 2 + frame_index * 3) * 0.001F;
        return plane == PLANAR_Y
                   ? std::clamp(values[static_cast<std::size_t>(x)] + row_offset, 0.0F, 1.0F)
                   : values[static_cast<std::size_t>(x)] + row_offset;
      });
    }
    frames.push_back(frame);
  }
  return frames;
}

std::vector<PVideoFrame> make_rgbps_arithmetic_frames(AviSynthEnvironment& environment,
                                                      const VideoInfo& vi, bool overlay) {
  constexpr std::array<float, 5> kBase{0.0F, 0.1F, 0.45F, 0.8F, 1.0F};
  constexpr std::array<float, 5> kOverlay{1.0F, 0.75F, 0.4F, 0.2F, 0.05F};
  if (vi.pixel_type != VideoInfo::CS_RGBPS || vi.width != 5 || vi.height != 3) {
    throw std::invalid_argument("unexpected float planar RGB arithmetic test geometry");
  }

  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < vi.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
      fill_plane_full_pitch(frame, overlay ? 0x47 : 0xb8, plane);
      write_frame_plane<float>(frame, plane, [&](int x, int y) {
        const auto& values = overlay ? kOverlay : kBase;
        const float plane_offset = static_cast<float>((plane - PLANAR_G) * 3) * 0.01F;
        const float row_offset = static_cast<float>(y * 2 + frame_index * 3) * 0.001F;
        return std::clamp(values[static_cast<std::size_t>(x)] + plane_offset + row_offset, 0.0F,
                          1.0F);
      });
    }
    frames.push_back(frame);
  }
  return frames;
}

float reference_yuv_float_luma(float base, float overlay, bool add, float opacity) {
  return add ? base + opacity * overlay : base - opacity * overlay;
}

float reference_yuv_float_chroma(float base, float overlay, bool add, float opacity,
                                 float y_value) {
  constexpr float kOver32 = 32.0F / 255.0F;
  float value = add ? base + opacity * overlay : base - opacity * overlay;
  if (add && y_value > 1.0F) {
    const float multiplier = std::max(0.0F, 1.0F + kOver32 - y_value);
    value = value * multiplier / kOver32;
  } else if (!add && y_value < 0.0F) {
    const float multiplier = std::min(-y_value, kOver32);
    value = value * (kOver32 - multiplier) / kOver32;
  }
  return value;
}

void expect_yuv_float_arithmetic_reference(const OverlayArithmeticCase& test_case,
                                           const PVideoFrame& base, const PVideoFrame& overlay,
                                           const PVideoFrame& output) {
  const bool add = test_case.operation == OverlayArithmeticOperation::Add;
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    const int width = base->GetRowSize(plane) / static_cast<int>(sizeof(float));
    ASSERT_EQ(output->GetRowSize(plane), base->GetRowSize(plane)) << "plane=" << plane;
    ASSERT_EQ(output->GetHeight(plane), base->GetHeight(plane)) << "plane=" << plane;
    for (int y = 0; y < base->GetHeight(plane); ++y) {
      const auto* base_row =
          reinterpret_cast<const float*>(base->GetReadPtr(plane) + y * base->GetPitch(plane));
      const auto* overlay_row =
          reinterpret_cast<const float*>(overlay->GetReadPtr(plane) + y * overlay->GetPitch(plane));
      const auto* base_y_row =
          reinterpret_cast<const float*>(base->GetReadPtr(PLANAR_Y) + y * base->GetPitch(PLANAR_Y));
      const auto* overlay_y_row = reinterpret_cast<const float*>(overlay->GetReadPtr(PLANAR_Y) +
                                                                 y * overlay->GetPitch(PLANAR_Y));
      const auto* output_row =
          reinterpret_cast<const float*>(output->GetReadPtr(plane) + y * output->GetPitch(plane));
      for (int x = 0; x < width; ++x) {
        const float y_value =
            reference_yuv_float_luma(base_y_row[x], overlay_y_row[x], add, test_case.opacity);
        const float expected = plane == PLANAR_Y
                                   ? std::clamp(y_value, 0.0F, 1.0F)
                                   : reference_yuv_float_chroma(base_row[x], overlay_row[x], add,
                                                                test_case.opacity, y_value);
        ASSERT_TRUE(std::isfinite(output_row[x]))
            << "case=" << test_case.name << " plane=" << plane << " x=" << x << " y=" << y;
        EXPECT_NEAR(output_row[x], expected, 1.0e-6F)
            << "case=" << test_case.name << " plane=" << plane << " x=" << x << " y=" << y
            << " base_pitch=" << base->GetPitch(plane)
            << " overlay_pitch=" << overlay->GetPitch(plane);
      }
    }
  }
}

void expect_rgbps_arithmetic_reference(const OverlayArithmeticCase& test_case,
                                       const PVideoFrame& base, const PVideoFrame& overlay,
                                       const PVideoFrame& output) {
  const bool add = test_case.operation == OverlayArithmeticOperation::Add;
  for (const int plane : {PLANAR_G, PLANAR_B, PLANAR_R}) {
    const int width = base->GetRowSize(plane) / static_cast<int>(sizeof(float));
    ASSERT_EQ(output->GetRowSize(plane), base->GetRowSize(plane)) << "plane=" << plane;
    ASSERT_EQ(output->GetHeight(plane), base->GetHeight(plane)) << "plane=" << plane;
    for (int y = 0; y < base->GetHeight(plane); ++y) {
      const auto* base_row =
          reinterpret_cast<const float*>(base->GetReadPtr(plane) + y * base->GetPitch(plane));
      const auto* overlay_row =
          reinterpret_cast<const float*>(overlay->GetReadPtr(plane) + y * overlay->GetPitch(plane));
      const auto* output_row =
          reinterpret_cast<const float*>(output->GetReadPtr(plane) + y * output->GetPitch(plane));
      for (int x = 0; x < width; ++x) {
        const float expected = add ? base_row[x] + overlay_row[x] * test_case.opacity
                                   : base_row[x] - overlay_row[x] * test_case.opacity;
        ASSERT_TRUE(std::isfinite(output_row[x]))
            << "case=" << test_case.name << " plane=" << plane << " x=" << x << " y=" << y;
        EXPECT_NEAR(output_row[x], expected, 1.0e-6F)
            << "case=" << test_case.name << " plane=" << plane << " x=" << x << " y=" << y
            << " base_pitch=" << base->GetPitch(plane)
            << " overlay_pitch=" << overlay->GetPitch(plane);
      }
    }
  }
}

class OverlayFilterFormatTest : public ::testing::TestWithParam<OverlayFormatCase> {};

TEST_P(OverlayFilterFormatTest, FullScaleMaskMatchesOmittedMaskAcrossYuvFormats) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  const auto vi = make_video_info(
      VideoInfoSpec{test_case.width, test_case.height, test_case.pixel_type, 2, 25, 1});

  auto unmasked_base_frames = make_yuv_frames(environment, vi, 29);
  auto unmasked_overlay_frames = make_yuv_frames(environment, vi, 151);
  if (vi.IsYUVA()) {
    for (auto& frame : unmasked_overlay_frames) {
      write_frame_plane<std::uint8_t>(frame, PLANAR_A, [](int, int) { return 255; });
    }
  }
  const auto unmasked_base_before = snapshot_frames(unmasked_base_frames, vi);
  const auto unmasked_overlay_before = snapshot_frames(unmasked_overlay_frames, vi);
  auto* unmasked_base_impl = new FrameSequenceClip(vi, unmasked_base_frames);
  auto* unmasked_overlay_impl = new FrameSequenceClip(vi, unmasked_overlay_frames);
  const PClip unmasked_base(unmasked_base_impl);
  const PClip unmasked_overlay(unmasked_overlay_impl);
  const PVideoFrame unmasked =
      render_overlay(test_case, environment, vi, unmasked_base, unmasked_overlay, PClip());

  auto masked_base_frames = make_yuv_frames(environment, vi, 29);
  auto masked_overlay_frames = make_yuv_frames(environment, vi, 151);
  if (vi.IsYUVA()) {
    for (auto& frame : masked_overlay_frames) {
      write_frame_plane<std::uint8_t>(frame, PLANAR_A, [](int, int) { return 255; });
    }
  }
  const auto full_mask_frames =
      make_full_mask_frames(environment, test_case.width, test_case.height);
  const auto masked_base_before = snapshot_frames(masked_base_frames, vi);
  const auto masked_overlay_before = snapshot_frames(masked_overlay_frames, vi);
  const auto full_mask_vi = make_video_info(
      VideoInfoSpec{test_case.width, test_case.height, VideoInfo::CS_YV24, 2, 25, 1});
  const auto full_mask_before = snapshot_frames(full_mask_frames, full_mask_vi);
  auto* masked_base_impl = new FrameSequenceClip(vi, masked_base_frames);
  auto* masked_overlay_impl = new FrameSequenceClip(vi, masked_overlay_frames);
  auto* full_mask_impl = new FrameSequenceClip(full_mask_vi, full_mask_frames);
  const PClip masked_base(masked_base_impl);
  const PClip masked_overlay(masked_overlay_impl);
  const PClip full_mask(full_mask_impl);
  const PVideoFrame masked =
      render_overlay(test_case, environment, vi, masked_base, masked_overlay, full_mask);

  expect_active_planes_equal(unmasked, masked, vi, test_case.name);
  EXPECT_NE(unmasked->CheckMemory(), 1);
  EXPECT_NE(masked->CheckMemory(), 1);
  EXPECT_EQ(unmasked_base_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(unmasked_overlay_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(masked_base_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(masked_overlay_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(full_mask_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(snapshot_frames(unmasked_base_frames, vi), unmasked_base_before);
  EXPECT_EQ(snapshot_frames(unmasked_overlay_frames, vi), unmasked_overlay_before);
  EXPECT_EQ(snapshot_frames(masked_base_frames, vi), masked_base_before);
  EXPECT_EQ(snapshot_frames(masked_overlay_frames, vi), masked_overlay_before);
  EXPECT_EQ(snapshot_frames(full_mask_frames, full_mask_vi), full_mask_before);
}

INSTANTIATE_TEST_SUITE_P(
    FormatCases, OverlayFilterFormatTest,
    ::testing::Values(
        OverlayFormatCase{VideoInfo::CS_YV12, 8, 4, "Blend", "Yv12_Width8_Height4_Blend_Use444"},
        OverlayFormatCase{VideoInfo::CS_YV16, 8, 5, "Blend", "Yv16_Width8_Height5_Blend_Use444"},
        OverlayFormatCase{VideoInfo::CS_YUVA420, 8, 4, "Blend",
                          "Yuva420_Width8_Height4_Blend_Use444"},
        OverlayFormatCase{VideoInfo::CS_YV12, 8, 4, "Blend", "Yv12_Width8_Height4_Blend_UseNative",
                          false},
        OverlayFormatCase{VideoInfo::CS_YV16, 8, 5, "Blend", "Yv16_Width8_Height5_Blend_UseNative",
                          false},
        OverlayFormatCase{VideoInfo::CS_YUVA420, 8, 4, "Blend",
                          "Yuva420_Width8_Height4_Blend_UseNative", false}),
    [](const ::testing::TestParamInfo<OverlayFormatCase>& info) { return info.param.name; });

TEST(OverlayFilter, FullOpacityBlendReturnsOverlayFrame) {
  AviSynthEnvironment environment;
  constexpr int width = 7;
  constexpr int height = 3;
  const auto vi = make_video_info(VideoInfoSpec{width, height, VideoInfo::CS_YV24, 2, 25, 1});
  auto base_frames = make_yuv_frames(environment, vi, 23);
  auto overlay_frames = make_yuv_frames(environment, vi, 149);
  const auto base_before = snapshot_frames(base_frames, vi);
  const auto overlay_before = snapshot_frames(overlay_frames, vi);
  auto* base_impl = new FrameSequenceClip(vi, base_frames);
  auto* overlay_impl = new FrameSequenceClip(vi, overlay_frames);
  const PClip base(base_impl);
  const PClip overlay(overlay_impl);
  auto args = make_overlay_args(base, overlay, PClip(), "Blend");
  args[5] = 1.0f;

  Overlay filter(base, AVSValue(args.data(), static_cast<int>(args.size())), environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, vi.pixel_type);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(1, environment.get());

  expect_active_planes_equal(overlay_frames[1], output, vi, "FullOpacityBlend");
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(base_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(overlay_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(snapshot_frames(base_frames, vi), base_before);
  EXPECT_EQ(snapshot_frames(overlay_frames, vi), overlay_before);
}

TEST(OverlayFilter, UsesBaseFramePropertiesForBlendOutput) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{7, 3, VideoInfo::CS_YV24, 2, 25, 1});
  auto base_frames = make_yuv_frames(environment, vi, 23);
  auto overlay_frames = make_yuv_frames(environment, vi, 149);
  set_frame_property_int(environment.get(), base_frames[1], "_ChromaLocation", AVS_CHROMA_LEFT);
  set_frame_property_int(environment.get(), base_frames[1], "_ColorRange", AVS_COLORRANGE_FULL);
  set_frame_property_int(environment.get(), base_frames[1], "_FieldBased", 1);
  set_frame_property_int(environment.get(), overlay_frames[1], "_ChromaLocation",
                         AVS_CHROMA_CENTER);
  set_frame_property_int(environment.get(), overlay_frames[1], "_ColorRange",
                         AVS_COLORRANGE_LIMITED);
  set_frame_property_int(environment.get(), overlay_frames[1], "_FieldBased", 0);
  const auto base_before = snapshot_frames(base_frames, vi);
  const auto overlay_before = snapshot_frames(overlay_frames, vi);
  auto* base_impl = new FrameSequenceClip(vi, base_frames);
  auto* overlay_impl = new FrameSequenceClip(vi, overlay_frames);
  const PClip base(base_impl);
  const PClip overlay(overlay_impl);
  auto args = make_overlay_args(base, overlay, PClip(), "Blend");

  Overlay filter(base, AVSValue(args.data(), static_cast<int>(args.size())), environment.get());
  const PVideoFrame output = filter.GetFrame(1, environment.get());

  for (const auto& property : std::array<std::pair<const char*, int>, 3>{
           std::pair{"_ChromaLocation", AVS_CHROMA_LEFT},
           std::pair{"_ColorRange", AVS_COLORRANGE_FULL}, std::pair{"_FieldBased", 1}}) {
    const auto actual = get_frame_property_int(environment.get(), output, property.first);
    ASSERT_TRUE(actual.has_value()) << property.first;
    EXPECT_EQ(*actual, property.second) << property.first;
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(base_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(overlay_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(snapshot_frames(base_frames, vi), base_before);
  EXPECT_EQ(snapshot_frames(overlay_frames, vi), overlay_before);
}

class OverlayFilterArithmeticTest : public ::testing::TestWithParam<OverlayArithmeticCase> {};

TEST_P(OverlayFilterArithmeticTest, MatchesIndependentArithmeticReference) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  const auto vi = make_video_info(
      VideoInfoSpec{test_case.width, test_case.height, test_case.pixel_type, 2, 25, 1});

  const bool planar_rgb = vi.IsPlanarRGB();
  std::vector<PVideoFrame> base_frames;
  std::vector<PVideoFrame> overlay_frames;
  if (test_case.pixel_type == VideoInfo::CS_YV24) {
    base_frames = make_yuv_arithmetic_frames(environment, vi, false);
    overlay_frames = make_yuv_arithmetic_frames(environment, vi, true);
  } else if (test_case.pixel_type == VideoInfo::CS_YUV444PS) {
    base_frames = make_yuv_float_arithmetic_frames(environment, vi, false);
    overlay_frames = make_yuv_float_arithmetic_frames(environment, vi, true);
  } else if (test_case.pixel_type == VideoInfo::CS_RGBP16) {
    base_frames = make_rgbp16_arithmetic_frames(environment, vi, false);
    overlay_frames = make_rgbp16_arithmetic_frames(environment, vi, true);
  } else {
    base_frames = make_rgbps_arithmetic_frames(environment, vi, false);
    overlay_frames = make_rgbps_arithmetic_frames(environment, vi, true);
  }
  const auto base_before = FrameSnapshot::capture(base_frames[1], vi);
  const auto overlay_before = FrameSnapshot::capture(overlay_frames[1], vi);
  auto* base_impl = new FrameSequenceClip(vi, base_frames);
  auto* overlay_impl = new FrameSequenceClip(vi, overlay_frames);
  const PClip base(base_impl);
  const PClip overlay(overlay_impl);

  auto args =
      make_overlay_args(base, overlay, PClip(), overlay_arithmetic_mode(test_case.operation));
  args[5] = test_case.opacity;
  if (planar_rgb) {
    args[11] = false;
  }
  Overlay filter(base, AVSValue(args.data(), static_cast<int>(args.size())), environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, vi.pixel_type);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(1, environment.get());

  if (planar_rgb) {
    if (test_case.pixel_type == VideoInfo::CS_RGBP16) {
      expect_rgbp16_arithmetic_reference(test_case, base_frames[1], overlay_frames[1], output);
    } else {
      expect_rgbps_arithmetic_reference(test_case, base_frames[1], overlay_frames[1], output);
    }
  } else if (test_case.pixel_type == VideoInfo::CS_YUV444PS) {
    expect_yuv_float_arithmetic_reference(test_case, base_frames[1], overlay_frames[1], output);
  } else {
    expect_yuv_arithmetic_reference(test_case, base_frames[1], overlay_frames[1], output);
  }
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(base_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(overlay_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(FrameSnapshot::capture(base_frames[1], vi), base_before);
  EXPECT_EQ(FrameSnapshot::capture(overlay_frames[1], vi), overlay_before);
}

INSTANTIATE_TEST_SUITE_P(
    ArithmeticCases, OverlayFilterArithmeticTest,
    ::testing::Values(
        OverlayArithmeticCase{VideoInfo::CS_YV24, 7, 3, OverlayArithmeticOperation::Add, 1.0F,
                              "Yv24_Width7_Height3_Add_OpacityFull"},
        OverlayArithmeticCase{VideoInfo::CS_YV24, 7, 3, OverlayArithmeticOperation::Add, 0.5F,
                              "Yv24_Width7_Height3_Add_OpacityHalf"},
        OverlayArithmeticCase{VideoInfo::CS_YV24, 7, 3, OverlayArithmeticOperation::Subtract, 1.0F,
                              "Yv24_Width7_Height3_Subtract_OpacityFull"},
        OverlayArithmeticCase{VideoInfo::CS_YV24, 7, 3, OverlayArithmeticOperation::Subtract, 0.5F,
                              "Yv24_Width7_Height3_Subtract_OpacityHalf"},
        OverlayArithmeticCase{VideoInfo::CS_RGBP16, 5, 3, OverlayArithmeticOperation::Add, 1.0F,
                              "Rgbp16_Width5_Height3_Add_OpacityFull"},
        OverlayArithmeticCase{VideoInfo::CS_RGBP16, 5, 3, OverlayArithmeticOperation::Add, 0.5F,
                              "Rgbp16_Width5_Height3_Add_OpacityHalf"},
        OverlayArithmeticCase{VideoInfo::CS_RGBP16, 5, 3, OverlayArithmeticOperation::Subtract,
                              1.0F, "Rgbp16_Width5_Height3_Subtract_OpacityFull"},
        OverlayArithmeticCase{VideoInfo::CS_RGBP16, 5, 3, OverlayArithmeticOperation::Subtract,
                              0.5F, "Rgbp16_Width5_Height3_Subtract_OpacityHalf"},
        OverlayArithmeticCase{VideoInfo::CS_YUV444PS, 7, 3, OverlayArithmeticOperation::Add, 1.0F,
                              "Yuv444ps_Width7_Height3_Add_OpacityFull"},
        OverlayArithmeticCase{VideoInfo::CS_YUV444PS, 7, 3, OverlayArithmeticOperation::Add, 0.5F,
                              "Yuv444ps_Width7_Height3_Add_OpacityHalf"},
        OverlayArithmeticCase{VideoInfo::CS_YUV444PS, 7, 3, OverlayArithmeticOperation::Subtract,
                              1.0F, "Yuv444ps_Width7_Height3_Subtract_OpacityFull"},
        OverlayArithmeticCase{VideoInfo::CS_YUV444PS, 7, 3, OverlayArithmeticOperation::Subtract,
                              0.5F, "Yuv444ps_Width7_Height3_Subtract_OpacityHalf"},
        OverlayArithmeticCase{VideoInfo::CS_RGBPS, 5, 3, OverlayArithmeticOperation::Add, 1.0F,
                              "Rgbps_Width5_Height3_Add_OpacityFull"},
        OverlayArithmeticCase{VideoInfo::CS_RGBPS, 5, 3, OverlayArithmeticOperation::Add, 0.5F,
                              "Rgbps_Width5_Height3_Add_OpacityHalf"},
        OverlayArithmeticCase{VideoInfo::CS_RGBPS, 5, 3, OverlayArithmeticOperation::Subtract, 1.0F,
                              "Rgbps_Width5_Height3_Subtract_OpacityFull"},
        OverlayArithmeticCase{VideoInfo::CS_RGBPS, 5, 3, OverlayArithmeticOperation::Subtract, 0.5F,
                              "Rgbps_Width5_Height3_Subtract_OpacityHalf"}),
    [](const ::testing::TestParamInfo<OverlayArithmeticCase>& info) { return info.param.name; });

enum class OverlayThresholdOperation { Lighten, Darken };

struct OverlayThresholdCase {
  OverlayThresholdOperation operation;
  float opacity;
  const char* name;
};

void PrintTo(const OverlayThresholdCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

const char* overlay_threshold_mode(OverlayThresholdOperation operation) {
  return operation == OverlayThresholdOperation::Lighten ? "Lighten" : "Darken";
}

std::vector<PVideoFrame> make_yuv_threshold_frames(AviSynthEnvironment& environment,
                                                   const VideoInfo& vi, bool overlay) {
  // Distinct Y pairs cover darker, lighter, and equal luma comparisons. Chroma
  // only changes when the Y threshold selects the overlay.
  constexpr std::array<std::uint8_t, 5> kBaseY{10, 200, 128, 40, 180};
  constexpr std::array<std::uint8_t, 5> kOverlayY{200, 10, 128, 90, 40};
  constexpr std::array<std::uint8_t, 5> kBaseU{16, 80, 128, 160, 200};
  constexpr std::array<std::uint8_t, 5> kOverlayU{240, 200, 128, 96, 32};
  constexpr std::array<std::uint8_t, 5> kBaseV{240, 200, 128, 96, 32};
  constexpr std::array<std::uint8_t, 5> kOverlayV{16, 80, 128, 160, 200};
  if (vi.pixel_type != VideoInfo::CS_YV24 || vi.width != 5 || vi.height != 3) {
    throw std::invalid_argument("unexpected YUV threshold test geometry");
  }

  const auto& y_values = overlay ? kOverlayY : kBaseY;
  const auto& u_values = overlay ? kOverlayU : kBaseU;
  const auto& v_values = overlay ? kOverlayV : kBaseV;
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < vi.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
      fill_plane_full_pitch(frame, overlay ? 0x55 : 0xaa, plane);
      write_frame_plane<std::uint8_t>(frame, plane, [&](int x, int y) {
        const auto& values = plane == PLANAR_Y ? y_values : plane == PLANAR_U ? u_values : v_values;
        const int row_offset = y + frame_index;
        return clamp_u8(static_cast<int>(values[static_cast<std::size_t>(x)]) + row_offset);
      });
    }
    frames.push_back(frame);
  }
  return frames;
}

std::uint8_t reference_threshold_channel(std::uint8_t base, std::uint8_t overlay, bool select,
                                         float opacity) {
  if (!select) {
    return base;
  }
  if (opacity == 1.0F) {
    return overlay;
  }
  const int opacity_i = static_cast<int>(opacity * 255.0F + 0.5F);
  return static_cast<std::uint8_t>(((255 - opacity_i) * base + opacity_i * overlay + 127) / 255);
}

void expect_yuv_threshold_reference(const OverlayThresholdCase& test_case,
                                    const PVideoFrame& base_frame, const PVideoFrame& overlay_frame,
                                    const PVideoFrame& output) {
  const int y_pitch = output->GetPitch(PLANAR_Y);
  const int u_pitch = output->GetPitch(PLANAR_U);
  const int v_pitch = output->GetPitch(PLANAR_V);
  const int base_y_pitch = base_frame->GetPitch(PLANAR_Y);
  const int base_u_pitch = base_frame->GetPitch(PLANAR_U);
  const int base_v_pitch = base_frame->GetPitch(PLANAR_V);
  const int ov_y_pitch = overlay_frame->GetPitch(PLANAR_Y);
  const int ov_u_pitch = overlay_frame->GetPitch(PLANAR_U);
  const int ov_v_pitch = overlay_frame->GetPitch(PLANAR_V);
  const auto* out_y = output->GetReadPtr(PLANAR_Y);
  const auto* out_u = output->GetReadPtr(PLANAR_U);
  const auto* out_v = output->GetReadPtr(PLANAR_V);
  const auto* base_y = base_frame->GetReadPtr(PLANAR_Y);
  const auto* base_u = base_frame->GetReadPtr(PLANAR_U);
  const auto* base_v = base_frame->GetReadPtr(PLANAR_V);
  const auto* ov_y = overlay_frame->GetReadPtr(PLANAR_Y);
  const auto* ov_u = overlay_frame->GetReadPtr(PLANAR_U);
  const auto* ov_v = overlay_frame->GetReadPtr(PLANAR_V);

  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 5; ++x) {
      const std::uint8_t by = base_y[x + y * base_y_pitch];
      const std::uint8_t oy = ov_y[x + y * ov_y_pitch];
      const bool select =
          test_case.operation == OverlayThresholdOperation::Lighten ? oy > by : oy < by;
      const auto expected_y = reference_threshold_channel(by, oy, select, test_case.opacity);
      const auto expected_u = reference_threshold_channel(
          base_u[x + y * base_u_pitch], ov_u[x + y * ov_u_pitch], select, test_case.opacity);
      const auto expected_v = reference_threshold_channel(
          base_v[x + y * base_v_pitch], ov_v[x + y * ov_v_pitch], select, test_case.opacity);
      EXPECT_EQ(out_y[x + y * y_pitch], expected_y)
          << "mode=" << test_case.name << " x=" << x << " y=" << y << " plane=Y";
      EXPECT_EQ(out_u[x + y * u_pitch], expected_u)
          << "mode=" << test_case.name << " x=" << x << " y=" << y << " plane=U";
      EXPECT_EQ(out_v[x + y * v_pitch], expected_v)
          << "mode=" << test_case.name << " x=" << x << " y=" << y << " plane=V";
    }
  }
}

class OverlayFilterThresholdTest : public ::testing::TestWithParam<OverlayThresholdCase> {};

TEST_P(OverlayFilterThresholdTest, MatchesIndependentYDrivenThresholdReference) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{5, 3, VideoInfo::CS_YV24, 2, 25, 1});
  auto base_frames = make_yuv_threshold_frames(environment, vi, false);
  auto overlay_frames = make_yuv_threshold_frames(environment, vi, true);
  const auto base_before = FrameSnapshot::capture(base_frames[1], vi);
  const auto overlay_before = FrameSnapshot::capture(overlay_frames[1], vi);
  auto* base_impl = new FrameSequenceClip(vi, base_frames);
  auto* overlay_impl = new FrameSequenceClip(vi, overlay_frames);
  const PClip base(base_impl);
  const PClip overlay(overlay_impl);

  auto args =
      make_overlay_args(base, overlay, PClip(), overlay_threshold_mode(test_case.operation));
  args[5] = test_case.opacity;
  Overlay filter(base, AVSValue(args.data(), static_cast<int>(args.size())), environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, vi.pixel_type);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(1, environment.get());

  expect_yuv_threshold_reference(test_case, base_frames[1], overlay_frames[1], output);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(base_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(overlay_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(FrameSnapshot::capture(base_frames[1], vi), base_before);
  EXPECT_EQ(FrameSnapshot::capture(overlay_frames[1], vi), overlay_before);
}

INSTANTIATE_TEST_SUITE_P(
    ThresholdCases, OverlayFilterThresholdTest,
    ::testing::Values(OverlayThresholdCase{OverlayThresholdOperation::Lighten, 1.0F,
                                           "Yv24_Width5_Height3_Lighten_OpacityFull"},
                      OverlayThresholdCase{OverlayThresholdOperation::Lighten, 0.5F,
                                           "Yv24_Width5_Height3_Lighten_OpacityHalf"},
                      OverlayThresholdCase{OverlayThresholdOperation::Darken, 1.0F,
                                           "Yv24_Width5_Height3_Darken_OpacityFull"},
                      OverlayThresholdCase{OverlayThresholdOperation::Darken, 0.5F,
                                           "Yv24_Width5_Height3_Darken_OpacityHalf"}),
    [](const ::testing::TestParamInfo<OverlayThresholdCase>& info) { return info.param.name; });

struct OverlayMultiplyCase {
  float opacity;
  const char* name;
};

void PrintTo(const OverlayMultiplyCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

std::vector<PVideoFrame> make_yuv_multiply_frames(AviSynthEnvironment& environment,
                                                  const VideoInfo& vi, bool overlay) {
  constexpr std::array<std::uint8_t, 5> kBaseY{0, 64, 128, 192, 255};
  constexpr std::array<std::uint8_t, 5> kOverlayY{255, 192, 128, 64, 0};
  constexpr std::array<std::uint8_t, 5> kBaseU{16, 64, 128, 192, 240};
  constexpr std::array<std::uint8_t, 5> kOverlayU{240, 192, 128, 64, 16};
  constexpr std::array<std::uint8_t, 5> kBaseV{240, 192, 128, 64, 16};
  constexpr std::array<std::uint8_t, 5> kOverlayV{16, 64, 128, 192, 240};
  if (vi.pixel_type != VideoInfo::CS_YV24 || vi.width != 5 || vi.height != 3) {
    throw std::invalid_argument("unexpected YUV multiply test geometry");
  }

  const auto& y_values = overlay ? kOverlayY : kBaseY;
  const auto& u_values = overlay ? kOverlayU : kBaseU;
  const auto& v_values = overlay ? kOverlayV : kBaseV;
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < vi.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
      fill_plane_full_pitch(frame, overlay ? 0x33 : 0xcc, plane);
      write_frame_plane<std::uint8_t>(frame, plane, [&](int x, int y) {
        const auto& values = plane == PLANAR_Y ? y_values : plane == PLANAR_U ? u_values : v_values;
        return clamp_u8(static_cast<int>(values[static_cast<std::size_t>(x)]) + y + frame_index);
      });
    }
    frames.push_back(frame);
  }
  return frames;
}

std::uint8_t reference_multiply_y(std::uint8_t base, std::uint8_t overlay, float opacity) {
  constexpr float factor = 1.0F / 255.0F;
  const float common_factor = 1.0F + (static_cast<float>(overlay) * factor - 1.0F) * opacity;
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(static_cast<float>(base) * common_factor + 0.5F), 0, 255));
}

std::uint8_t reference_multiply_chroma(std::uint8_t base, std::uint8_t overlay_y, float opacity) {
  constexpr float factor = 1.0F / 255.0F;
  constexpr float half = 128.0F;
  const float common_factor = 1.0F + (static_cast<float>(overlay_y) * factor - 1.0F) * opacity;
  return static_cast<std::uint8_t>(std::clamp(
      static_cast<int>((static_cast<float>(base) - half) * common_factor + half + 0.5F), 0, 255));
}

void expect_yuv_multiply_reference(const OverlayMultiplyCase& test_case,
                                   const PVideoFrame& base_frame, const PVideoFrame& overlay_frame,
                                   const PVideoFrame& output) {
  const int y_pitch = output->GetPitch(PLANAR_Y);
  const int u_pitch = output->GetPitch(PLANAR_U);
  const int v_pitch = output->GetPitch(PLANAR_V);
  const int base_y_pitch = base_frame->GetPitch(PLANAR_Y);
  const int base_u_pitch = base_frame->GetPitch(PLANAR_U);
  const int base_v_pitch = base_frame->GetPitch(PLANAR_V);
  const int ov_y_pitch = overlay_frame->GetPitch(PLANAR_Y);
  const auto* out_y = output->GetReadPtr(PLANAR_Y);
  const auto* out_u = output->GetReadPtr(PLANAR_U);
  const auto* out_v = output->GetReadPtr(PLANAR_V);
  const auto* base_y = base_frame->GetReadPtr(PLANAR_Y);
  const auto* base_u = base_frame->GetReadPtr(PLANAR_U);
  const auto* base_v = base_frame->GetReadPtr(PLANAR_V);
  const auto* ov_y = overlay_frame->GetReadPtr(PLANAR_Y);

  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 5; ++x) {
      const std::uint8_t by = base_y[x + y * base_y_pitch];
      const std::uint8_t oy = ov_y[x + y * ov_y_pitch];
      const auto expected_y = reference_multiply_y(by, oy, test_case.opacity);
      const auto expected_u =
          reference_multiply_chroma(base_u[x + y * base_u_pitch], oy, test_case.opacity);
      const auto expected_v =
          reference_multiply_chroma(base_v[x + y * base_v_pitch], oy, test_case.opacity);
      EXPECT_EQ(out_y[x + y * y_pitch], expected_y)
          << "mode=" << test_case.name << " x=" << x << " y=" << y << " plane=Y";
      EXPECT_EQ(out_u[x + y * u_pitch], expected_u)
          << "mode=" << test_case.name << " x=" << x << " y=" << y << " plane=U";
      EXPECT_EQ(out_v[x + y * v_pitch], expected_v)
          << "mode=" << test_case.name << " x=" << x << " y=" << y << " plane=V";
    }
  }
}

class OverlayFilterMultiplyTest : public ::testing::TestWithParam<OverlayMultiplyCase> {};

TEST_P(OverlayFilterMultiplyTest, MatchesIndependentLumaDrivenMultiplyReference) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{5, 3, VideoInfo::CS_YV24, 2, 25, 1});
  auto base_frames = make_yuv_multiply_frames(environment, vi, false);
  auto overlay_frames = make_yuv_multiply_frames(environment, vi, true);
  const auto base_before = FrameSnapshot::capture(base_frames[1], vi);
  const auto overlay_before = FrameSnapshot::capture(overlay_frames[1], vi);
  auto* base_impl = new FrameSequenceClip(vi, base_frames);
  auto* overlay_impl = new FrameSequenceClip(vi, overlay_frames);
  const PClip base(base_impl);
  const PClip overlay(overlay_impl);

  auto args = make_overlay_args(base, overlay, PClip(), "Multiply");
  args[5] = test_case.opacity;
  Overlay filter(base, AVSValue(args.data(), static_cast<int>(args.size())), environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, vi.pixel_type);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(1, environment.get());

  expect_yuv_multiply_reference(test_case, base_frames[1], overlay_frames[1], output);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(base_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(overlay_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(FrameSnapshot::capture(base_frames[1], vi), base_before);
  EXPECT_EQ(FrameSnapshot::capture(overlay_frames[1], vi), overlay_before);
}

INSTANTIATE_TEST_SUITE_P(
    MultiplyCases, OverlayFilterMultiplyTest,
    ::testing::Values(OverlayMultiplyCase{1.0F, "Yv24_Width5_Height3_Multiply_OpacityFull"},
                      OverlayMultiplyCase{0.5F, "Yv24_Width5_Height3_Multiply_OpacityHalf"}),
    [](const ::testing::TestParamInfo<OverlayMultiplyCase>& info) { return info.param.name; });

enum class OverlaySpecialOperation { Difference, Exclusion, SoftLight, HardLight };

struct OverlaySpecialCase {
  OverlaySpecialOperation operation;
  float opacity;
  const char* name;
};

void PrintTo(const OverlaySpecialCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

const char* overlay_special_mode(OverlaySpecialOperation operation) {
  switch (operation) {
    case OverlaySpecialOperation::Difference:
      return "Difference";
    case OverlaySpecialOperation::Exclusion:
      return "Exclusion";
    case OverlaySpecialOperation::SoftLight:
      return "SoftLight";
    case OverlaySpecialOperation::HardLight:
      return "HardLight";
  }
  return "Difference";
}

std::vector<PVideoFrame> make_yuv_special_frames(AviSynthEnvironment& environment,
                                                 const VideoInfo& vi, bool overlay) {
  // Include pairs that force Y over-bright and under-dark UV compensation.
  constexpr std::array<std::uint8_t, 5> kBaseY{0, 64, 128, 200, 255};
  constexpr std::array<std::uint8_t, 5> kOverlayY{255, 200, 128, 40, 0};
  constexpr std::array<std::uint8_t, 5> kBaseU{16, 64, 128, 192, 240};
  constexpr std::array<std::uint8_t, 5> kOverlayU{240, 192, 128, 64, 16};
  constexpr std::array<std::uint8_t, 5> kBaseV{240, 192, 128, 64, 16};
  constexpr std::array<std::uint8_t, 5> kOverlayV{16, 64, 128, 192, 240};
  if (vi.pixel_type != VideoInfo::CS_YV24 || vi.width != 5 || vi.height != 3) {
    throw std::invalid_argument("unexpected YUV special-mode test geometry");
  }

  const auto& y_values = overlay ? kOverlayY : kBaseY;
  const auto& u_values = overlay ? kOverlayU : kBaseU;
  const auto& v_values = overlay ? kOverlayV : kBaseV;
  std::vector<PVideoFrame> frames;
  for (int frame_index = 0; frame_index < vi.num_frames; ++frame_index) {
    PVideoFrame frame = environment.get()->NewVideoFrame(vi);
    for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
      fill_plane_full_pitch(frame, overlay ? 0x27 : 0xd8, plane);
      write_frame_plane<std::uint8_t>(frame, plane, [&](int x, int y) {
        const auto& values = plane == PLANAR_Y ? y_values : plane == PLANAR_U ? u_values : v_values;
        return clamp_u8(static_cast<int>(values[static_cast<std::size_t>(x)]) + y + frame_index);
      });
    }
    frames.push_back(frame);
  }
  return frames;
}

void apply_y_uv_compensation(int& y_value, int& u_value, int& v_value) {
  constexpr int half = 128;
  constexpr int max_value = 255;
  constexpr int pixel_range = 256;
  constexpr int over32 = 32;
  constexpr int shift = 5;
  if (y_value > max_value) {
    const int multiplier = std::max(0, pixel_range + over32 - y_value);
    u_value = ((u_value * multiplier) + (half * (over32 - multiplier))) >> shift;
    v_value = ((v_value * multiplier) + (half * (over32 - multiplier))) >> shift;
    y_value = max_value;
  } else if (y_value < 0) {
    const int multiplier = std::min(-y_value, over32);
    u_value = ((u_value * (over32 - multiplier)) + (half * multiplier)) >> shift;
    v_value = ((v_value * (over32 - multiplier)) + (half * multiplier)) >> shift;
    y_value = 0;
  }
  u_value = std::clamp(u_value, 0, max_value);
  v_value = std::clamp(v_value, 0, max_value);
}

void reference_special_pixel(OverlaySpecialOperation operation, float opacity,
                             std::uint8_t base_y, std::uint8_t base_u, std::uint8_t base_v,
                             std::uint8_t ov_y, std::uint8_t ov_u, std::uint8_t ov_v,
                             std::uint8_t& out_y, std::uint8_t& out_u, std::uint8_t& out_v) {
  constexpr int half = 128;
  constexpr int xor_mask = 255;
  int y_value = 0;
  int u_value = 0;
  int v_value = 0;
  switch (operation) {
    case OverlaySpecialOperation::Difference:
      y_value = std::abs(static_cast<int>(base_y) - static_cast<int>(ov_y)) + half;
      u_value = std::abs(static_cast<int>(base_u) - static_cast<int>(ov_u)) + half;
      v_value = std::abs(static_cast<int>(base_v) - static_cast<int>(ov_v)) + half;
      break;
    case OverlaySpecialOperation::Exclusion: {
      const int ov = ov_y;
      y_value = (((base_y ^ xor_mask) * ov) + ((ov ^ xor_mask) * base_y)) / 255;
      u_value = (((base_u ^ xor_mask) * ov) + ((ov ^ xor_mask) * base_u)) / 255;
      v_value = (((base_v ^ xor_mask) * ov) + ((ov ^ xor_mask) * base_v)) / 255;
      break;
    }
    case OverlaySpecialOperation::SoftLight:
      y_value = static_cast<int>(base_y) + static_cast<int>(ov_y) - half;
      u_value = static_cast<int>(base_u) + static_cast<int>(ov_u) - half;
      v_value = static_cast<int>(base_v) + static_cast<int>(ov_v) - half;
      break;
    case OverlaySpecialOperation::HardLight:
      y_value = static_cast<int>(base_y) + static_cast<int>(ov_y) * 2 - half * 2;
      u_value = static_cast<int>(base_u) + static_cast<int>(ov_u) - half;
      v_value = static_cast<int>(base_v) + static_cast<int>(ov_v) - half;
      break;
  }

  if (opacity != 1.0F) {
    const int opacity_i = static_cast<int>(opacity * 255.0F + 0.5F);
    if (operation == OverlaySpecialOperation::Difference ||
        operation == OverlaySpecialOperation::Exclusion) {
      y_value = (y_value * opacity_i + (255 - opacity_i) * base_y + 127) / 255;
      u_value = (u_value * opacity_i + (255 - opacity_i) * base_u + 127) / 255;
      v_value = (v_value * opacity_i + (255 - opacity_i) * base_v + 127) / 255;
    } else {
      const int dy = y_value - static_cast<int>(base_y);
      const int du = u_value - static_cast<int>(base_u);
      const int dv = v_value - static_cast<int>(base_v);
      const int ty = (dy >= 0) ? (dy * opacity_i + 127) / 255 : -((-dy * opacity_i + 127) / 255);
      const int tu = (du >= 0) ? (du * opacity_i + 127) / 255 : -((-du * opacity_i + 127) / 255);
      const int tv = (dv >= 0) ? (dv * opacity_i + 127) / 255 : -((-dv * opacity_i + 127) / 255);
      y_value = static_cast<int>(base_y) + ty;
      u_value = static_cast<int>(base_u) + tu;
      v_value = static_cast<int>(base_v) + tv;
    }
  }

  apply_y_uv_compensation(y_value, u_value, v_value);
  out_y = static_cast<std::uint8_t>(y_value);
  out_u = static_cast<std::uint8_t>(u_value);
  out_v = static_cast<std::uint8_t>(v_value);
}

void expect_yuv_special_reference(const OverlaySpecialCase& test_case,
                                  const PVideoFrame& base_frame, const PVideoFrame& overlay_frame,
                                  const PVideoFrame& output) {
  const int y_pitch = output->GetPitch(PLANAR_Y);
  const int u_pitch = output->GetPitch(PLANAR_U);
  const int v_pitch = output->GetPitch(PLANAR_V);
  const int base_y_pitch = base_frame->GetPitch(PLANAR_Y);
  const int base_u_pitch = base_frame->GetPitch(PLANAR_U);
  const int base_v_pitch = base_frame->GetPitch(PLANAR_V);
  const int ov_y_pitch = overlay_frame->GetPitch(PLANAR_Y);
  const int ov_u_pitch = overlay_frame->GetPitch(PLANAR_U);
  const int ov_v_pitch = overlay_frame->GetPitch(PLANAR_V);
  const auto* out_y = output->GetReadPtr(PLANAR_Y);
  const auto* out_u = output->GetReadPtr(PLANAR_U);
  const auto* out_v = output->GetReadPtr(PLANAR_V);
  const auto* base_y = base_frame->GetReadPtr(PLANAR_Y);
  const auto* base_u = base_frame->GetReadPtr(PLANAR_U);
  const auto* base_v = base_frame->GetReadPtr(PLANAR_V);
  const auto* ov_y = overlay_frame->GetReadPtr(PLANAR_Y);
  const auto* ov_u = overlay_frame->GetReadPtr(PLANAR_U);
  const auto* ov_v = overlay_frame->GetReadPtr(PLANAR_V);

  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 5; ++x) {
      std::uint8_t expected_y = 0;
      std::uint8_t expected_u = 0;
      std::uint8_t expected_v = 0;
      reference_special_pixel(
          test_case.operation, test_case.opacity, base_y[x + y * base_y_pitch],
          base_u[x + y * base_u_pitch], base_v[x + y * base_v_pitch], ov_y[x + y * ov_y_pitch],
          ov_u[x + y * ov_u_pitch], ov_v[x + y * ov_v_pitch], expected_y, expected_u, expected_v);
      EXPECT_EQ(out_y[x + y * y_pitch], expected_y)
          << "mode=" << test_case.name << " x=" << x << " y=" << y << " plane=Y";
      EXPECT_EQ(out_u[x + y * u_pitch], expected_u)
          << "mode=" << test_case.name << " x=" << x << " y=" << y << " plane=U";
      EXPECT_EQ(out_v[x + y * v_pitch], expected_v)
          << "mode=" << test_case.name << " x=" << x << " y=" << y << " plane=V";
    }
  }
}

class OverlayFilterSpecialTest : public ::testing::TestWithParam<OverlaySpecialCase> {};

TEST_P(OverlayFilterSpecialTest, MatchesIndependentSpecialModeReference) {
  const auto& test_case = GetParam();
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{5, 3, VideoInfo::CS_YV24, 2, 25, 1});
  auto base_frames = make_yuv_special_frames(environment, vi, false);
  auto overlay_frames = make_yuv_special_frames(environment, vi, true);
  const auto base_before = FrameSnapshot::capture(base_frames[1], vi);
  const auto overlay_before = FrameSnapshot::capture(overlay_frames[1], vi);
  auto* base_impl = new FrameSequenceClip(vi, base_frames);
  auto* overlay_impl = new FrameSequenceClip(vi, overlay_frames);
  const PClip base(base_impl);
  const PClip overlay(overlay_impl);

  auto args = make_overlay_args(base, overlay, PClip(), overlay_special_mode(test_case.operation));
  args[5] = test_case.opacity;
  Overlay filter(base, AVSValue(args.data(), static_cast<int>(args.size())), environment.get());
  EXPECT_EQ(filter.GetVideoInfo().pixel_type, vi.pixel_type);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  const PVideoFrame output = filter.GetFrame(1, environment.get());

  expect_yuv_special_reference(test_case, base_frames[1], overlay_frames[1], output);
  EXPECT_NE(output->CheckMemory(), 1);
  EXPECT_EQ(base_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(overlay_impl->frame_requests(), std::vector<int>{1});
  EXPECT_EQ(FrameSnapshot::capture(base_frames[1], vi), base_before);
  EXPECT_EQ(FrameSnapshot::capture(overlay_frames[1], vi), overlay_before);
}

INSTANTIATE_TEST_SUITE_P(
    SpecialCases, OverlayFilterSpecialTest,
    ::testing::Values(OverlaySpecialCase{OverlaySpecialOperation::Difference, 1.0F,
                                         "Yv24_Width5_Height3_Difference_OpacityFull"},
                      OverlaySpecialCase{OverlaySpecialOperation::Difference, 0.5F,
                                         "Yv24_Width5_Height3_Difference_OpacityHalf"},
                      OverlaySpecialCase{OverlaySpecialOperation::Exclusion, 1.0F,
                                         "Yv24_Width5_Height3_Exclusion_OpacityFull"},
                      OverlaySpecialCase{OverlaySpecialOperation::Exclusion, 0.5F,
                                         "Yv24_Width5_Height3_Exclusion_OpacityHalf"},
                      OverlaySpecialCase{OverlaySpecialOperation::SoftLight, 1.0F,
                                         "Yv24_Width5_Height3_SoftLight_OpacityFull"},
                      OverlaySpecialCase{OverlaySpecialOperation::SoftLight, 0.5F,
                                         "Yv24_Width5_Height3_SoftLight_OpacityHalf"},
                      OverlaySpecialCase{OverlaySpecialOperation::HardLight, 1.0F,
                                         "Yv24_Width5_Height3_HardLight_OpacityFull"},
                      OverlaySpecialCase{OverlaySpecialOperation::HardLight, 0.5F,
                                         "Yv24_Width5_Height3_HardLight_OpacityHalf"}),
    [](const ::testing::TestParamInfo<OverlaySpecialCase>& info) { return info.param.name; });

}  // namespace
