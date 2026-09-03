#pragma once

#include "support/audio_sequence_clip.h"
#include "support/guarded_audio_buffer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace avsut::test {

template <typename Sample>
Sample read_audio_sample(const GuardedAudioBuffer& buffer, std::size_t index) {
  Sample value{};
  std::memcpy(&value, buffer.data() + index * sizeof(Sample), sizeof(Sample));
  return value;
}

template <typename Sample>
void expect_exact_audio(const GuardedAudioBuffer& actual, const std::vector<Sample>& expected) {
  ASSERT_EQ(actual.active_bytes(), expected.size() * sizeof(Sample));
  for (std::size_t index = 0; index < expected.size(); ++index) {
    EXPECT_EQ(read_audio_sample<Sample>(actual, index), expected[index]) << "sample=" << index;
  }
}

inline void expect_float_audio(const GuardedAudioBuffer& actual, const std::vector<float>& expected,
                               float tolerance = 1.0e-6F) {
  ASSERT_EQ(actual.active_bytes(), expected.size() * sizeof(float));
  for (std::size_t index = 0; index < expected.size(); ++index) {
    const float value = read_audio_sample<float>(actual, index);
    ASSERT_TRUE(std::isfinite(value)) << "sample=" << index;
    EXPECT_NEAR(value, expected[index], tolerance) << "sample=" << index;
  }
}

inline void expect_audio_requests(const AudioSequenceClip& source,
                                  std::initializer_list<AudioRequest> expected) {
  EXPECT_EQ(source.audio_requests(), std::vector<AudioRequest>(expected));
}

inline void expect_audio_source_unchanged(const AudioSequenceClip& source,
                                          const std::vector<std::uint8_t>& before) {
  EXPECT_EQ(source.audio(), before);
}

}  // namespace avsut::test
