#include "support/avisynth_environment.h"
#include "support/audio_sequence_clip.h"
#include "support/audio_test_helpers.h"
#include "support/guarded_audio_buffer.h"
#include "support/video_filter_test_support.h"

#include <avisynth.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {

using avsut::test::AudioBoundsPolicy;
using avsut::test::AudioInfoSpec;
using avsut::test::AudioRequest;
using avsut::test::AudioSequenceClip;
using avsut::test::AviSynthEnvironment;
using avsut::test::expect_audio_requests;
using avsut::test::expect_audio_source_unchanged;
using avsut::test::FrameSequenceClip;
using avsut::test::get_frame_property_int;
using avsut::test::GuardedAudioBuffer;
using avsut::test::make_audio_bytes;
using avsut::test::make_audio_video_info;
using avsut::test::make_video_info;
using avsut::test::read_audio_sample;
using avsut::test::read_frame_plane_active;
using avsut::test::set_frame_property_int;
using avsut::test::VideoInfoSpec;
using avsut::test::write_frame_plane;

enum class CpuMode { Native, None };

inline const char* cpu_mode_name(CpuMode mode) {
  return mode == CpuMode::Native ? "Native" : "CpuNone";
}

struct CpuModeNameGenerator {
  std::string operator()(const ::testing::TestParamInfo<CpuMode>& info) const {
    return cpu_mode_name(info.param);
  }
};

inline void configure_cpu_mode(IScriptEnvironment* env, CpuMode mode) {
  if (mode == CpuMode::None) {
    const AVSValue none("none");
    env->Invoke("SetMaxCPU", AVSValue(&none, 1));
    constexpr int simd_mask = CPUF_MMX | CPUF_SSE | CPUF_SSE2 | CPUF_SSSE3 | CPUF_SSE4_1 | CPUF_AVX | CPUF_AVX2;
    ASSERT_EQ(env->GetCPUFlags() & simd_mask, 0)
        << "SetMaxCPU none must clear all SIMD flags";
  }
}

inline std::vector<std::uint8_t> make_s24_le_bytes(const std::vector<std::int32_t>& values) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(values.size() * 3);
  for (std::int32_t value : values) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  }
  return bytes;
}

inline std::int32_t read_s24_le(const std::uint8_t* data) {
  std::uint32_t u = static_cast<std::uint32_t>(data[0]) |
                    (static_cast<std::uint32_t>(data[1]) << 8) |
                    (static_cast<std::uint32_t>(data[2]) << 16);
  if (u & 0x00800000U) {
    u |= 0xFF000000U;
  }
  return static_cast<std::int32_t>(u);
}

inline void verify_float_exact(const GuardedAudioBuffer& actual,
                               const std::vector<float>& expected,
                               const std::string& context) {
  ASSERT_EQ(actual.active_bytes(), expected.size() * sizeof(float)) << context;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const float actual_val = read_audio_sample<float>(actual, i);
    EXPECT_EQ(actual_val, expected[i])
        << context << " sample=" << i << " expected=" << expected[i]
        << " actual=" << actual_val;
  }
}

inline void verify_integer_exact(const GuardedAudioBuffer& actual,
                                 const std::vector<std::uint8_t>& expected,
                                 const std::string& context) {
  ASSERT_EQ(actual.active_bytes(), expected.size()) << context;
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(actual.data()[i], expected[i])
        << context << " byte=" << i << " expected=0x" << std::hex
        << static_cast<int>(expected[i]) << " actual=0x"
        << static_cast<int>(actual.data()[i]) << std::dec;
  }
}

// ---------------------------------------------------------------------------
// 4.1 Route cases for 20 cross-format conversions
// ---------------------------------------------------------------------------

struct RouteCase {
  std::string name;
  std::string filter_name;
  int src_format{};
  int dst_format{};
  std::vector<std::uint8_t> src_bytes;
  std::vector<std::uint8_t> expected_int_bytes;
  std::vector<float> expected_floats;
  bool is_dst_float{false};
};

inline void PrintTo(const RouteCase& rc, std::ostream* os) {
  *os << rc.name;
}

inline std::vector<RouteCase> build_20_route_cases() {
  std::vector<RouteCase> cases;
  cases.reserve(20);

  // U8 source: 7 representative samples (silence 128, min 0, max 255, +1, -1, +64, -64)
  const std::vector<std::uint8_t> u8_src{128, 0, 255, 129, 127, 192, 64};

  // 1. U8 -> S16
  cases.push_back(RouteCase{
      "U8ToS16", "ConvertAudioTo16bit", SAMPLE_INT8, SAMPLE_INT16,
      u8_src,
      make_audio_bytes<std::int16_t>({0, -32768, 32512, 256, -256, 16384, -16384}),
      {}, false});

  // 2. U8 -> S24
  cases.push_back(RouteCase{
      "U8ToS24", "ConvertAudioTo24bit", SAMPLE_INT8, SAMPLE_INT24,
      u8_src,
      make_s24_le_bytes({0, -8388608, 8323072, 65536, -65536, 4194304, -4194304}),
      {}, false});

  // 3. U8 -> S32
  cases.push_back(RouteCase{
      "U8ToS32", "ConvertAudioTo32bit", SAMPLE_INT8, SAMPLE_INT32,
      u8_src,
      make_audio_bytes<std::int32_t>({0, -2147483648LL, 2130706432, 16777216, -16777216, 1073741824, -1073741824}),
      {}, false});

  // 4. U8 -> F32
  cases.push_back(RouteCase{
      "U8ToF32", "ConvertAudioToFloat", SAMPLE_INT8, SAMPLE_FLOAT,
      u8_src,
      {},
      {0.0f, -1.0f, 127.0f / 128.0f, 1.0f / 128.0f, -1.0f / 128.0f, 0.5f, -0.5f},
      true});

  // S16 source: 7 representative samples (0, min -32768, max 32767, +256, -256, +16384, -16384)
  const auto s16_src = make_audio_bytes<std::int16_t>({0, -32768, 32767, 256, -256, 16384, -16384});

  // 5. S16 -> U8
  cases.push_back(RouteCase{
      "S16ToU8", "ConvertAudioTo8bit", SAMPLE_INT16, SAMPLE_INT8,
      s16_src,
      {128, 0, 255, 129, 127, 192, 64},
      {}, false});

  // 6. S16 -> S24
  cases.push_back(RouteCase{
      "S16ToS24", "ConvertAudioTo24bit", SAMPLE_INT16, SAMPLE_INT24,
      s16_src,
      make_s24_le_bytes({0, -8388608, 8388352, 65536, -65536, 4194304, -4194304}),
      {}, false});

  // 7. S16 -> S32
  cases.push_back(RouteCase{
      "S16ToS32", "ConvertAudioTo32bit", SAMPLE_INT16, SAMPLE_INT32,
      s16_src,
      make_audio_bytes<std::int32_t>({0, -2147483648LL, 2147418112, 16777216, -16777216, 1073741824, -1073741824}),
      {}, false});

  // 8. S16 -> F32
  cases.push_back(RouteCase{
      "S16ToF32", "ConvertAudioToFloat", SAMPLE_INT16, SAMPLE_FLOAT,
      s16_src,
      {},
      {0.0f, -1.0f, 32767.0f / 32768.0f, 256.0f / 32768.0f, -256.0f / 32768.0f, 0.5f, -0.5f},
      true});

  // S24 source: 7 representative samples (0, min -8388608, max 8388607, +65536, -65536, +4194304, -4194304)
  const auto s24_src = make_s24_le_bytes({0, -8388608, 8388607, 65536, -65536, 4194304, -4194304});

  // 9. S24 -> U8
  cases.push_back(RouteCase{
      "S24ToU8", "ConvertAudioTo8bit", SAMPLE_INT24, SAMPLE_INT8,
      s24_src,
      {128, 0, 255, 129, 127, 192, 64},
      {}, false});

  // 10. S24 -> S16
  cases.push_back(RouteCase{
      "S24ToS16", "ConvertAudioTo16bit", SAMPLE_INT24, SAMPLE_INT16,
      s24_src,
      make_audio_bytes<std::int16_t>({0, -32768, 32767, 256, -256, 16384, -16384}),
      {}, false});

  // 11. S24 -> S32
  cases.push_back(RouteCase{
      "S24ToS32", "ConvertAudioTo32bit", SAMPLE_INT24, SAMPLE_INT32,
      s24_src,
      make_audio_bytes<std::int32_t>({0, -2147483648LL, 2147483392, 16777216, -16777216, 1073741824, -1073741824}),
      {}, false});

  // 12. S24 -> F32 (two-stage: S24 -> S32 -> F32)
  cases.push_back(RouteCase{
      "S24ToF32", "ConvertAudioToFloat", SAMPLE_INT24, SAMPLE_FLOAT,
      s24_src,
      {},
      {0.0f, -1.0f, 8388607.0f / 8388608.0f, 65536.0f / 8388608.0f, -65536.0f / 8388608.0f, 0.5f, -0.5f},
      true});

  // S32 source: 7 representative samples (0, min -2147483648, max 2147483647, +16777216, -16777216, +1073741824, -1073741824)
  const auto s32_src = make_audio_bytes<std::int32_t>({0, -2147483648LL, 2147483647, 16777216, -16777216, 1073741824, -1073741824});

  // 13. S32 -> U8
  cases.push_back(RouteCase{
      "S32ToU8", "ConvertAudioTo8bit", SAMPLE_INT32, SAMPLE_INT8,
      s32_src,
      {128, 0, 255, 129, 127, 192, 64},
      {}, false});

  // 14. S32 -> S16
  cases.push_back(RouteCase{
      "S32ToS16", "ConvertAudioTo16bit", SAMPLE_INT32, SAMPLE_INT16,
      s32_src,
      make_audio_bytes<std::int16_t>({0, -32768, 32767, 256, -256, 16384, -16384}),
      {}, false});

  // 15. S32 -> S24
  cases.push_back(RouteCase{
      "S32ToS24", "ConvertAudioTo24bit", SAMPLE_INT32, SAMPLE_INT24,
      s32_src,
      make_s24_le_bytes({0, -8388608, 8388607, 65536, -65536, 4194304, -4194304}),
      {}, false});

  // 16. S32 -> F32
  cases.push_back(RouteCase{
      "S32ToF32", "ConvertAudioToFloat", SAMPLE_INT32, SAMPLE_FLOAT,
      s32_src,
      {},
      {0.0f, -1.0f, 1.0f, 1.0f / 128.0f, -1.0f / 128.0f, 0.5f, -0.5f},
      true});

  // F32 source: 7 representative samples (0.0f, -1.0f, +1.25f clamped, -1.25f clamped, +0.5f, -0.5f, +0.25f)
  const auto f32_src = make_audio_bytes<float>({0.0f, -1.0f, 1.25f, -1.25f, 0.5f, -0.5f, 0.25f});

  // 17. F32 -> U8
  cases.push_back(RouteCase{
      "F32ToU8", "ConvertAudioTo8bit", SAMPLE_FLOAT, SAMPLE_INT8,
      f32_src,
      {128, 0, 255, 0, 192, 64, 160},
      {}, false});

  // 18. F32 -> S16
  cases.push_back(RouteCase{
      "F32ToS16", "ConvertAudioTo16bit", SAMPLE_FLOAT, SAMPLE_INT16,
      f32_src,
      make_audio_bytes<std::int16_t>({0, -32768, 32767, -32768, 16384, -16384, 8192}),
      {}, false});

  // 19. F32 -> S24 (two-stage: F32 -> S32 -> S24)
  cases.push_back(RouteCase{
      "F32ToS24", "ConvertAudioTo24bit", SAMPLE_FLOAT, SAMPLE_INT24,
      f32_src,
      make_s24_le_bytes({0, -8388608, 8388607, -8388608, 4194304, -4194304, 2097152}),
      {}, false});

  // 20. F32 -> S32
  cases.push_back(RouteCase{
      "F32ToS32", "ConvertAudioTo32bit", SAMPLE_FLOAT, SAMPLE_INT32,
      f32_src,
      make_audio_bytes<std::int32_t>({0, -2147483648LL, 2147483647, -2147483648LL, 1073741824, -1073741824, 536870912}),
      {}, false});

  return cases;
}

// ---------------------------------------------------------------------------
// 4.1 Route Parameterized Test Suite
// ---------------------------------------------------------------------------

using RouteParam = std::tuple<CpuMode, RouteCase>;

struct RouteNameGenerator {
  std::string operator()(const ::testing::TestParamInfo<RouteParam>& info) const {
    const CpuMode mode = std::get<0>(info.param);
    const RouteCase& route = std::get<1>(info.param);
    return std::string(cpu_mode_name(mode)) + "_" + route.name;
  }
};

class ConvertAudioRouteSuite : public ::testing::TestWithParam<RouteParam> {};

TEST_P(ConvertAudioRouteSuite, ConvertsRepresentativeSamplesAcrossTwentyDirections) {
  const auto& param = GetParam();
  const CpuMode cpu_mode = std::get<0>(param);
  const RouteCase& route = std::get<1>(param);

  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), cpu_mode));

  constexpr int sample_count = 7;
  constexpr int channels = 1;
  constexpr int sample_rate = 48000;

  const auto vi_src = make_audio_video_info(
      AudioInfoSpec{sample_rate, route.src_format, sample_count, channels});
  auto* source_clip = new AudioSequenceClip(vi_src, route.src_bytes);
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  AVSValue arg(source);
  AVSValue res = env.get()->Invoke(route.filter_name.c_str(), AVSValue(&arg, 1));
  PClip converted = res.AsClip();
  ASSERT_TRUE(converted);

  const auto& vi_dst = converted->GetVideoInfo();
  EXPECT_EQ(vi_dst.SampleType(), route.dst_format)
      << route.name << " " << cpu_mode_name(cpu_mode);
  EXPECT_EQ(vi_dst.AudioChannels(), channels)
      << route.name << " " << cpu_mode_name(cpu_mode);
  EXPECT_EQ(vi_dst.audio_samples_per_second, sample_rate)
      << route.name << " " << cpu_mode_name(cpu_mode);
  EXPECT_EQ(vi_dst.num_audio_samples, sample_count)
      << route.name << " " << cpu_mode_name(cpu_mode);
  EXPECT_FALSE(vi_dst.IsChannelMaskKnown());
  EXPECT_EQ(vi_dst.GetChannelMask(), vi_src.GetChannelMask());

  const auto expected_bytes = vi_dst.BytesFromAudioSamples(sample_count);
  GuardedAudioBuffer output(expected_bytes, 64, 64, 4);
  converted->GetAudio(output.data(), 0, sample_count, env.get());

  EXPECT_TRUE(output.memory_intact())
      << route.name << " " << cpu_mode_name(cpu_mode) << " guard buffer corrupted";
  expect_audio_source_unchanged(*source_clip, source_before);

  const std::string context = route.name + " (" + cpu_mode_name(cpu_mode) + ")";
  if (route.is_dst_float) {
    verify_float_exact(output, route.expected_floats, context);
  } else {
    verify_integer_exact(output, route.expected_int_bytes, context);
  }
}

INSTANTIATE_TEST_SUITE_P(
    ConvertAudioApi, ConvertAudioRouteSuite,
    ::testing::Combine(
        ::testing::Values(CpuMode::Native, CpuMode::None),
        ::testing::ValuesIn(build_20_route_cases())),
    RouteNameGenerator());

// ---------------------------------------------------------------------------
// 4.1 General Entrypoint Factory Semantics Suite
// ---------------------------------------------------------------------------

class ConvertAudioFactorySuite : public ::testing::TestWithParam<CpuMode> {};

TEST_P(ConvertAudioFactorySuite, PassesThroughWhenAcceptedContainsCurrentFormat) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, 8, 1});
  const auto src_bytes = make_audio_bytes<std::int16_t>({100, 200, 300, 400, 500, 600, 700, 800});
  auto* source_clip = new AudioSequenceClip(vi, src_bytes);
  PClip source(source_clip);

  AVSValue args[3] = {source, SAMPLE_INT16, SAMPLE_FLOAT};
  AVSValue res = env.get()->Invoke("ConvertAudio", AVSValue(args, 3));
  PClip converted = res.AsClip();
  ASSERT_TRUE(converted);

  EXPECT_EQ(converted->GetVideoInfo().SampleType(), SAMPLE_INT16)
      << "accepted containing current format must pass clip through without conversion";

  GuardedAudioBuffer output(converted->GetVideoInfo().BytesFromAudioSamples(8), 64, 64, 4);
  converted->GetAudio(output.data(), 0, 8, env.get());
  EXPECT_TRUE(output.memory_intact());
  verify_integer_exact(output, src_bytes, "Pass-through accepted");
  expect_audio_requests(*source_clip, {{0, 8}});
  expect_audio_source_unchanged(*source_clip, src_bytes);
}

TEST_P(ConvertAudioFactorySuite, PassesThroughWhenPreferredContainsCurrentFormat) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, 8, 1});
  const auto src_bytes = make_audio_bytes<std::int16_t>({100, 200, 300, 400, 500, 600, 700, 800});
  auto* source_clip = new AudioSequenceClip(vi, src_bytes);
  PClip source(source_clip);

  AVSValue args[3] = {source, SAMPLE_FLOAT, SAMPLE_INT16};
  AVSValue res = env.get()->Invoke("ConvertAudio", AVSValue(args, 3));
  PClip converted = res.AsClip();
  ASSERT_TRUE(converted);

  EXPECT_EQ(converted->GetVideoInfo().SampleType(), SAMPLE_INT16)
      << "preferred containing current format must pass clip through without conversion";

  GuardedAudioBuffer output(converted->GetVideoInfo().BytesFromAudioSamples(8), 64, 64, 4);
  converted->GetAudio(output.data(), 0, 8, env.get());
  EXPECT_TRUE(output.memory_intact());
  verify_integer_exact(output, src_bytes, "Pass-through preferred");
  expect_audio_requests(*source_clip, {{0, 8}});
  expect_audio_source_unchanged(*source_clip, src_bytes);
}

TEST_P(ConvertAudioFactorySuite, PassesThroughWhenAcceptedIsBitmaskMatchingCurrent) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, 8, 1});
  const auto src_bytes = make_audio_bytes<std::int16_t>({100, 200, 300, 400, 500, 600, 700, 800});
  auto* source_clip = new AudioSequenceClip(vi, src_bytes);
  PClip source(source_clip);

  AVSValue args[3] = {source, SAMPLE_INT8 | SAMPLE_INT16 | SAMPLE_INT32, SAMPLE_FLOAT};
  AVSValue res = env.get()->Invoke("ConvertAudio", AVSValue(args, 3));
  PClip converted = res.AsClip();
  ASSERT_TRUE(converted);

  EXPECT_EQ(converted->GetVideoInfo().SampleType(), SAMPLE_INT16)
      << "bitmask accepted matching current format must pass clip through without conversion";

  GuardedAudioBuffer output(converted->GetVideoInfo().BytesFromAudioSamples(8), 64, 64, 4);
  converted->GetAudio(output.data(), 0, 8, env.get());
  EXPECT_TRUE(output.memory_intact());
  verify_integer_exact(output, src_bytes, "Pass-through bitmask");
  expect_audio_requests(*source_clip, {{0, 8}});
  expect_audio_source_unchanged(*source_clip, src_bytes);
}

TEST_P(ConvertAudioFactorySuite, ConvertsToPreferredWhenNeitherMatchesCurrent) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, 8, 1});
  const auto src_bytes = make_audio_bytes<std::int16_t>({0, 16384, -16384, 8192, -8192, 4096, -4096, 0});
  auto* source_clip = new AudioSequenceClip(vi, src_bytes);
  PClip source(source_clip);

  AVSValue args[3] = {source, SAMPLE_INT8 | SAMPLE_INT32, SAMPLE_FLOAT};
  AVSValue res = env.get()->Invoke("ConvertAudio", AVSValue(args, 3));
  PClip converted = res.AsClip();
  ASSERT_TRUE(converted);

  EXPECT_EQ(converted->GetVideoInfo().SampleType(), SAMPLE_FLOAT);

  GuardedAudioBuffer output(converted->GetVideoInfo().BytesFromAudioSamples(8), 64, 64, 4);
  converted->GetAudio(output.data(), 0, 8, env.get());
  EXPECT_TRUE(output.memory_intact());
  verify_float_exact(output, {0.0f, 0.5f, -0.5f, 0.25f, -0.25f, 0.125f, -0.125f, 0.0f},
                     "ConvertAudio preferred=float");
  expect_audio_requests(*source_clip, {{0, 8}});
  expect_audio_source_unchanged(*source_clip, src_bytes);
}

TEST_P(ConvertAudioFactorySuite, PassesThroughVideoOnlyClipWithoutAddingAudio) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  const auto vi = make_video_info(VideoInfoSpec{64, 48, VideoInfo::CS_BGR32, 5, 25, 1});
  std::vector<PVideoFrame> frames;
  for (int i = 0; i < 5; ++i) {
    frames.push_back(env.get()->NewVideoFrame(vi));
  }
  auto* source_clip = new FrameSequenceClip(vi, frames);
  PClip source(source_clip);

  AVSValue args[3] = {source, SAMPLE_INT16, SAMPLE_FLOAT};
  AVSValue res = env.get()->Invoke("ConvertAudio", AVSValue(args, 3));
  PClip converted = res.AsClip();
  ASSERT_TRUE(converted);

  EXPECT_FALSE(converted->GetVideoInfo().HasAudio())
      << "clip without audio must not have audio added";
  EXPECT_TRUE(converted->GetVideoInfo().HasVideo());
  EXPECT_EQ(converted->GetVideoInfo().num_frames, 5);

  PVideoFrame frame = converted->GetFrame(0, env.get());
  EXPECT_TRUE(frame);
}

TEST_P(ConvertAudioFactorySuite, IdentityDedicatedCallsPreserveSourceAndSamples) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  struct IdentityCase {
    const char* filter_name;
    int format;
    std::vector<std::uint8_t> payload;
  };

  const std::vector<IdentityCase> id_cases{
      {"ConvertAudioTo8bit", SAMPLE_INT8, {10, 50, 100, 128, 200}},
      {"ConvertAudioTo16bit", SAMPLE_INT16, make_audio_bytes<std::int16_t>({-1000, 0, 1000, 20000, -20000})},
      {"ConvertAudioTo24bit", SAMPLE_INT24, make_s24_le_bytes({-100000, 0, 100000, 5000000, -5000000})},
      {"ConvertAudioTo32bit", SAMPLE_INT32, make_audio_bytes<std::int32_t>({-1000000, 0, 1000000, 1000000000, -1000000000})},
      {"ConvertAudioToFloat", SAMPLE_FLOAT, make_audio_bytes<float>({-0.75f, -0.5f, 0.0f, 0.5f, 0.75f})},
  };

  for (const auto& idc : id_cases) {
    const auto vi = make_audio_video_info(AudioInfoSpec{48000, idc.format, 5, 1});
    auto* source_clip = new AudioSequenceClip(vi, idc.payload);
    PClip source(source_clip);

    AVSValue arg(source);
    AVSValue res = env.get()->Invoke(idc.filter_name, AVSValue(&arg, 1));
    PClip converted = res.AsClip();
    ASSERT_TRUE(converted);

    EXPECT_EQ(converted->GetVideoInfo().SampleType(), idc.format)
        << idc.filter_name << " on same format must preserve sample type";

    GuardedAudioBuffer output(converted->GetVideoInfo().BytesFromAudioSamples(5), 64, 64, 4);
    converted->GetAudio(output.data(), 0, 5, env.get());
    EXPECT_TRUE(output.memory_intact());
    verify_integer_exact(output, idc.payload, idc.filter_name);
    expect_audio_requests(*source_clip, {{0, 5}});
    expect_audio_source_unchanged(*source_clip, idc.payload);
  }
}

INSTANTIATE_TEST_SUITE_P(
    ConvertAudioApi, ConvertAudioFactorySuite,
    ::testing::Values(CpuMode::Native, CpuMode::None),
    CpuModeNameGenerator());

// ---------------------------------------------------------------------------
// 4.2 GetAudio Wrapper Behavior Suite
// ---------------------------------------------------------------------------

class ConvertAudioGetAudioSuite : public ::testing::TestWithParam<CpuMode> {};

TEST_P(ConvertAudioGetAudioSuite, ConvertsPackedFloatAcrossLibraryBlockBoundary) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));
  constexpr int frames = 257;
  constexpr int channels = 2;
  std::vector<float> floats(frames * channels);
  std::vector<int32_t> packed_values(floats.size());
  for (size_t i = 0; i < floats.size(); ++i) {
    const int value = int(i % 17) - 8;
    floats[i] = value / 16.0f;
    packed_values[i] = value * 524288;
  }
  const auto packed = make_s24_le_bytes(packed_values);
  for (bool to_float : {false, true}) {
    const auto vi = make_audio_video_info(AudioInfoSpec{
        48000, to_float ? SAMPLE_INT24 : SAMPLE_FLOAT, frames, channels});
    const auto input = to_float ? packed : make_audio_bytes(floats);
    auto* source_clip = new AudioSequenceClip(vi, input);
    PClip source(source_clip);
    AVSValue arg(source);
    PClip converted = env.get()->Invoke(
        to_float ? "ConvertAudioToFloat" : "ConvertAudioTo24bit", AVSValue(&arg, 1)).AsClip();
    GuardedAudioBuffer output(converted->GetVideoInfo().BytesFromAudioSamples(frames), 64, 64, 4);
    converted->GetAudio(output.data(), 0, frames, env.get());
    if (to_float) verify_float_exact(output, floats, "S24->F32 block boundary");
    else verify_integer_exact(output, packed, "F32->S24 block boundary");
    EXPECT_TRUE(output.memory_intact());
    expect_audio_requests(*source_clip, {{0, frames}});
    expect_audio_source_unchanged(*source_clip, input);
  }
}

TEST_P(ConvertAudioGetAudioSuite, HandlesNonZeroStartInStereoInterleaved) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  // Stereo S16 -> S32, 10 samples total (20 values)
  constexpr int total_samples = 10;
  constexpr int channels = 2;
  constexpr int rate = 44100;

  std::vector<std::int16_t> s16_samples(total_samples * channels);
  for (int s = 0; s < total_samples; ++s) {
    s16_samples[s * 2 + 0] = static_cast<std::int16_t>((s * 2 + 1) * 1000);
    s16_samples[s * 2 + 1] = static_cast<std::int16_t>(-((s * 2 + 2) * 1000));
  }

  const auto vi = make_audio_video_info(AudioInfoSpec{rate, SAMPLE_INT16, total_samples, channels});
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(s16_samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  AVSValue arg(source);
  PClip converted = env.get()->Invoke("ConvertAudioTo32bit", AVSValue(&arg, 1)).AsClip();
  ASSERT_TRUE(converted);

  // Request window [start=3, count=4]
  constexpr std::int64_t start = 3;
  constexpr std::int64_t count = 4;
  const auto bytes_needed = converted->GetVideoInfo().BytesFromAudioSamples(count);
  GuardedAudioBuffer output(bytes_needed, 64, 64, 4);
  converted->GetAudio(output.data(), start, count, env.get());

  EXPECT_TRUE(output.memory_intact());
  expect_audio_requests(*source_clip, {{start, count}});
  expect_audio_source_unchanged(*source_clip, source_before);

  // Multiplication preserves signed scaling without left-shifting negative values.
  std::vector<std::int32_t> expected_s32(count * channels);
  for (std::int64_t i = 0; i < count; ++i) {
    const auto s = start + i;
    expected_s32[i * 2 + 0] = static_cast<std::int32_t>(s16_samples[s * 2 + 0]) * 65536;
    expected_s32[i * 2 + 1] = static_cast<std::int32_t>(s16_samples[s * 2 + 1]) * 65536;
  }
  verify_integer_exact(output, make_audio_bytes(expected_s32), "Stereo S16->S32 non-zero start");
}

TEST_P(ConvertAudioGetAudioSuite, HandlesSixChannelsWithChannelMaskPreservation) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  // 6 channels, S16 -> Float, 15 samples (90 values)
  constexpr int total_samples = 15;
  constexpr int channels = 6;
  constexpr int rate = 48000;
  constexpr unsigned mask = 0x3F; // 5.1 surround: FL, FR, FC, LFE, BL, BR

  auto vi = make_audio_video_info(AudioInfoSpec{rate, SAMPLE_INT16, total_samples, channels});
  vi.SetChannelMask(true, mask);

  std::vector<std::int16_t> s16_samples(total_samples * channels);
  for (int s = 0; s < total_samples; ++s) {
    for (int ch = 0; ch < channels; ++ch) {
      // Choose dyadic values to ensure exact float representation
      s16_samples[s * channels + ch] = static_cast<std::int16_t>((((s + ch) % 5) - 2) * 4096);
    }
  }

  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(s16_samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  AVSValue arg(source);
  PClip converted = env.get()->Invoke("ConvertAudioToFloat", AVSValue(&arg, 1)).AsClip();
  ASSERT_TRUE(converted);

  const auto& vi_dst = converted->GetVideoInfo();
  EXPECT_TRUE(vi_dst.IsChannelMaskKnown());
  EXPECT_EQ(vi_dst.GetChannelMask(), mask);
  EXPECT_EQ(vi_dst.AudioChannels(), 6);
  EXPECT_EQ(vi_dst.audio_samples_per_second, rate);
  EXPECT_EQ(vi_dst.num_audio_samples, total_samples);

  // Request window [start=4, count=5]
  constexpr std::int64_t start = 4;
  constexpr std::int64_t count = 5;
  const auto bytes_needed = vi_dst.BytesFromAudioSamples(count);
  GuardedAudioBuffer output(bytes_needed, 64, 64, 4);
  converted->GetAudio(output.data(), start, count, env.get());

  EXPECT_TRUE(output.memory_intact());
  expect_audio_requests(*source_clip, {{start, count}});
  expect_audio_source_unchanged(*source_clip, source_before);

  std::vector<float> expected_floats(count * channels);
  for (std::int64_t i = 0; i < count; ++i) {
    for (int ch = 0; ch < channels; ++ch) {
      const auto raw = s16_samples[(start + i) * channels + ch];
      expected_floats[i * channels + ch] = raw / 32768.0f;
    }
  }
  verify_float_exact(output, expected_floats, "6-channel S16->Float non-zero start");
}

TEST_P(ConvertAudioGetAudioSuite, ReusesAndGrowsTemporaryBufferAcrossShortLongShortRequests) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  // 1 channel, S16 -> Float, 32 samples total
  constexpr int total_samples = 32;
  std::vector<std::int16_t> s16_samples(total_samples);
  for (int i = 0; i < total_samples; ++i) {
    s16_samples[i] = static_cast<std::int16_t>((i - 16) * 1024);
  }

  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, total_samples, 1});
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(s16_samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  AVSValue arg(source);
  PClip converted = env.get()->Invoke("ConvertAudioToFloat", AVSValue(&arg, 1)).AsClip();
  ASSERT_TRUE(converted);

  // Request 1: short (start=2, count=4)
  {
    GuardedAudioBuffer out1(converted->GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 4);
    converted->GetAudio(out1.data(), 2, 4, env.get());
    EXPECT_TRUE(out1.memory_intact());
    std::vector<float> exp1(4);
    for (int i = 0; i < 4; ++i) exp1[i] = s16_samples[2 + i] / 32768.0f;
    verify_float_exact(out1, exp1, "Req 1 short");
  }

  // Request 2: long (start=0, count=16) -> triggers tempbuffer growth
  {
    GuardedAudioBuffer out2(converted->GetVideoInfo().BytesFromAudioSamples(16), 64, 64, 4);
    converted->GetAudio(out2.data(), 0, 16, env.get());
    EXPECT_TRUE(out2.memory_intact());
    std::vector<float> exp2(16);
    for (int i = 0; i < 16; ++i) exp2[i] = s16_samples[i] / 32768.0f;
    verify_float_exact(out2, exp2, "Req 2 long");
  }

  // Request 3: short (start=8, count=4) -> reuses grown tempbuffer
  {
    GuardedAudioBuffer out3(converted->GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 4);
    converted->GetAudio(out3.data(), 8, 4, env.get());
    EXPECT_TRUE(out3.memory_intact());
    std::vector<float> exp3(4);
    for (int i = 0; i < 4; ++i) exp3[i] = s16_samples[8 + i] / 32768.0f;
    verify_float_exact(out3, exp3, "Req 3 short reused");
  }

  expect_audio_requests(*source_clip, {{2, 4}, {0, 16}, {8, 4}});
  expect_audio_source_unchanged(*source_clip, source_before);
}

TEST_P(ConvertAudioGetAudioSuite, ProducesConsistentOutputBetweenChunkedAndFullAndReverseReads) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  // 1 channel, S32 -> S16, 20 samples
  constexpr int total_samples = 20;
  std::vector<std::int32_t> s32_samples(total_samples);
  for (int i = 0; i < total_samples; ++i) {
    s32_samples[i] = static_cast<std::int32_t>((i - 10) * 100000000LL);
  }

  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT32, total_samples, 1});

  // Single full read of [0, 12)
  auto* src_full = new AudioSequenceClip(vi, make_audio_bytes(s32_samples));
  PClip clip_full(src_full);
  AVSValue arg_full(clip_full);
  PClip conv_full = env.get()->Invoke("ConvertAudioTo16bit", AVSValue(&arg_full, 1)).AsClip();

  GuardedAudioBuffer full_buf(conv_full->GetVideoInfo().BytesFromAudioSamples(12), 64, 64, 4);
  conv_full->GetAudio(full_buf.data(), 0, 12, env.get());
  EXPECT_TRUE(full_buf.memory_intact());

  // Chunked reads [0, 4), [4, 5), [9, 3) on a second instance
  auto* src_chunk = new AudioSequenceClip(vi, make_audio_bytes(s32_samples));
  PClip clip_chunk(src_chunk);
  AVSValue arg_chunk(clip_chunk);
  PClip conv_chunk = env.get()->Invoke("ConvertAudioTo16bit", AVSValue(&arg_chunk, 1)).AsClip();

  std::vector<std::uint8_t> assembled(12 * sizeof(std::int16_t));
  GuardedAudioBuffer chunk1(conv_chunk->GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 4);
  conv_chunk->GetAudio(chunk1.data(), 0, 4, env.get());
  std::memcpy(assembled.data(), chunk1.data(), chunk1.active_bytes());

  GuardedAudioBuffer chunk2(conv_chunk->GetVideoInfo().BytesFromAudioSamples(5), 64, 64, 4);
  conv_chunk->GetAudio(chunk2.data(), 4, 5, env.get());
  std::memcpy(assembled.data() + 4 * sizeof(std::int16_t), chunk2.data(), chunk2.active_bytes());

  GuardedAudioBuffer chunk3(conv_chunk->GetVideoInfo().BytesFromAudioSamples(3), 64, 64, 4);
  conv_chunk->GetAudio(chunk3.data(), 9, 3, env.get());
  std::memcpy(assembled.data() + 9 * sizeof(std::int16_t), chunk3.data(), chunk3.active_bytes());

  EXPECT_TRUE(chunk1.memory_intact());
  EXPECT_TRUE(chunk2.memory_intact());
  EXPECT_TRUE(chunk3.memory_intact());
  verify_integer_exact(full_buf, assembled, "Chunked vs full read");

  // Reverse window requests on the same chunked instance: [8, 4), [4, 4), [0, 4)
  GuardedAudioBuffer rev1(conv_chunk->GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 4);
  conv_chunk->GetAudio(rev1.data(), 8, 4, env.get());
  EXPECT_TRUE(rev1.memory_intact());
  for (std::size_t i = 0; i < rev1.active_bytes(); ++i) {
    EXPECT_EQ(rev1.data()[i], full_buf.data()[8 * sizeof(std::int16_t) + i]);
  }

  GuardedAudioBuffer rev2(conv_chunk->GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 4);
  conv_chunk->GetAudio(rev2.data(), 4, 4, env.get());
  EXPECT_TRUE(rev2.memory_intact());
  for (std::size_t i = 0; i < rev2.active_bytes(); ++i) {
    EXPECT_EQ(rev2.data()[i], full_buf.data()[4 * sizeof(std::int16_t) + i]);
  }

  GuardedAudioBuffer rev3(conv_chunk->GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 4);
  conv_chunk->GetAudio(rev3.data(), 0, 4, env.get());
  EXPECT_TRUE(rev3.memory_intact());
  for (std::size_t i = 0; i < rev3.active_bytes(); ++i) {
    EXPECT_EQ(rev3.data()[i], full_buf.data()[i]);
  }
}

TEST_P(ConvertAudioGetAudioSuite, ExecutesTwoStageBufferingSequenceForS24ToFloat) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  // S24 -> F32: 16 samples
  constexpr int total_samples = 16;
  std::vector<std::int32_t> s24_values(total_samples);
  for (int i = 0; i < total_samples; ++i) {
    s24_values[i] = (i - 8) * 524288; // powers of 2 for exact float
  }

  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT24, total_samples, 1});
  auto* source_clip = new AudioSequenceClip(vi, make_s24_le_bytes(s24_values));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  AVSValue arg(source);
  PClip converted = env.get()->Invoke("ConvertAudioToFloat", AVSValue(&arg, 1)).AsClip();
  ASSERT_TRUE(converted);

  // Request 1: [start=1, count=5]
  {
    GuardedAudioBuffer out1(converted->GetVideoInfo().BytesFromAudioSamples(5), 64, 64, 4);
    converted->GetAudio(out1.data(), 1, 5, env.get());
    EXPECT_TRUE(out1.memory_intact());
    std::vector<float> exp1(5);
    for (int i = 0; i < 5; ++i) exp1[i] = s24_values[1 + i] / 8388608.0f;
    verify_float_exact(out1, exp1, "S24->Float stage req 1");
  }

  // Request 2: [start=7, count=6]
  {
    GuardedAudioBuffer out2(converted->GetVideoInfo().BytesFromAudioSamples(6), 64, 64, 4);
    converted->GetAudio(out2.data(), 7, 6, env.get());
    EXPECT_TRUE(out2.memory_intact());
    std::vector<float> exp2(6);
    for (int i = 0; i < 6; ++i) exp2[i] = s24_values[7 + i] / 8388608.0f;
    verify_float_exact(out2, exp2, "S24->Float stage req 2");
  }

  expect_audio_requests(*source_clip, {{1, 5}, {7, 6}});
  expect_audio_source_unchanged(*source_clip, source_before);
}

TEST_P(ConvertAudioGetAudioSuite, ExecutesTwoStageBufferingSequenceForFloatToS24) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  // F32 -> S24: 16 samples
  constexpr int total_samples = 16;
  std::vector<float> f32_values(total_samples);
  for (int i = 0; i < total_samples; ++i) {
    f32_values[i] = (i - 8) * 0.0625f; // multiples of 1/16, exact representation
  }
  // -0.5 of an S24 LSB becomes S32 -128, whose high 24 bits encode -1.
  // Direct scaling to S24 followed by truncation would incorrectly yield zero.
  f32_values[2] = -1.0f / 16777216.0f;
  const std::vector<std::int32_t> expected_s24{
      -4194304, -3670016, -1, -2621440, -2097152, -1572864, -1048576, -524288,
      0, 524288, 1048576, 1572864, 2097152, 2621440, 3145728, 3670016};

  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT24, total_samples, 1});
  // Note: source clip is Float
  const auto vi_float = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_FLOAT, total_samples, 1});
  auto* source_clip = new AudioSequenceClip(vi_float, make_audio_bytes(f32_values));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  AVSValue arg(source);
  PClip converted = env.get()->Invoke("ConvertAudioTo24bit", AVSValue(&arg, 1)).AsClip();
  ASSERT_TRUE(converted);

  // Request 1: [start=2, count=4]
  {
    GuardedAudioBuffer out1(converted->GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 4);
    converted->GetAudio(out1.data(), 2, 4, env.get());
    EXPECT_TRUE(out1.memory_intact());
    std::vector<std::int32_t> exp_s24(4);
    for (int i = 0; i < 4; ++i) {
      exp_s24[i] = expected_s24[2 + i];
    }
    verify_integer_exact(out1, make_s24_le_bytes(exp_s24), "Float->S24 stage req 1");
  }

  // Request 2: [start=6, count=5]
  {
    GuardedAudioBuffer out2(converted->GetVideoInfo().BytesFromAudioSamples(5), 64, 64, 4);
    converted->GetAudio(out2.data(), 6, 5, env.get());
    EXPECT_TRUE(out2.memory_intact());
    std::vector<std::int32_t> exp_s24(5);
    for (int i = 0; i < 5; ++i) {
      exp_s24[i] = expected_s24[6 + i];
    }
    verify_integer_exact(out2, make_s24_le_bytes(exp_s24), "Float->S24 stage req 2");
  }

  expect_audio_requests(*source_clip, {{2, 4}, {6, 5}});
  expect_audio_source_unchanged(*source_clip, source_before);
}

TEST_P(ConvertAudioGetAudioSuite, ConvertsNonVectorMultipleTotalSampleCount) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  // 17 samples (prime number, not a multiple of 2, 4, 8, 16)
  constexpr int count = 17;
  std::vector<std::int16_t> samples(count);
  for (int i = 0; i < count; ++i) {
    samples[i] = static_cast<std::int16_t>((i - 8) * 2048);
  }

  const auto vi = make_audio_video_info(AudioInfoSpec{44100, SAMPLE_INT16, count, 1});
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  AVSValue arg(source);
  PClip converted = env.get()->Invoke("ConvertAudioToFloat", AVSValue(&arg, 1)).AsClip();
  ASSERT_TRUE(converted);

  GuardedAudioBuffer output(converted->GetVideoInfo().BytesFromAudioSamples(count), 64, 64, 4);
  converted->GetAudio(output.data(), 0, count, env.get());

  EXPECT_TRUE(output.memory_intact());
  expect_audio_requests(*source_clip, {{0, count}});
  expect_audio_source_unchanged(*source_clip, source_before);

  std::vector<float> expected(count);
  for (int i = 0; i < count; ++i) {
    expected[i] = samples[i] / 32768.0f;
  }
  verify_float_exact(output, expected, "17 samples S16->Float");
}

INSTANTIATE_TEST_SUITE_P(
    ConvertAudioApi, ConvertAudioGetAudioSuite,
    ::testing::Values(CpuMode::Native, CpuMode::None),
    CpuModeNameGenerator());

// ---------------------------------------------------------------------------
// 4.3 Video Pass-through Suite
// ---------------------------------------------------------------------------

class ConvertAudioVideoPassThroughSuite : public ::testing::TestWithParam<CpuMode> {};

TEST_P(ConvertAudioVideoPassThroughSuite, PreservesVideoGeometryTimingPixelsAndFrameProperties) {
  AviSynthEnvironment env;
  ASSERT_NO_FATAL_FAILURE(configure_cpu_mode(env.get(), GetParam()));

  constexpr int width = 32;
  constexpr int height = 24;
  constexpr int pixel_type = VideoInfo::CS_BGR32;
  constexpr int num_frames = 2;
  constexpr unsigned fps_num = 25;
  constexpr unsigned fps_den = 1;

  constexpr int audio_rate = 48000;
  constexpr int audio_channels = 2;
  constexpr std::int64_t audio_samples = 100;

  VideoInfo vi{};
  vi.width = width;
  vi.height = height;
  vi.pixel_type = pixel_type;
  vi.num_frames = num_frames;
  vi.fps_numerator = fps_num;
  vi.fps_denominator = fps_den;
  vi.audio_samples_per_second = audio_rate;
  vi.sample_type = SAMPLE_INT16;
  vi.num_audio_samples = audio_samples;
  vi.nchannels = audio_channels;
  vi.SetChannelMask(false, 0);

  // Create two video frames with deterministic pixels and explicit frame properties
  std::vector<PVideoFrame> frames;
  frames.reserve(num_frames);

  PVideoFrame frame0 = env.get()->NewVideoFrame(vi);
  write_frame_plane<std::uint32_t>(frame0, DEFAULT_PLANE, [](int x, int y) {
    return static_cast<std::uint32_t>(0xFF000000U | (x * 7 + y * 13));
  });
  set_frame_property_int(env.get(), frame0, "TestProp", 42);
  set_frame_property_int(env.get(), frame0, "CustomIndex", 1001);
  frames.push_back(frame0);

  PVideoFrame frame1 = env.get()->NewVideoFrame(vi);
  write_frame_plane<std::uint32_t>(frame1, DEFAULT_PLANE, [](int x, int y) {
    return static_cast<std::uint32_t>(0xFF112233U | ((x + y) & 0xFF));
  });
  set_frame_property_int(env.get(), frame1, "TestProp", 99);
  set_frame_property_int(env.get(), frame1, "CustomIndex", 1002);
  frames.push_back(frame1);

  // Audio payload: 100 samples of stereo S16
  std::vector<std::int16_t> audio_raw(audio_samples * audio_channels);
  for (std::size_t i = 0; i < audio_raw.size(); ++i) {
    audio_raw[i] = static_cast<std::int16_t>((static_cast<int>(i % 16) - 8) * 2048);
  }
  const auto audio_bytes = make_audio_bytes(audio_raw);

  auto* source_clip = new FrameSequenceClip(vi, frames, audio_bytes);
  PClip source(source_clip);

  AVSValue arg(source);
  PClip converted = env.get()->Invoke("ConvertAudioToFloat", AVSValue(&arg, 1)).AsClip();
  ASSERT_TRUE(converted);

  // Verify VideoInfo geometry and timing
  const auto& vi_dst = converted->GetVideoInfo();
  EXPECT_TRUE(vi_dst.HasVideo());
  EXPECT_EQ(vi_dst.width, width);
  EXPECT_EQ(vi_dst.height, height);
  EXPECT_EQ(vi_dst.pixel_type, pixel_type);
  EXPECT_EQ(vi_dst.num_frames, num_frames);
  EXPECT_EQ(vi_dst.fps_numerator, fps_num);
  EXPECT_EQ(vi_dst.fps_denominator, fps_den);

  // Verify Audio metadata updated
  EXPECT_TRUE(vi_dst.HasAudio());
  EXPECT_EQ(vi_dst.SampleType(), SAMPLE_FLOAT);
  EXPECT_EQ(vi_dst.AudioChannels(), audio_channels);
  EXPECT_EQ(vi_dst.audio_samples_per_second, audio_rate);
  EXPECT_EQ(vi_dst.num_audio_samples, audio_samples);

  // Verify Frame 0 pixels and properties
  PVideoFrame out_frame0 = converted->GetFrame(0, env.get());
  ASSERT_TRUE(out_frame0);
  EXPECT_EQ(get_frame_property_int(env.get(), out_frame0, "TestProp"), 42);
  EXPECT_EQ(get_frame_property_int(env.get(), out_frame0, "CustomIndex"), 1001);

  const auto pixels0 = read_frame_plane_active<std::uint32_t>(out_frame0, DEFAULT_PLANE);
  const auto expected_pixels0 = read_frame_plane_active<std::uint32_t>(frame0, DEFAULT_PLANE);
  EXPECT_EQ(pixels0, expected_pixels0);

  // Verify Frame 1 pixels and properties
  PVideoFrame out_frame1 = converted->GetFrame(1, env.get());
  ASSERT_TRUE(out_frame1);
  EXPECT_EQ(get_frame_property_int(env.get(), out_frame1, "TestProp"), 99);
  EXPECT_EQ(get_frame_property_int(env.get(), out_frame1, "CustomIndex"), 1002);

  const auto pixels1 = read_frame_plane_active<std::uint32_t>(out_frame1, DEFAULT_PLANE);
  const auto expected_pixels1 = read_frame_plane_active<std::uint32_t>(frame1, DEFAULT_PLANE);
  EXPECT_EQ(pixels1, expected_pixels1);

  // Verify audio reading on the same video-and-audio clip
  GuardedAudioBuffer audio_out(converted->GetVideoInfo().BytesFromAudioSamples(10), 64, 64, 4);
  converted->GetAudio(audio_out.data(), 5, 10, env.get());
  EXPECT_TRUE(audio_out.memory_intact());

  std::vector<float> exp_audio(10 * audio_channels);
  for (int i = 0; i < 10 * audio_channels; ++i) {
    exp_audio[i] = audio_raw[5 * audio_channels + i] / 32768.0f;
  }
  verify_float_exact(audio_out, exp_audio, "FrameSequenceClip audio");
}

INSTANTIATE_TEST_SUITE_P(
    ConvertAudioApi, ConvertAudioVideoPassThroughSuite,
    ::testing::Values(CpuMode::Native, CpuMode::None),
    CpuModeNameGenerator());

}  // namespace
