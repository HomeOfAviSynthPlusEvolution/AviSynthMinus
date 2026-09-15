#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_LOCAL_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_bits.h"
#include "convert/convert_helper.h"
#ifdef AVSUT_LOCAL_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_LOCAL_UNDEF_AVS_UNUSED
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
  ASSERT_EQ(environment->propSetInt(properties, "_ColorRange", range, AVSPropAppendMode::PROPAPPENDMODE_REPLACE), 0);
}

PClip create_convert_bits(PClip clip, int bits, int dither, int dither_bits, AVSValue fulls, AVSValue fulld,
                          IScriptEnvironment* environment) {
  AVSValue args[7] = {clip, bits, true, dither, dither_bits, fulls, fulld};
  return ConvertBits::Create(AVSValue(args, 7), nullptr, environment).AsClip();
}

TEST(ConvertBitsFactory, RejectsUnsupportedFrameColorRangeValues) {
  AviSynthEnvironment environment;
  const auto vi = make_video_info(VideoInfoSpec{8, 1, VideoInfo::CS_Y8, 1, 25, 1});
  PVideoFrame frame = environment.get()->NewVideoFrame(vi);
  fill_plane_full_pitch(frame, 16, PLANAR_Y);
  set_color_range(frame, environment.get(), 2);
  const PClip source(new StaticFrameClip(vi, frame));

  EXPECT_THROW(create_convert_bits(source, 10, -1, 10, AVSValue(), AVSValue(false), environment.get()), AvisynthError);
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
  std::copy(values.begin(), values.end(), reinterpret_cast<std::uint16_t*>(frame->GetWritePtr(PLANAR_Y)));
  const auto frame_before = FrameSnapshot::capture(frame, vi);
  const PClip source(new StaticFrameClip(vi, frame));
  const PClip converted = create_convert_bits(source, 16, 0, 1, AVSValue(true), AVSValue(true), environment.get());
  const PVideoFrame output = converted->GetFrame(0, environment.get());
  const auto* output_values = reinterpret_cast<const std::uint16_t*>(output->GetReadPtr(PLANAR_Y));

  for (int x = 0; x < width; ++x) {
    EXPECT_TRUE(output_values[x] == 0 || output_values[x] == 0xffff)
        << "ordered-dither source=16 target=16 dither_bits=1 column=" << x << " output=" << output_values[x];
  }
  EXPECT_EQ(FrameSnapshot::capture(frame, vi), frame_before) << "modified dither source";
}

} // namespace
} // namespace avsut::test
