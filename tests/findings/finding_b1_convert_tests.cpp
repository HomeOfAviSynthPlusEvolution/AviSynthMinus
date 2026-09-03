#include <gtest/gtest.h>

#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_FINDING_UNDEF_AVS_UNUSED
#endif
#include "convert/convert_audio.h"
#include "convert/convert_bits.h"
#include "convert/convert_helper.h"
#include "convert/intel/convert_bits_avx2.h"
#include "convert/intel/convert_bits_sse.h"
#ifdef AVSUT_FINDING_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_FINDING_UNDEF_AVS_UNUSED
#endif

#include "support/avisynth_environment.h"
#include "support/cpu_features.h"
#include "support/guarded_video_buffer.h"
#include "support/video_filter_test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

namespace avsut::test {
namespace {

using AudioConvertFunction = void (*)(void*, void*, int);

struct AudioNonFiniteCase {
  std::string name;
  std::size_t bytes_per_sample;
  AudioConvertFunction scalar;
  AudioConvertFunction sse;
  AudioConvertFunction avx2;
};

void PrintTo(const AudioNonFiniteCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

std::vector<AudioNonFiniteCase> audio_non_finite_cases() {
  return {
      {"ToUInt8_Count17_PatternQuietNanVectorAndTail", sizeof(std::uint8_t), convertFLTTo8,
       convertFLTTo8_SSE2, convertFLTTo8_AVX2},
      {"ToInt16_Count17_PatternQuietNanVectorAndTail", sizeof(std::int16_t), convertFLTTo16,
       convertFLTTo16_SSE2, convertFLTTo16_AVX2},
      {"ToInt32_Count17_PatternQuietNanVectorAndTail", sizeof(std::int32_t), convertFLTTo32,
       convertFLTTo32_SSE41, convertFLTTo32_AVX2},
  };
}

std::array<float, 17> non_finite_audio_input() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  return {nan,    -1.25F, -0.75F, nan,   -0.5F, -0.125F, 0.0F, nan,  0.125F,
          0.5F,   0.75F,  0.99F,  nan,   1.0F,  1.25F,  -0.0F, nan};
}

bool same_float_bits(const std::array<float, 17>& lhs, const std::array<float, 17>& rhs) {
  return std::memcmp(lhs.data(), rhs.data(), lhs.size() * sizeof(float)) == 0;
}

std::vector<std::uint8_t> run_audio_conversion(AudioConvertFunction function,
                                                std::array<float, 17>& source,
                                                std::size_t bytes_per_sample) {
  std::vector<std::uint8_t> output(source.size() * bytes_per_sample, 0);
  function(source.data(), output.data(), static_cast<int>(source.size()));
  return output;
}

void expect_equal_audio_bytes(const std::vector<std::uint8_t>& expected,
                              const std::vector<std::uint8_t>& actual,
                              const AudioNonFiniteCase& test_case, const char* variant) {
  ASSERT_EQ(expected.size(), actual.size());
  for (std::size_t byte_index = 0; byte_index < expected.size(); ++byte_index) {
    EXPECT_EQ(expected[byte_index], actual[byte_index])
        << "B1 float-to-" << test_case.name << " variant=" << variant
        << " byte_index=" << byte_index << " bytes_per_sample=" << test_case.bytes_per_sample;
  }
}

class ConvertAudioNonFinite : public ::testing::TestWithParam<AudioNonFiniteCase> {};

TEST_P(ConvertAudioNonFinite, MapsQuietNanConsistentlyAcrossAvailableImplementations) {
  const auto& test_case = GetParam();
  const auto features = CpuFeatures::detect();
  if (!features.supports(IsaRequirement::Sse2)) {
    GTEST_SKIP() << "host does not support sse2";
  }

  auto source = non_finite_audio_input();
  const auto source_before = source;
  const auto scalar = run_audio_conversion(test_case.scalar, source, test_case.bytes_per_sample);
  EXPECT_TRUE(same_float_bits(source, source_before))
      << "B1 float-to-" << test_case.name << " scalar modified input";

  source = source_before;
  const auto sse = run_audio_conversion(test_case.sse, source, test_case.bytes_per_sample);
  EXPECT_TRUE(same_float_bits(source, source_before))
      << "B1 float-to-" << test_case.name << " sse modified input";
  expect_equal_audio_bytes(scalar, sse, test_case, "sse");

  if (features.supports(IsaRequirement::Avx2)) {
    source = source_before;
    const auto avx2 = run_audio_conversion(test_case.avx2, source, test_case.bytes_per_sample);
    EXPECT_TRUE(same_float_bits(source, source_before))
        << "B1 float-to-" << test_case.name << " avx2 modified input";
    expect_equal_audio_bytes(scalar, avx2, test_case, "avx2");
  }
}

INSTANTIATE_TEST_SUITE_P(B1, ConvertAudioNonFinite, ::testing::ValuesIn(audio_non_finite_cases()),
                         [](const ::testing::TestParamInfo<AudioNonFiniteCase>& info) {
                           return info.param.name;
                         });

float next_float_steps(float value, int direction, int steps) {
  const float destination = direction < 0 ? -std::numeric_limits<float>::infinity()
                                          : std::numeric_limits<float>::infinity();
  for (int step = 0; step < steps; ++step) {
    value = std::nextafter(value, destination);
  }
  return value;
}

TEST(ConvertBitsFmaRounding, KeepsSse41AndAvx2FmaThresholdResultsExact) {
  const auto features = CpuFeatures::detect();
  if (!features.supports(IsaRequirement::Sse41)) {
    GTEST_SKIP() << "host does not support sse4.1";
  }
  if (!features.supports(IsaRequirement::Avx2Fma)) {
    GTEST_SKIP() << "host does not support avx2 and fma3";
  }

  constexpr std::size_t width = 16;
  GuardedVideoBuffer<float> source(width, 1, width * sizeof(float), 64);
  GuardedVideoBuffer<std::uint16_t> sse_output(width, 1, width * sizeof(std::uint16_t), 64);
  GuardedVideoBuffer<std::uint16_t> avx2_output(width, 1, width * sizeof(std::uint16_t), 64);

  constexpr std::array<int, 5> offsets{-2, -1, 0, 1, 2};
  for (int threshold = 1; threshold < 1023; ++threshold) {
    const float boundary = static_cast<float>(threshold - 0.5F) / 1023.0F;
    for (const int offset : offsets) {
      const float candidate = offset == 0
                                  ? boundary
                                  : next_float_steps(boundary, offset < 0 ? -1 : 1,
                                                     std::abs(offset));
      std::fill(source.view().row(0), source.view().row(0) + width, candidate);
      std::fill(sse_output.view().row(0), sse_output.view().row(0) + width, 0);
      std::fill(avx2_output.view().row(0), avx2_output.view().row(0) + width, 0);

      convert_32_to_uintN_sse41<std::uint16_t, false, true, true>(
          reinterpret_cast<const BYTE*>(source.view().data()),
          reinterpret_cast<BYTE*>(sse_output.view().data()), static_cast<int>(width * sizeof(float)),
          1, static_cast<int>(width * sizeof(float)),
          static_cast<int>(width * sizeof(std::uint16_t)), 32, 10, 10);
      convert_32_to_uintN_avx2<std::uint16_t, false, true, true>(
          reinterpret_cast<const BYTE*>(source.view().data()),
          reinterpret_cast<BYTE*>(avx2_output.view().data()), static_cast<int>(width * sizeof(float)),
          1, static_cast<int>(width * sizeof(float)),
          static_cast<int>(width * sizeof(std::uint16_t)), 32, 10, 10);

      for (std::size_t x = 0; x < width; ++x) {
        ASSERT_EQ(sse_output.view().row(0)[x], avx2_output.view().row(0)[x])
            << "B1 float-to-10 threshold=" << threshold << " offset_ulps=" << offset
            << " input=" << candidate << " column=" << x;
      }
    }
  }

  EXPECT_TRUE(source.memory_intact()) << "B1 float threshold scan modified source guards";
  EXPECT_TRUE(sse_output.memory_intact()) << "B1 float threshold scan modified sse guards";
  EXPECT_TRUE(avx2_output.memory_intact()) << "B1 float threshold scan modified avx2 guards";
}

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
