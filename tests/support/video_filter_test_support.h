#pragma once

#include "support/audio_sequence_clip.h"
#include "support/avisynth_environment.h"
#include "support/compat_375.h"

#include <avisynth.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace avsut::test {

struct VideoInfoSpec {
  int width;
  int height;
  int pixel_type;
  int num_frames;
  unsigned fps_numerator;
  unsigned fps_denominator;
};

inline VideoInfo make_video_info(const VideoInfoSpec& spec) {
  if (spec.width <= 0 || spec.height <= 0) {
    throw std::invalid_argument("video dimensions must be positive");
  }
  if (spec.num_frames <= 0) {
    throw std::invalid_argument("video frame count must be positive");
  }
  if (spec.fps_numerator == 0 || spec.fps_denominator == 0) {
    throw std::invalid_argument("video frame rate must be positive");
  }

  VideoInfo video_info{};
  video_info.width = spec.width;
  video_info.height = spec.height;
  video_info.pixel_type = spec.pixel_type;
  video_info.num_frames = spec.num_frames;
  video_info.fps_numerator = spec.fps_numerator;
  video_info.fps_denominator = spec.fps_denominator;
  return video_info;
}

struct CacheHintRequest {
  int cache_hint{};
  int frame_range{};

  friend bool operator==(const CacheHintRequest& lhs, const CacheHintRequest& rhs) {
    return lhs.cache_hint == rhs.cache_hint && lhs.frame_range == rhs.frame_range;
  }
};

struct FramePlaneGeometry {
  int width{};
  int height{};
  int row_size{};
  int pitch{};
};

inline FramePlaneGeometry frame_plane_geometry(const PVideoFrame& frame, int plane,
                                               std::size_t component_size) {
  if (!frame || component_size == 0) {
    throw std::invalid_argument("frame plane requires a non-null frame and component size");
  }
  const int row_size = frame->GetRowSize(plane);
  const int height = frame->GetHeight(plane);
  const int pitch = frame->GetPitch(plane);
  if (row_size < 0 || height < 0 || pitch < row_size ||
      static_cast<std::size_t>(row_size) % component_size != 0) {
    throw std::invalid_argument("frame plane has invalid typed geometry");
  }
  return FramePlaneGeometry{row_size / static_cast<int>(component_size), height, row_size, pitch};
}

template <typename Pixel, typename ValueFunction>
void write_frame_plane(PVideoFrame& frame, int plane, ValueFunction&& value_at) {
  const auto geometry = frame_plane_geometry(frame, plane, sizeof(Pixel));
  for (int y = 0; y < geometry.height; ++y) {
    auto* row = reinterpret_cast<Pixel*>(frame->GetWritePtr(plane) + y * geometry.pitch);
    for (int x = 0; x < geometry.width; ++x) {
      row[x] = static_cast<Pixel>(value_at(x, y));
    }
  }
}

template <typename Pixel>
std::vector<Pixel> read_frame_plane_active(const PVideoFrame& frame, int plane) {
  const auto geometry = frame_plane_geometry(frame, plane, sizeof(Pixel));
  std::vector<Pixel> values;
  values.reserve(static_cast<std::size_t>(geometry.width) * geometry.height);
  for (int y = 0; y < geometry.height; ++y) {
    const auto* row = reinterpret_cast<const Pixel*>(frame->GetReadPtr(plane) + y * geometry.pitch);
    values.insert(values.end(), row, row + geometry.width);
  }
  return values;
}

inline void set_frame_property_int(IScriptEnvironment* environment, PVideoFrame& frame,
                                   const char* key, int value) {
  if (environment == nullptr || frame == nullptr) {
    throw std::invalid_argument("frame property requires an environment and frame");
  }
  AVSMap* properties = environment->getFramePropsRW(frame);
  if (properties == nullptr ||
      environment->propSetInt(properties, key, value, PROPAPPENDMODE_REPLACE) != 0) {
    throw std::runtime_error("failed to set frame property");
  }
}

inline std::optional<int> get_frame_property_int(IScriptEnvironment* environment,
                                                 const PVideoFrame& frame, const char* key) {
  if (environment == nullptr || frame == nullptr) {
    throw std::invalid_argument("frame property requires an environment and frame");
  }
  const AVSMap* properties = environment->getFramePropsRO(frame);
  if (properties == nullptr || environment->propNumElements(properties, key) <= 0) {
    return std::nullopt;
  }
  int error = 0;
  const int64_t value = environment->propGetInt(properties, key, 0, &error);
  if (error != 0) {
    throw std::runtime_error("failed to read frame property");
  }
  return static_cast<int>(value);
}

inline std::vector<int> video_frame_planes(const VideoInfo& video_info) {
  if (video_info.IsPlanarRGBA()) {
    return {PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A};
  }
  if (video_info.IsPlanarRGB()) {
    return {PLANAR_G, PLANAR_B, PLANAR_R};
  }
  if (video_info.IsYUVA()) {
    return {PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A};
  }
  if (video_info.IsYUV()) {
    return {PLANAR_Y, PLANAR_U, PLANAR_V};
  }
  if (video_info.IsY()) {
    return {PLANAR_Y};
  }
  return {DEFAULT_PLANE};
}

class FrameSequenceClip : public IClip {
 public:
  FrameSequenceClip(VideoInfo video_info, std::vector<PVideoFrame> frames,
                    std::vector<std::uint8_t> audio = {})
      : video_info_(video_info), frames_(std::move(frames)), audio_(std::move(audio)) {
    if (frames_.empty()) {
      throw std::invalid_argument("frame sequence must not be empty");
    }
    if (frames_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        video_info_.num_frames != static_cast<int>(frames_.size())) {
      throw std::invalid_argument("frame count does not match video info");
    }
    for (const auto& frame : frames_) {
      if (!frame) {
        throw std::invalid_argument("frame sequence contains a null frame");
      }
    }
    if (video_info_.HasAudio()) {
      if (video_info_.BytesPerAudioSample() <= 0) {
        throw std::invalid_argument("audio-enabled video clip must have a valid audio format");
      }
      const auto expected_bytes = video_info_.BytesFromAudioSamples(video_info_.num_audio_samples);
      if (expected_bytes < 0) {
        throw std::invalid_argument("audio data size does not match video info");
      }
      if (audio_.empty()) {
        // Construction-only fixtures may declare audio without providing payload.
        audio_.assign(static_cast<std::size_t>(expected_bytes), 0);
      } else if (audio_.size() != static_cast<std::size_t>(expected_bytes)) {
        throw std::invalid_argument("audio data size does not match video info");
      }
    } else if (!audio_.empty()) {
      throw std::invalid_argument("audio payload requires HasAudio video info");
    }
  }

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment*) override {
    validate_frame_index(n);
    frame_requests_.push_back(n);
    return frames_[static_cast<std::size_t>(n)];
  }

  bool __stdcall GetParity(int n) override {
    validate_frame_index(n);
    parity_requests_.push_back(n);
    return false;
  }

  void __stdcall GetAudio(void* buffer, std::int64_t start, std::int64_t count,
                          IScriptEnvironment*) override {
    if (!video_info_.HasAudio()) {
      throw std::logic_error("video test clip received an unexpected audio request");
    }
    if (count < 0 || start > std::numeric_limits<std::int64_t>::max() - count) {
      throw std::out_of_range("audio request has invalid bounds");
    }
    if (buffer == nullptr && count != 0) {
      throw std::invalid_argument("audio destination must not be null");
    }
    if (start < 0 || start > video_info_.num_audio_samples ||
        count > video_info_.num_audio_samples - start) {
      throw std::out_of_range("audio request is outside the clip");
    }

    audio_requests_.push_back(AudioRequest{start, count});
    const auto requested_bytes = video_info_.BytesFromAudioSamples(count);
    if (requested_bytes != 0) {
      std::memset(buffer, 0, static_cast<std::size_t>(requested_bytes));
      const auto source_offset = video_info_.BytesFromAudioSamples(start);
      std::memcpy(buffer, audio_.data() + source_offset, static_cast<std::size_t>(requested_bytes));
    }
  }

  int __stdcall SetCacheHints(int cachehints, int frame_range) override {
    cache_hint_requests_.push_back(CacheHintRequest{cachehints, frame_range});
    return 0;
  }

  const VideoInfo& __stdcall GetVideoInfo() override { return video_info_; }

  const std::vector<int>& frame_requests() const noexcept { return frame_requests_; }
  const std::vector<int>& parity_requests() const noexcept { return parity_requests_; }
  const std::vector<CacheHintRequest>& cache_hint_requests() const noexcept {
    return cache_hint_requests_;
  }
  const std::vector<AudioRequest>& audio_requests() const noexcept { return audio_requests_; }
  const std::vector<std::uint8_t>& audio() const noexcept { return audio_; }

 protected:
  void validate_frame_index(int n) const {
    if (n < 0 || static_cast<std::size_t>(n) >= frames_.size()) {
      throw std::out_of_range("frame index is outside the clip");
    }
  }

 private:
  VideoInfo video_info_{};
  std::vector<PVideoFrame> frames_;
  std::vector<std::uint8_t> audio_;
  std::vector<int> frame_requests_;
  std::vector<int> parity_requests_;
  std::vector<CacheHintRequest> cache_hint_requests_;
  std::vector<AudioRequest> audio_requests_;
};

class StaticFrameClip final : public FrameSequenceClip {
 public:
  StaticFrameClip(VideoInfo video_info, PVideoFrame frame, std::vector<std::uint8_t> audio = {})
      : FrameSequenceClip(video_info, std::vector<PVideoFrame>{std::move(frame)},
                          std::move(audio)) {}
};

struct FramePlaneSnapshot {
  int plane{};
  int pitch{};
  int row_size{};
  int height{};
  std::vector<std::uint8_t> bytes;

  friend bool operator==(const FramePlaneSnapshot& lhs, const FramePlaneSnapshot& rhs) {
    return lhs.plane == rhs.plane && lhs.pitch == rhs.pitch && lhs.row_size == rhs.row_size &&
           lhs.height == rhs.height && lhs.bytes == rhs.bytes;
  }
};

class FrameSnapshot {
 public:
  static FrameSnapshot capture(const PVideoFrame& frame, const VideoInfo& video_info) {
    if (!frame) {
      throw std::invalid_argument("cannot snapshot a null frame");
    }

    FrameSnapshot snapshot;
    for (const int plane : video_frame_planes(video_info)) {
      snapshot.planes_.push_back(capture_plane(frame, plane));
    }
    return snapshot;
  }

  const std::vector<FramePlaneSnapshot>& planes() const noexcept { return planes_; }

  friend bool operator==(const FrameSnapshot& lhs, const FrameSnapshot& rhs) {
    return lhs.planes_ == rhs.planes_;
  }

 private:
  static FramePlaneSnapshot capture_plane(const PVideoFrame& frame, int plane) {
    const int pitch = frame->GetPitch(plane);
    const int row_size = frame->GetRowSize(plane);
    const int height = frame->GetHeight(plane);
    if (pitch < 0 || row_size < 0 || height < 0 || row_size > pitch) {
      throw std::invalid_argument("frame plane has invalid geometry");
    }
    const auto pitch_size = static_cast<std::size_t>(pitch);
    const auto height_size = static_cast<std::size_t>(height);
    if (height_size != 0 && pitch_size > std::numeric_limits<std::size_t>::max() / height_size) {
      throw std::overflow_error("frame plane snapshot size overflows");
    }

    FramePlaneSnapshot snapshot{plane, pitch, row_size, height,
                                std::vector<std::uint8_t>(pitch_size * height_size)};
    const auto* source = frame->GetReadPtr(plane);
    for (int y = 0; y < height; ++y) {
      std::memcpy(snapshot.bytes.data() + static_cast<std::size_t>(y) * pitch_size,
                  source + static_cast<std::size_t>(y) * pitch_size, pitch_size);
    }
    return snapshot;
  }

  std::vector<FramePlaneSnapshot> planes_;
};

inline void fill_plane_full_pitch(PVideoFrame& frame, std::uint8_t value, int plane) {
  const int pitch = frame->GetPitch(plane);
  const int height = frame->GetHeight(plane);
  if (pitch < 0 || height < 0) {
    throw std::invalid_argument("frame plane has invalid geometry");
  }
  for (int y = 0; y < height; ++y) {
    std::memset(frame->GetWritePtr(plane) + static_cast<std::size_t>(y) * pitch, value,
                static_cast<std::size_t>(pitch));
  }
}

}  // namespace avsut::test
