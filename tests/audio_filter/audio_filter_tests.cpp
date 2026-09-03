#include <avisynth.h>

#ifndef AVS_UNUSED
#define AVS_UNUSED(x) (void)(x)
#define AVSUT_AUDIO_FILTER_UNDEF_AVS_UNUSED
#endif
#include "core/internal.h"
#include "core/audio.h"
#include "convert/convert_audio.h"
#include "filters/edit.h"
#include "filters/fps.h"
#ifdef AVSUT_AUDIO_FILTER_UNDEF_AVS_UNUSED
#undef AVS_UNUSED
#undef AVSUT_AUDIO_FILTER_UNDEF_AVS_UNUSED
#endif

#include "support/audio_test_helpers.h"
#include "support/video_filter_test_support.h"
#include "support/avisynth_environment.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace {

using avsut::test::AudioBoundsPolicy;
using avsut::test::AudioInfoSpec;
using avsut::test::AudioSequenceClip;
using avsut::test::AviSynthEnvironment;
using avsut::test::expect_audio_requests;
using avsut::test::expect_audio_source_unchanged;
using avsut::test::expect_exact_audio;
using avsut::test::expect_float_audio;
using avsut::test::fill_plane_full_pitch;
using avsut::test::FrameSequenceClip;
using avsut::test::FrameSnapshot;
using avsut::test::GuardedAudioBuffer;
using avsut::test::make_audio_bytes;
using avsut::test::make_audio_video_info;
using avsut::test::make_video_info;
using avsut::test::read_audio_sample;
using avsut::test::VideoInfoSpec;

int amplify_int16_reference(std::int16_t sample, int factor) {
  const auto scaled = (static_cast<std::int64_t>(sample) * factor + 65536) >> 17;
  return static_cast<int>(std::clamp<std::int64_t>(scaled, std::numeric_limits<std::int16_t>::min(),
                                                   std::numeric_limits<std::int16_t>::max()));
}

std::int16_t mix_int16_reference(std::int16_t first, std::int16_t second, int first_factor,
                                 int second_factor) {
  const auto scaled = (static_cast<std::int64_t>(first) * first_factor +
                       static_cast<std::int64_t>(second) * second_factor + 65536) >>
                      17;
  return static_cast<std::int16_t>(std::clamp<std::int64_t>(
      scaled, std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max()));
}

template <typename Sample>
void expect_audio_buffers_equal(const GuardedAudioBuffer& expected,
                                const GuardedAudioBuffer& actual, float tolerance = 0.0F) {
  ASSERT_EQ(expected.active_bytes(), actual.active_bytes());
  const auto sample_count = expected.active_bytes() / sizeof(Sample);
  for (std::size_t index = 0; index < sample_count; ++index) {
    const Sample expected_value = read_audio_sample<Sample>(expected, index);
    const Sample actual_value = read_audio_sample<Sample>(actual, index);
    if constexpr (std::is_floating_point_v<Sample>) {
      ASSERT_TRUE(std::isfinite(expected_value)) << "sample=" << index;
      ASSERT_TRUE(std::isfinite(actual_value)) << "sample=" << index;
      EXPECT_NEAR(actual_value, expected_value, tolerance) << "sample=" << index;
    } else {
      EXPECT_EQ(actual_value, expected_value) << "sample=" << index;
    }
  }
}

TEST(ConvertAudioFilter, ConvertsSigned16ToFloatForRequestedInterleavedWindow) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, 4, 2});
  const std::vector<std::int16_t> samples{0, 32767, -32768, 16384, -16384, 1, 8192, -8192};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  ConvertAudio filter(source, SAMPLE_FLOAT);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(2), 64, 64, 1);
  filter.GetAudio(output.data(), 1, 2, environment.get());

  expect_float_audio(output, {-1.0F, 16384.0F / 32768.0F, -16384.0F / 32768.0F, 1.0F / 32768.0F});
  EXPECT_EQ(filter.GetVideoInfo().SampleType(), SAMPLE_FLOAT);
  EXPECT_EQ(filter.GetVideoInfo().AudioChannels(), 2);
  expect_audio_requests(*source_clip, {{1, 2}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), 0);
  EXPECT_TRUE(output.memory_intact());
}

TEST(ConvertAudioFilter, ConvertsFloatToSigned16WithEndpointClamping) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{44100, SAMPLE_FLOAT, 5, 1});
  const std::vector<float> samples{-1.25F, -1.0F, -0.5F, 0.5F, 1.25F};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  ConvertAudio filter(source, SAMPLE_INT16);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(5), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 5, environment.get());

  expect_exact_audio<std::int16_t>(output, {-32768, -32768, -16384, 16384, 32767});
  EXPECT_EQ(filter.GetVideoInfo().SampleType(), SAMPLE_INT16);
  expect_audio_requests(*source_clip, {{0, 5}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_TRUE(output.memory_intact());
}

TEST(ConvertToMonoFilter, AveragesAllFloatChannelsAndSetsMonoLayout) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_FLOAT, 3, 3});
  const std::vector<float> samples{0.3F, 0.0F, -0.3F, 1.0F, 0.5F, 0.0F, -0.5F, -0.25F, 0.25F};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  ConvertToMono filter(source);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(2), 64, 64, 1);
  filter.GetAudio(output.data(), 1, 2, environment.get());

  expect_float_audio(output, {0.5F, (-0.5F - 0.25F + 0.25F) / 3.0F});
  EXPECT_EQ(filter.GetVideoInfo().AudioChannels(), 1);
  EXPECT_TRUE(filter.GetVideoInfo().IsChannelMaskKnown());
  EXPECT_EQ(filter.GetVideoInfo().GetChannelMask(), AVS_CHANNEL_LAYOUT_MONO);
  expect_audio_requests(*source_clip, {{1, 2}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_SERIALIZED);
  EXPECT_TRUE(output.memory_intact());
}

TEST(GetChannelFilter, SelectsRequestedChannelsInOrderForSigned16) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, 3, 3});
  const std::vector<std::int16_t> samples{1, 2, 3, 4, 5, 6, 7, 8, 9};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();
  auto* selected_channels = new int[2]{2, 0};

  GetChannel filter(source, selected_channels, 2);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(3), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 3, environment.get());

  expect_exact_audio<std::int16_t>(output, {3, 1, 6, 4, 9, 7});
  EXPECT_EQ(filter.GetVideoInfo().AudioChannels(), 2);
  EXPECT_TRUE(filter.GetVideoInfo().IsChannelMaskKnown());
  EXPECT_EQ(filter.GetVideoInfo().GetChannelMask(), AVS_CHANNEL_LAYOUT_STEREO);
  expect_audio_requests(*source_clip, {{0, 3}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_SERIALIZED);
  EXPECT_TRUE(output.memory_intact());
}

TEST(MergeChannelsFilter, InterleavesMonoSourcesAfterConvertingTheSecondFormat) {
  AviSynthEnvironment environment;
  const auto int16_vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, 3, 1});
  const auto float_vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_FLOAT, 3, 1});
  const std::vector<std::int16_t> first_samples{100, -200, 300};
  const std::vector<float> second_samples{0.5F, -0.25F, 1.25F};
  auto* first_clip = new AudioSequenceClip(int16_vi, make_audio_bytes(first_samples));
  auto* second_clip = new AudioSequenceClip(float_vi, make_audio_bytes(second_samples));
  PClip first(first_clip);
  PClip second(second_clip);
  const auto first_before = first_clip->audio();
  const auto second_before = second_clip->audio();

  auto* children = new PClip[2];
  children[0] = first;
  children[1] = second;
  MergeChannels filter(first, 2, children, environment.get());
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(3), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 3, environment.get());

  expect_exact_audio<std::int16_t>(output, {100, 16384, -200, -8192, 300, 32767});
  EXPECT_EQ(filter.GetVideoInfo().AudioChannels(), 2);
  EXPECT_TRUE(filter.GetVideoInfo().IsChannelMaskKnown());
  EXPECT_EQ(filter.GetVideoInfo().GetChannelMask(), AVS_CHANNEL_LAYOUT_STEREO);
  expect_audio_requests(*first_clip, {{0, 3}});
  expect_audio_requests(*second_clip, {{0, 3}});
  expect_audio_source_unchanged(*first_clip, first_before);
  expect_audio_source_unchanged(*second_clip, second_before);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_SERIALIZED);
  EXPECT_TRUE(output.memory_intact());
}

TEST(DelayAudioFilter, ZeroFillsTheDelayedPrefixAndOffsetsTheChildRequest) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{4, SAMPLE_FLOAT, 4, 1});
  const std::vector<float> samples{0.1F, 0.2F, 0.3F, 0.4F};
  auto* source_clip =
      new AudioSequenceClip(vi, make_audio_bytes(samples), AudioBoundsPolicy::ZeroFill);
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  DelayAudio filter(0.5, source);
  ASSERT_EQ(filter.GetVideoInfo().num_audio_samples, 6);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 4, environment.get());

  expect_float_audio(output, {0.0F, 0.0F, 0.1F, 0.2F});
  expect_audio_requests(*source_clip, {{-2, 4}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_TRUE(output.memory_intact());
}

TEST(AmplifyFilter, AppliesPerChannelIntegerFactorsWithSaturation) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, 3, 2});
  const std::vector<std::int16_t> samples{20000, -20000, -32768, 32767, 1000, -1000};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();
  auto* volumes = new float[2]{2.0F, 0.5F};
  auto* factors = new int[2]{static_cast<int>(2.0F * 131072.0F + 0.5F),
                             static_cast<int>(0.5F * 131072.0F + 0.5F)};

  Amplify filter(source, volumes, factors);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(3), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 3, environment.get());

  const std::vector<std::int16_t> expected{
      static_cast<std::int16_t>(amplify_int16_reference(samples[0], factors[0])),
      static_cast<std::int16_t>(amplify_int16_reference(samples[1], factors[1])),
      static_cast<std::int16_t>(amplify_int16_reference(samples[2], factors[0])),
      static_cast<std::int16_t>(amplify_int16_reference(samples[3], factors[1])),
      static_cast<std::int16_t>(amplify_int16_reference(samples[4], factors[0])),
      static_cast<std::int16_t>(amplify_int16_reference(samples[5], factors[1]))};
  expect_exact_audio<std::int16_t>(output, expected);
  expect_audio_requests(*source_clip, {{0, 3}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_TRUE(output.memory_intact());
}

TEST(AmplifyFilter, AppliesPerChannelFloatFactorsWithoutClamping) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_FLOAT, 2, 2});
  const std::vector<float> samples{0.75F, -0.75F, -0.5F, 0.5F};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();
  auto* volumes = new float[2]{2.0F, 0.5F};
  auto* factors = new int[2]{static_cast<int>(2.0F * 131072.0F + 0.5F),
                             static_cast<int>(0.5F * 131072.0F + 0.5F)};

  Amplify filter(source, volumes, factors);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(2), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 2, environment.get());

  expect_float_audio(output, {1.5F, -0.375F, -1.0F, 0.25F});
  expect_audio_requests(*source_clip, {{0, 2}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_TRUE(output.memory_intact());
}

TEST(MixAudioFilter, MixesFloatTracksWithIndependentFactors) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_FLOAT, 3, 2});
  const std::vector<float> first_samples{0.8F, 0.2F, -0.4F, 0.6F, 0.0F, -0.8F};
  const std::vector<float> second_samples{-0.2F, 0.4F, 0.6F, -0.6F, 1.0F, 0.2F};
  auto* first_clip = new AudioSequenceClip(vi, make_audio_bytes(first_samples));
  auto* second_clip = new AudioSequenceClip(vi, make_audio_bytes(second_samples));
  PClip first(first_clip);
  PClip second(second_clip);
  const auto first_before = first_clip->audio();
  const auto second_before = second_clip->audio();

  MixAudio filter(first, second, 0.25, 0.75, environment.get());
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(3), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 3, environment.get());

  expect_float_audio(output, {0.05F, 0.35F, 0.35F, -0.3F, 0.75F, -0.05F});
  expect_audio_requests(*first_clip, {{0, 3}});
  expect_audio_requests(*second_clip, {{0, 3}});
  expect_audio_source_unchanged(*first_clip, first_before);
  expect_audio_source_unchanged(*second_clip, second_before);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_SERIALIZED);
  EXPECT_TRUE(output.memory_intact());
}

TEST(MixAudioFilter, SaturatesSigned16Results) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, 2, 1});
  const std::vector<std::int16_t> first_samples{30000, -30000};
  const std::vector<std::int16_t> second_samples{30000, -30000};
  auto* first_clip = new AudioSequenceClip(vi, make_audio_bytes(first_samples));
  auto* second_clip = new AudioSequenceClip(vi, make_audio_bytes(second_samples));
  PClip first(first_clip);
  PClip second(second_clip);

  MixAudio filter(first, second, 0.75, 0.75, environment.get());
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(2), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 2, environment.get());

  const int factor = static_cast<int>(0.75 * 131072.0 + 0.5);
  expect_exact_audio<std::int16_t>(output, {mix_int16_reference(30000, 30000, factor, factor),
                                            mix_int16_reference(-30000, -30000, factor, factor)});
  expect_audio_requests(*first_clip, {{0, 2}});
  expect_audio_requests(*second_clip, {{0, 2}});
  EXPECT_TRUE(output.memory_intact());
}

TEST(NormalizeFilter, ScansFloatStreamBeforeNormalizingRequestedWindow) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_FLOAT, 4, 1});
  const std::vector<float> samples{0.25F, -0.5F, 0.125F, 0.0F};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  Normalize filter(source, 1.0F, false);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(2), 64, 64, 1);
  filter.GetAudio(output.data(), 1, 2, environment.get());

  expect_float_audio(output, {-1.0F, 0.25F});
  expect_audio_requests(*source_clip, {{0, 4}, {1, 2}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_SERIALIZED);
  EXPECT_TRUE(output.memory_intact());
}

TEST(NormalizeFilter, ScansSigned16PeaksAndSaturatesOutput) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_INT16, 3, 1});
  const std::vector<std::int16_t> samples{-16384, 8192, 4096};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  Normalize filter(source, 1.0F, false);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(3), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 3, environment.get());

  expect_exact_audio<std::int16_t>(output, {-32768, 16384, 8192});
  expect_audio_requests(*source_clip, {{0, 3}, {0, 3}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_TRUE(output.memory_intact());
}

TEST(EnsureVBRMP3SyncFilter, ReplaysSkippedAndRewoundAudioBeforeServingOutput) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{44100, SAMPLE_FLOAT, 10, 1});
  const std::vector<float> samples{0.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F, 0.9F};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  EnsureVBRMP3Sync filter(source);
  GuardedAudioBuffer first(vi.BytesFromAudioSamples(4), 64, 64, 1);
  GuardedAudioBuffer skipped(vi.BytesFromAudioSamples(2), 64, 64, 1);
  GuardedAudioBuffer rewound(vi.BytesFromAudioSamples(2), 64, 64, 1);

  filter.GetAudio(first.data(), 0, 4, environment.get());
  filter.GetAudio(skipped.data(), 8, 2, environment.get());
  filter.GetAudio(rewound.data(), 2, 2, environment.get());

  expect_float_audio(first, {0.0F, 0.1F, 0.2F, 0.3F});
  expect_float_audio(skipped, {0.8F, 0.9F});
  expect_float_audio(rewound, {0.2F, 0.3F});
  expect_audio_requests(*source_clip, {{0, 4}, {4, 4}, {8, 2}, {0, 2}, {2, 2}});
  EXPECT_EQ(filter.SetCacheHints(CACHE_GETCHILD_AUDIO_MODE, 0), CACHE_AUDIO);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GETCHILD_AUDIO_SIZE, 0), 1024 * 1024);
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_SERIALIZED);
  EXPECT_TRUE(first.memory_intact());
  EXPECT_TRUE(skipped.memory_intact());
  EXPECT_TRUE(rewound.memory_intact());
}

TEST(AssumeRateFilter, ChangesOnlyTheDeclaredSampleRate) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{44100, SAMPLE_FLOAT, 3, 2});
  const std::vector<float> samples{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();

  AssumeRate filter(source, 48000);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(2), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 2, environment.get());

  expect_float_audio(output, {0.1F, 0.2F, 0.3F, 0.4F});
  EXPECT_EQ(filter.GetVideoInfo().SamplesPerSecond(), 48000);
  EXPECT_EQ(filter.GetVideoInfo().num_audio_samples, 3);
  expect_audio_requests(*source_clip, {{0, 2}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_TRUE(output.memory_intact());
}

TEST(SetChannelMaskFilter, SetsAndClearsAudioChannelMaskMetadata) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_FLOAT, 2, 2});
  const std::vector<float> samples{0.1F, 0.2F, 0.3F, 0.4F};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);

  SetChannelMask known_filter(source, true, AVS_CHANNEL_LAYOUT_STEREO);
  EXPECT_TRUE(known_filter.GetVideoInfo().IsChannelMaskKnown());
  EXPECT_EQ(known_filter.GetVideoInfo().GetChannelMask(), AVS_CHANNEL_LAYOUT_STEREO);

  GuardedAudioBuffer output(known_filter.GetVideoInfo().BytesFromAudioSamples(2), 64, 64, 1);
  known_filter.GetAudio(output.data(), 0, 2, environment.get());
  expect_float_audio(output, {0.1F, 0.2F, 0.3F, 0.4F});

  SetChannelMask unknown_filter(source, false, 0);
  EXPECT_FALSE(unknown_filter.GetVideoInfo().IsChannelMaskKnown());
  EXPECT_EQ(unknown_filter.GetVideoInfo().GetChannelMask(), 0U);
  expect_audio_requests(*source_clip, {{0, 2}});
  EXPECT_TRUE(output.memory_intact());
}

TEST(KillAudioFilter, RemovesAudioMetadataWithoutRequestingTheSource) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{44100, SAMPLE_FLOAT, 2, 1});
  const std::vector<float> samples{0.25F, -0.5F};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  KillAudio filter(source);

  EXPECT_FALSE(filter.GetVideoInfo().HasAudio());
  EXPECT_EQ(filter.GetVideoInfo().AudioChannels(), 0);
  EXPECT_EQ(filter.GetVideoInfo().num_audio_samples, 0);
  GuardedAudioBuffer output(8, 64, 64, 1);
  output.fill_active(0x5A);
  filter.GetAudio(output.data(), 0, 2, environment.get());
  EXPECT_EQ(output.snapshot_active(), std::vector<std::uint8_t>(8, 0x5A));
  EXPECT_TRUE(source_clip->audio_requests().empty());
  EXPECT_TRUE(output.memory_intact());
}

TEST(KillVideoFilter, RemovesVideoMetadataWhilePreservingAudio) {
  AviSynthEnvironment environment;
  auto vi = make_audio_video_info(AudioInfoSpec{44100, SAMPLE_FLOAT, 3, 1});
  vi.width = 4;
  vi.height = 2;
  vi.pixel_type = VideoInfo::CS_Y8;
  vi.num_frames = 1;
  vi.fps_numerator = 25;
  vi.fps_denominator = 1;
  const std::vector<float> samples{0.25F, -0.5F, 0.75F};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  const auto source_before = source_clip->audio();
  KillVideo filter(source);

  EXPECT_FALSE(filter.GetVideoInfo().HasVideo());
  EXPECT_EQ(filter.GetVideoInfo().SamplesPerSecond(), 44100);
  EXPECT_EQ(filter.GetVideoInfo().num_audio_samples, 3);

  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(3), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 3, environment.get());
  expect_float_audio(output, samples);
  expect_audio_requests(*source_clip, {{0, 3}});
  expect_audio_source_unchanged(*source_clip, source_before);
  EXPECT_TRUE(output.memory_intact());
}

TEST(AudioEditFilters, TrimsAudioByTimeInLengthMode) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{10, SAMPLE_FLOAT, 8, 1});
  const std::vector<float> samples{0.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  Trim filter(0.2, 0.3, source, Trim::Length, false, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().num_audio_samples, 3);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(3), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 3, environment.get());

  expect_float_audio(output, {0.2F, 0.3F, 0.4F});
  expect_audio_requests(*source_clip, {{2, 3}});
  EXPECT_EQ(filter.SetCacheHints(CACHE_DONT_CACHE_ME, 0), 1);
  EXPECT_TRUE(output.memory_intact());
}

TEST(AudioEditFilters, SplicesAudioAtTheSampleBoundary) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{10, SAMPLE_FLOAT, 4, 1});
  auto* first_clip =
      new AudioSequenceClip(vi, make_audio_bytes(std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}));
  auto* second_clip =
      new AudioSequenceClip(vi, make_audio_bytes(std::vector<float>{10.0F, 20.0F, 30.0F, 40.0F}));
  PClip first(first_clip);
  PClip second(second_clip);
  Splice filter(first, second, false, true, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().num_audio_samples, 8);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 1);
  filter.GetAudio(output.data(), 2, 4, environment.get());

  expect_float_audio(output, {3.0F, 4.0F, 10.0F, 20.0F});
  expect_audio_requests(*first_clip, {{2, 2}});
  expect_audio_requests(*second_clip, {{0, 2}});
  EXPECT_EQ(filter.SetCacheHints(CACHE_GET_MTMODE, 0), MT_NICE_FILTER);
  EXPECT_TRUE(output.memory_intact());
}

TEST(AudioEditFilters, DissolvesAudioOnlyClipsWithAStableRamp) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{10, SAMPLE_FLOAT, 8, 1});
  const std::vector<float> first_samples(8, 1.0F);
  const std::vector<float> second_samples(8, -1.0F);
  auto* first_clip = new AudioSequenceClip(vi, make_audio_bytes(first_samples));
  auto* second_clip = new AudioSequenceClip(vi, make_audio_bytes(second_samples));
  PClip first(first_clip);
  PClip second(second_clip);
  Dissolve filter(first, second, 2, 5.0, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().num_audio_samples, 12);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 1);
  filter.GetAudio(output.data(), 4, 4, environment.get());

  for (int index = 0; index < 4; ++index) {
    const float expected =
        index == 0 ? 1.0F
                   : (index == 3 ? -1.0F : -1.0F + static_cast<float>(3 - index) * 2.0F / 3.0F);
    EXPECT_NEAR(read_audio_sample<float>(output, static_cast<std::size_t>(index)), expected,
                1.0e-6F)
        << "sample=" << index;
  }
  expect_audio_requests(*first_clip, {{4, 4}});
  expect_audio_requests(*second_clip, {{0, 4}});
  EXPECT_TRUE(output.memory_intact());
}

TEST(AudioEditFilters, AudioDubUsesVideoGeometryAndAudioSamples) {
  AviSynthEnvironment environment;
  auto video_vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_FLOAT, 1, 1});
  video_vi.width = 4;
  video_vi.height = 2;
  video_vi.pixel_type = VideoInfo::CS_Y8;
  video_vi.num_frames = 1;
  video_vi.fps_numerator = 25;
  video_vi.fps_denominator = 1;
  auto audio_vi = make_audio_video_info(AudioInfoSpec{44100, SAMPLE_INT16, 3, 2});
  auto* video_clip = new AudioSequenceClip(video_vi, make_audio_bytes(std::vector<float>{0.0F}));
  auto* audio_clip = new AudioSequenceClip(
      audio_vi, make_audio_bytes(std::vector<std::int16_t>{100, -100, 200, -200, 300, -300}));
  PClip video(video_clip);
  PClip audio(audio_clip);
  AudioDub filter(video, audio, 0, environment.get());

  EXPECT_TRUE(filter.GetVideoInfo().HasVideo());
  EXPECT_EQ(filter.GetVideoInfo().width, 4);
  EXPECT_EQ(filter.GetVideoInfo().SamplesPerSecond(), 44100);
  EXPECT_EQ(filter.GetVideoInfo().SampleType(), SAMPLE_INT16);
  EXPECT_EQ(filter.GetVideoInfo().AudioChannels(), 2);

  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(3), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 3, environment.get());
  expect_exact_audio<std::int16_t>(output, {100, -100, 200, -200, 300, -300});
  expect_audio_requests(*audio_clip, {{0, 3}});
  EXPECT_TRUE(video_clip->audio_requests().empty());
  EXPECT_TRUE(output.memory_intact());
}

TEST(AudioEditFilters, ReversesInterleavedAudioFrames) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{10, SAMPLE_INT16, 4, 2});
  const std::vector<std::int16_t> samples{10, 100, 20, 200, 30, 300, 40, 400};
  auto* source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip source(source_clip);
  Reverse filter(source);

  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 4, environment.get());

  expect_exact_audio<std::int16_t>(output, {40, 400, 30, 300, 20, 200, 10, 100});
  expect_audio_requests(*source_clip, {{0, 4}});
  EXPECT_TRUE(output.memory_intact());
}

TEST(AudioEditFilters, LoopsAnAudioOnlyClipAcrossBoundaries) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{10, SAMPLE_FLOAT, 4, 1});
  auto* source_clip =
      new AudioSequenceClip(vi, make_audio_bytes(std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}));
  PClip source(source_clip);
  Loop filter(source, 3, 0, 10000000, environment.get());

  ASSERT_EQ(filter.GetVideoInfo().num_audio_samples, 12);
  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(10), 64, 64, 1);
  filter.GetAudio(output.data(), 1, 10, environment.get());

  expect_float_audio(output, {2.0F, 3.0F, 4.0F, 1.0F, 2.0F, 3.0F, 4.0F, 1.0F, 2.0F, 3.0F});
  expect_audio_requests(*source_clip, {{1, 3}, {0, 4}, {0, 3}});
  EXPECT_TRUE(output.memory_intact());
}

TEST(AudioMetadataFilter, SynchronizesAudioMetadataWithAssumeFPSVariants) {
  AviSynthEnvironment environment;
  auto vi = make_audio_video_info(AudioInfoSpec{48000, SAMPLE_FLOAT, 3, 1});
  vi.fps_numerator = 25;
  vi.fps_denominator = 1;
  const std::vector<float> samples{0.1F, 0.2F, 0.3F};
  auto* fps_source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  auto* scaled_source_clip = new AudioSequenceClip(vi, make_audio_bytes(samples));
  PClip fps_source(fps_source_clip);
  PClip scaled_source(scaled_source_clip);
  AssumeFPS fps_filter(fps_source, 50, 1, true, environment.get());
  AssumeScaledFPS scaled_filter(scaled_source, 2, 1, true, environment.get());

  EXPECT_EQ(fps_filter.GetVideoInfo().SamplesPerSecond(), 96000);
  EXPECT_EQ(fps_filter.GetVideoInfo().fps_numerator, 50U);
  EXPECT_EQ(scaled_filter.GetVideoInfo().SamplesPerSecond(), 96000);
  EXPECT_EQ(scaled_filter.GetVideoInfo().fps_numerator, 50U);

  GuardedAudioBuffer fps_output(fps_filter.GetVideoInfo().BytesFromAudioSamples(2), 64, 64, 1);
  GuardedAudioBuffer scaled_output(scaled_filter.GetVideoInfo().BytesFromAudioSamples(2), 64, 64,
                                   1);
  fps_filter.GetAudio(fps_output.data(), 0, 2, environment.get());
  scaled_filter.GetAudio(scaled_output.data(), 0, 2, environment.get());
  expect_float_audio(fps_output, {0.1F, 0.2F});
  expect_float_audio(scaled_output, {0.1F, 0.2F});
  expect_audio_requests(*fps_source_clip, {{0, 2}});
  expect_audio_requests(*scaled_source_clip, {{0, 2}});
  EXPECT_TRUE(fps_output.memory_intact());
  EXPECT_TRUE(scaled_output.memory_intact());
}

TEST(ResampleAudioFilter, ProducesContinuousFloatOutputAcrossChunkedRequests) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{4, SAMPLE_FLOAT, 8, 1});
  const std::vector<float> samples{0.0F, 0.2F, -0.1F, 0.4F, -0.3F, 0.6F, -0.5F, 0.8F};
  auto* one_shot_source =
      new AudioSequenceClip(vi, make_audio_bytes(samples), AudioBoundsPolicy::ZeroFill);
  auto* chunked_source =
      new AudioSequenceClip(vi, make_audio_bytes(samples), AudioBoundsPolicy::ZeroFill);
  PClip one_shot_clip(one_shot_source);
  PClip chunked_clip(chunked_source);
  ResampleAudio one_shot(one_shot_clip, 6, 1, environment.get());
  ResampleAudio chunked(chunked_clip, 6, 1, environment.get());

  ASSERT_EQ(one_shot.GetVideoInfo().SamplesPerSecond(), 6);
  const auto output_samples = one_shot.GetVideoInfo().num_audio_samples;
  ASSERT_EQ(output_samples, 12);
  GuardedAudioBuffer expected(one_shot.GetVideoInfo().BytesFromAudioSamples(output_samples), 64, 64,
                              1);
  GuardedAudioBuffer actual(chunked.GetVideoInfo().BytesFromAudioSamples(output_samples), 64, 64,
                            1);
  one_shot.GetAudio(expected.data(), 0, output_samples, environment.get());
  chunked.GetAudio(actual.data(), 0, 5, environment.get());
  chunked.GetAudio(actual.data() + 5 * sizeof(float), 5, output_samples - 5, environment.get());

  // The legacy resampler accumulates float filter roundoff independently for each request.
  expect_audio_buffers_equal<float>(expected, actual, 1.0e-4F);
  EXPECT_EQ(one_shot.SetCacheHints(CACHE_GET_MTMODE, 0), MT_SERIALIZED);
  EXPECT_EQ(chunked.SetCacheHints(CACHE_GET_MTMODE, 0), MT_SERIALIZED);
  EXPECT_TRUE(expected.memory_intact());
  EXPECT_TRUE(actual.memory_intact());
  EXPECT_FALSE(one_shot_source->audio_requests().empty());
  EXPECT_FALSE(chunked_source->audio_requests().empty());
}

TEST(ResampleAudioFilter, ProducesContinuousSigned16OutputAcrossChunkedRequests) {
  AviSynthEnvironment environment;
  const auto vi = make_audio_video_info(AudioInfoSpec{4, SAMPLE_INT16, 8, 1});
  const std::vector<std::int16_t> samples{0, 1000, -1000, 2000, -2000, 3000, -3000, 4000};
  auto* one_shot_source =
      new AudioSequenceClip(vi, make_audio_bytes(samples), AudioBoundsPolicy::ZeroFill);
  auto* chunked_source =
      new AudioSequenceClip(vi, make_audio_bytes(samples), AudioBoundsPolicy::ZeroFill);
  PClip one_shot_clip(one_shot_source);
  PClip chunked_clip(chunked_source);
  ResampleAudio one_shot(one_shot_clip, 6, 1, environment.get());
  ResampleAudio chunked(chunked_clip, 6, 1, environment.get());

  ASSERT_EQ(one_shot.GetVideoInfo().SampleType(), SAMPLE_INT16);
  const auto output_samples = one_shot.GetVideoInfo().num_audio_samples;
  ASSERT_EQ(output_samples, 12);
  GuardedAudioBuffer expected(one_shot.GetVideoInfo().BytesFromAudioSamples(output_samples), 64, 64,
                              1);
  GuardedAudioBuffer actual(chunked.GetVideoInfo().BytesFromAudioSamples(output_samples), 64, 64,
                            1);
  one_shot.GetAudio(expected.data(), 0, output_samples, environment.get());
  chunked.GetAudio(actual.data(), 0, 5, environment.get());
  chunked.GetAudio(actual.data() + 5 * sizeof(std::int16_t), 5, output_samples - 5,
                   environment.get());

  expect_audio_buffers_equal<std::int16_t>(expected, actual);
  EXPECT_TRUE(expected.memory_intact());
  EXPECT_TRUE(actual.memory_intact());
  EXPECT_FALSE(one_shot_source->audio_requests().empty());
  EXPECT_FALSE(chunked_source->audio_requests().empty());
}

TEST(NormalizeFilter, ShowsAmplifyFactorOnVideoFrameAfterPeakScan) {
  AviSynthEnvironment environment;
  // Video-plus-audio clip: showvalues overlays text only after the peak scan.
  auto video = make_video_info(VideoInfoSpec{64, 32, VideoInfo::CS_YV12, 2, 25, 1});
  video.audio_samples_per_second = 48000;
  video.sample_type = SAMPLE_FLOAT;
  video.nchannels = 1;
  video.num_audio_samples = 4;
  video.SetChannelMask(false, 0);

  PVideoFrame frame0 = environment.get()->NewVideoFrame(video);
  PVideoFrame frame1 = environment.get()->NewVideoFrame(video);
  for (const int plane : {PLANAR_Y, PLANAR_U, PLANAR_V}) {
    fill_plane_full_pitch(frame0, plane == PLANAR_Y ? 0x20 : 0x80, plane);
    fill_plane_full_pitch(frame1, plane == PLANAR_Y ? 0x20 : 0x80, plane);
  }
  const auto frame0_before = FrameSnapshot::capture(frame0, video);
  const auto frame1_before = FrameSnapshot::capture(frame1, video);

  // Peak at sample 2 maps to frame 1 for 48000 Hz / 25 fps (1920 samples/frame).
  // Use a short clip with explicit sample-to-frame mapping via FramesFromAudioSamples.
  const std::vector<float> samples{0.25F, -0.5F, 0.5F, 0.125F};
  // Ensure FramesFromAudioSamples(peak_index) is well-defined for this fixture.
  auto* source_clip = new avsut::test::FrameSequenceClip(
      video, std::vector<PVideoFrame>{frame0, frame1}, make_audio_bytes(samples));
  const PClip source(source_clip);

  Normalize filter(source, 1.0F, true);
  EXPECT_EQ(filter.GetVideoInfo().HasVideo(), true);
  EXPECT_EQ(filter.GetVideoInfo().HasAudio(), true);

  // Before any GetAudio, GetFrame reports the pending-scan message path.
  const PVideoFrame pending = filter.GetFrame(0, environment.get());
  EXPECT_NE(pending->CheckMemory(), 1);
  EXPECT_FALSE(FrameSnapshot::capture(pending, video) == frame0_before);

  GuardedAudioBuffer output(filter.GetVideoInfo().BytesFromAudioSamples(4), 64, 64, 1);
  filter.GetAudio(output.data(), 0, 4, environment.get());
  // Peak magnitude is 0.5, so factor becomes 1.0 / 0.5 = 2.0.
  expect_float_audio(output, {0.5F, -1.0F, 1.0F, 0.25F}, 1e-6F);

  const PVideoFrame annotated = filter.GetFrame(0, environment.get());
  EXPECT_NE(annotated->CheckMemory(), 1);
  EXPECT_FALSE(FrameSnapshot::capture(annotated, video) == frame0_before);
  // After the peak scan the overlay text differs from the pending message.
  EXPECT_FALSE(FrameSnapshot::capture(annotated, video) == FrameSnapshot::capture(pending, video));
  // Source frames remain intact; Normalize annotates a writable copy.
  EXPECT_EQ(FrameSnapshot::capture(frame0, video), frame0_before);
  EXPECT_EQ(FrameSnapshot::capture(frame1, video), frame1_before);
  EXPECT_FALSE(source_clip->audio_requests().empty());
}

}  // namespace
