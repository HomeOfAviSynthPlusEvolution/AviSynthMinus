#pragma once

#include <avisynth.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace avsut::test {

struct AudioInfoSpec {
  int sample_rate;
  int sample_type;
  std::int64_t sample_count;
  int channels;
};

inline VideoInfo make_audio_video_info(const AudioInfoSpec& spec) {
  if (spec.sample_rate <= 0 || spec.sample_count < 0 || spec.channels <= 0) {
    throw std::invalid_argument("audio information must contain positive rate and channels");
  }

  VideoInfo video_info{};
  video_info.width = 0;
  video_info.height = 0;
  video_info.pixel_type = VideoInfo::CS_UNKNOWN;
  video_info.num_frames = 0;
  video_info.fps_numerator = 1;
  video_info.fps_denominator = 1;
  video_info.audio_samples_per_second = spec.sample_rate;
  video_info.sample_type = spec.sample_type;
  video_info.num_audio_samples = spec.sample_count;
  video_info.nchannels = spec.channels;
  video_info.SetChannelMask(false, 0);
  return video_info;
}

template <typename Sample>
std::vector<std::uint8_t> make_audio_bytes(const std::vector<Sample>& samples) {
  static_assert(std::is_trivially_copyable_v<Sample>);
  std::vector<std::uint8_t> bytes(samples.size() * sizeof(Sample));
  if (!bytes.empty()) {
    std::memcpy(bytes.data(), samples.data(), bytes.size());
  }
  return bytes;
}

struct AudioRequest {
  std::int64_t start{};
  std::int64_t count{};

  friend bool operator==(const AudioRequest& lhs, const AudioRequest& rhs) {
    return lhs.start == rhs.start && lhs.count == rhs.count;
  }
};

struct AudioCacheHintRequest {
  int cache_hint{};
  int frame_range{};

  friend bool operator==(const AudioCacheHintRequest& lhs, const AudioCacheHintRequest& rhs) {
    return lhs.cache_hint == rhs.cache_hint && lhs.frame_range == rhs.frame_range;
  }
};

enum class AudioBoundsPolicy {
  Reject,
  ZeroFill,
};

class AudioSequenceClip final : public IClip {
 public:
  AudioSequenceClip(VideoInfo video_info, std::vector<std::uint8_t> audio,
                    AudioBoundsPolicy bounds_policy = AudioBoundsPolicy::Reject)
      : video_info_(video_info), audio_(std::move(audio)), bounds_policy_(bounds_policy) {
    if (!video_info_.HasAudio() || video_info_.BytesPerAudioSample() <= 0) {
      throw std::invalid_argument("audio clip must have a valid audio format");
    }
    const auto expected_bytes = video_info_.BytesFromAudioSamples(video_info_.num_audio_samples);
    if (expected_bytes < 0 || audio_.size() != static_cast<std::size_t>(expected_bytes)) {
      throw std::invalid_argument("audio data size does not match video info");
    }
  }

  PVideoFrame __stdcall GetFrame(int, IScriptEnvironment*) override {
    throw std::logic_error("audio test clip received an unexpected frame request");
  }

  bool __stdcall GetParity(int) override {
    throw std::logic_error("audio test clip received an unexpected parity request");
  }

  void __stdcall GetAudio(void* buffer, std::int64_t start, std::int64_t count,
                          IScriptEnvironment*) override {
    if (count < 0 || start > std::numeric_limits<std::int64_t>::max() - count) {
      throw std::out_of_range("audio request has invalid bounds");
    }
    if (buffer == nullptr && count != 0) {
      throw std::invalid_argument("audio destination must not be null");
    }
    if (bounds_policy_ == AudioBoundsPolicy::Reject &&
        (start < 0 || start > video_info_.num_audio_samples ||
         count > video_info_.num_audio_samples - start)) {
      throw std::out_of_range("audio request is outside the clip");
    }

    requests_.push_back(AudioRequest{start, count});
    const auto requested_bytes = video_info_.BytesFromAudioSamples(count);
    if (requested_bytes != 0) {
      std::memset(buffer, 0, static_cast<std::size_t>(requested_bytes));
    }
    if (count == 0) {
      return;
    }

    const auto request_end = start + count;
    const auto copy_start = std::max<std::int64_t>(start, 0);
    const auto copy_end = std::min<std::int64_t>(request_end, video_info_.num_audio_samples);
    if (copy_start >= copy_end) {
      return;
    }

    const auto source_offset = video_info_.BytesFromAudioSamples(copy_start);
    const auto destination_offset = video_info_.BytesFromAudioSamples(copy_start - start);
    const auto copy_bytes = video_info_.BytesFromAudioSamples(copy_end - copy_start);
    std::memcpy(static_cast<std::uint8_t*>(buffer) + destination_offset,
                audio_.data() + source_offset, static_cast<std::size_t>(copy_bytes));
  }

  int __stdcall SetCacheHints(int cachehints, int frame_range) override {
    cache_hint_requests_.push_back(AudioCacheHintRequest{cachehints, frame_range});
    return 0;
  }

  const VideoInfo& __stdcall GetVideoInfo() override { return video_info_; }

  const std::vector<std::uint8_t>& audio() const noexcept { return audio_; }
  const std::vector<AudioRequest>& audio_requests() const noexcept { return requests_; }
  const std::vector<AudioCacheHintRequest>& cache_hint_requests() const noexcept {
    return cache_hint_requests_;
  }

 private:
  VideoInfo video_info_{};
  std::vector<std::uint8_t> audio_;
  AudioBoundsPolicy bounds_policy_{};
  std::vector<AudioRequest> requests_;
  std::vector<AudioCacheHintRequest> cache_hint_requests_;
};

}  // namespace avsut::test
