#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_bits.h"
#include "convert/convert_helper.h"
#ifdef AVSUT_FINDING_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/video_filter_test_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace avsut::test {
namespace {

void set_color_range(PVideoFrame& frame, IScriptEnvironment* environment, int range) {
  AVSMap* properties = environment->getFramePropsRW(frame);
  ASSERT_NE(properties, nullptr);
  ASSERT_EQ(environment->propSetInt(properties, "_ColorRange", range,
                                    AVSPropAppendMode::PROPAPPENDMODE_REPLACE),
            0);
}

std::uint16_t read_y16(const PVideoFrame& frame) {
  return reinterpret_cast<const std::uint16_t*>(frame->GetReadPtr(PLANAR_Y))[0];
}

int read_color_range(const PVideoFrame& frame, IScriptEnvironment* environment) {
  int error = 0;
  const AVSMap* properties = environment->getFramePropsRO(frame);
  const auto value = environment->propGetInt(properties, "_ColorRange", 0, &error);
  EXPECT_EQ(error, 0) << "B1 output _ColorRange was absent";
  return static_cast<int>(value);
}

PClip create_convert_bits(PClip clip, int bits, int dither, int dither_bits, AVSValue fulls,
                          AVSValue fulld, IScriptEnvironment* environment) {
  AVSValue args[7] = {clip, bits, true, dither, dither_bits, fulls, fulld};
  return ConvertBits::Create(AVSValue(args, 7), nullptr, environment).AsClip();
}

TEST(ConvertBitsFactory, UsesEachFrameColorRangeWhenSourceRangeIsUnspecified) {
  AviSynthEnvironment environment;
  constexpr int width = 8;
  const auto vi = make_video_info(VideoInfoSpec{width, 1, VideoInfo::CS_Y8, 2, 25, 1});
  PVideoFrame frame0 = environment.get()->NewVideoFrame(vi);
  PVideoFrame frame1 = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(frame0, 0xa1, PLANAR_Y);
  fill_plane_full_pitch(frame1, 0xb2, PLANAR_Y);
  std::fill(frame0->GetWritePtr(PLANAR_Y), frame0->GetWritePtr(PLANAR_Y) + width, 16);
  std::fill(frame1->GetWritePtr(PLANAR_Y), frame1->GetWritePtr(PLANAR_Y) + width, 16);
  set_color_range(frame0, environment.get(), ColorRange_Compat_e::AVS_COLORRANGE_FULL);
  set_color_range(frame1, environment.get(), ColorRange_Compat_e::AVS_COLORRANGE_LIMITED);
  const auto frame0_before = FrameSnapshot::capture(frame0, vi);
  const auto frame1_before = FrameSnapshot::capture(frame1, vi);

  const PClip source(new FrameSequenceClip(vi, {frame0, frame1}));
  const PClip converted = create_convert_bits(source, 10, -1, 10, AVSValue(), AVSValue(false),
                                               environment.get());
  const PVideoFrame output0 = converted->GetFrame(0, environment.get());
  const PVideoFrame output1 = converted->GetFrame(1, environment.get());

  EXPECT_EQ(read_y16(output0), 119) << "B1 frame 0 full-to-limited conversion";
  EXPECT_EQ(read_y16(output1), 64) << "B1 frame 1 limited-to-limited conversion";
  EXPECT_EQ(read_color_range(output0, environment.get()),
            ColorRange_Compat_e::AVS_COLORRANGE_LIMITED);
  EXPECT_EQ(read_color_range(output1, environment.get()),
            ColorRange_Compat_e::AVS_COLORRANGE_LIMITED);
  EXPECT_EQ(FrameSnapshot::capture(frame0, vi), frame0_before) << "B1 modified frame 0";
  EXPECT_EQ(FrameSnapshot::capture(frame1, vi), frame1_before) << "B1 modified frame 1";
}

TEST(ConvertBitsFactory, RejectsUnsupportedFrameColorRangeValues) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 1, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(frame, 16, PLANAR_Y);
  set_color_range(frame, environment.get(), 2);
  const PClip source(new StaticFrameClip(vi, frame));

  EXPECT_THROW(create_convert_bits(source, 10, -1, 10, AVSValue(), AVSValue(false),
                                   environment.get()),
               AvisynthError);
}

TEST(ConvertBitsFactory, PreservesRequestedOrderedDitherQuantizationDepth) {
  AviSynthEnvironment environment;
  constexpr int width = 16;
  const auto vi = make_video_info(VideoInfoSpec{width, 1, VideoInfo::CS_Y16, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(frame, 0xa5, PLANAR_Y);
  const std::array<std::uint16_t, width> values{
      0x0000, 0x0100, 0x0800, 0x1800, 0x2800, 0x3800, 0x4800, 0x5800,
      0x6800, 0x7800, 0x8800, 0x9800, 0xa800, 0xc000, 0xe000, 0xffff,
  };
  std::copy(values.begin(), values.end(),
            reinterpret_cast<std::uint16_t*>(frame->GetWritePtr(PLANAR_Y)));
  const auto frame_before = FrameSnapshot::capture(frame, vi);
  const PClip source(new StaticFrameClip(vi, frame));
  const PClip converted = create_convert_bits(source, 16, 0, 1, AVSValue(true), AVSValue(true),
                                               environment.get());
  const PVideoFrame output = converted->GetFrame(0, environment.get());
  const auto* output_values = reinterpret_cast<const std::uint16_t*>(output->GetReadPtr(PLANAR_Y));

  for (int x = 0; x < width; ++x) {
    EXPECT_TRUE(output_values[x] == 0 || output_values[x] == 0xffff)
        << "B1 ordered-dither source=16 target=16 dither_bits=1 column=" << x
        << " output=" << output_values[x];
  }
  EXPECT_EQ(FrameSnapshot::capture(frame, vi), frame_before) << "B1 modified dither source";
}

}  // namespace
}  // namespace avsut::test
