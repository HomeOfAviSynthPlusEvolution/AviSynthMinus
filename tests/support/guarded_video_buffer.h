#pragma once

#include "support/plane_view.h"
#include "support/compat_375.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace avsut::test {

// Different allocation padding prevents copied tail data from looking intact.
inline std::atomic<std::uint32_t> guarded_buffer_padding_sequence{0};

inline std::uint8_t next_guarded_buffer_padding_sentinel() noexcept {
  const auto sequence = guarded_buffer_padding_sequence.fetch_add(1, std::memory_order_relaxed);
  return static_cast<std::uint8_t>((sequence * 73U + 0x5AU) & 0xffU);
}

template <typename T>
class GuardedVideoBuffer {
 public:
  GuardedVideoBuffer(std::size_t width, std::size_t height, std::size_t pitch_bytes,
                     std::size_t alignment = 64, std::size_t alignment_offset = 0,
                     std::uint8_t padding_sentinel = next_guarded_buffer_padding_sentinel())
      : width_(width),
        height_(height),
        pitch_bytes_(pitch_bytes),
        alignment_(alignment),
        alignment_offset_(alignment_offset),
        padding_sentinel_(padding_sentinel) {
    if (alignment_ == 0 || (alignment_ & (alignment_ - 1)) != 0) {
      throw std::invalid_argument("alignment must be a power of two");
    }
    if (alignment_offset_ >= alignment_) {
      throw std::invalid_argument("alignment offset must be smaller than alignment");
    }
    if (width_ > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::invalid_argument("active row size overflows");
    }
    active_row_bytes_ = width_ * sizeof(T);
    if (pitch_bytes_ < active_row_bytes_) {
      throw std::invalid_argument("pitch is smaller than active row");
    }
    if (height_ != 0 && pitch_bytes_ > std::numeric_limits<std::size_t>::max() / height_) {
      throw std::invalid_argument("allocation size overflows");
    }
    payload_bytes_ = pitch_bytes_ * height_;
    const auto extra = 2 * kGuardBytes + alignment_ + alignment_offset_;
    if (payload_bytes_ > std::numeric_limits<std::size_t>::max() - extra) {
      throw std::invalid_argument("allocation size overflows");
    }
    storage_.resize(payload_bytes_ + extra);
    const auto base = reinterpret_cast<std::uintptr_t>(storage_.data() + kGuardBytes);
    const auto aligned = (base + alignment_ - 1) & ~(alignment_ - 1);
    data_ = reinterpret_cast<std::uint8_t*>(aligned) + alignment_offset_;
    prefix_guard_ = data_ - kGuardBytes;
    suffix_guard_ = data_ + payload_bytes_;
    std::fill(data_, data_ + payload_bytes_, 0);
    reset_sentinels();
  }

  PlaneView<T> view() {
    return PlaneView<T>(reinterpret_cast<T*>(data_), width_, height_, pitch_bytes_);
  }

  PlaneView<const T> view() const {
    return PlaneView<const T>(reinterpret_cast<const T*>(data_), width_, height_, pitch_bytes_);
  }

  void reset_sentinels() {
    std::fill(prefix_guard_, prefix_guard_ + kGuardBytes, kGuardSentinel);
    std::fill(suffix_guard_, suffix_guard_ + kGuardBytes, kGuardSentinel);
    for (std::size_t y = 0; y < height_; ++y) {
      auto* row = data_ + y * pitch_bytes_;
      std::fill(row + active_row_bytes_, row + pitch_bytes_, padding_sentinel_);
    }
  }

  bool guards_intact() const {
    return std::all_of(prefix_guard_, prefix_guard_ + kGuardBytes,
                       [](std::uint8_t value) { return value == kGuardSentinel; }) &&
           std::all_of(suffix_guard_, suffix_guard_ + kGuardBytes,
                       [](std::uint8_t value) { return value == kGuardSentinel; });
  }

  bool padding_intact() const {
    for (std::size_t y = 0; y < height_; ++y) {
      const auto* row = data_ + y * pitch_bytes_;
      if (!std::all_of(row + active_row_bytes_, row + pitch_bytes_,
                       [this](std::uint8_t value) { return value == padding_sentinel_; })) {
        return false;
      }
    }
    return true;
  }

  bool padding_intact_from(std::size_t row_byte_offset) const {
    if (row_byte_offset < active_row_bytes_ || row_byte_offset > pitch_bytes_) {
      throw std::invalid_argument("padding range is outside row padding");
    }
    for (std::size_t y = 0; y < height_; ++y) {
      const auto* row = data_ + y * pitch_bytes_;
      if (!std::all_of(row + row_byte_offset, row + pitch_bytes_,
                       [this](std::uint8_t value) { return value == padding_sentinel_; })) {
        return false;
      }
    }
    return true;
  }

  bool memory_intact() const { return guards_intact() && padding_intact(); }

  std::vector<std::uint8_t> snapshot_active() const {
    std::vector<std::uint8_t> result;
    result.reserve(active_row_bytes_ * height_);
    for (std::size_t y = 0; y < height_; ++y) {
      const auto* row = data_ + y * pitch_bytes_;
      result.insert(result.end(), row, row + active_row_bytes_);
    }
    return result;
  }

  bool active_matches(const std::vector<std::uint8_t>& snapshot) const {
    return snapshot_active() == snapshot;
  }

  void corrupt_suffix_guard_for_test() { suffix_guard_[0] ^= 1; }

 private:
  static constexpr std::size_t kGuardBytes = 64;
  static constexpr std::uint8_t kGuardSentinel = 0xA5;

  std::size_t width_;
  std::size_t height_;
  std::size_t pitch_bytes_;
  std::size_t alignment_;
  std::size_t alignment_offset_;
  std::uint8_t padding_sentinel_;
  std::size_t active_row_bytes_{};
  std::size_t payload_bytes_{};
  std::vector<std::uint8_t> storage_;
  std::uint8_t* data_{};
  std::uint8_t* prefix_guard_{};
  std::uint8_t* suffix_guard_{};
};

}  // namespace avsut::test
