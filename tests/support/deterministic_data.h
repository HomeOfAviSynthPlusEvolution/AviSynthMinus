#pragma once

#include "support/plane_view.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace avsut::test {

class XorShift32 {
 public:
  explicit XorShift32(std::uint32_t seed) : state_(seed == 0 ? 0x6D2B79F5U : seed) {}

  std::uint32_t next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
  }

 private:
  std::uint32_t state_;
};

template <typename T>
void fill_random(PlaneView<T> view, std::uint32_t seed) {
  static_assert(!std::is_const_v<T>);
  XorShift32 generator(seed);
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      if constexpr (std::is_floating_point_v<T>) {
        view.row(y)[x] =
            static_cast<T>(static_cast<std::int32_t>(generator.next()) /
                           static_cast<double>(std::numeric_limits<std::int32_t>::max()));
      } else {
        view.row(y)[x] = static_cast<T>(generator.next());
      }
    }
  }
}

template <typename T>
void fill_incrementing(PlaneView<T> view) {
  static_assert(!std::is_const_v<T>);
  std::uint64_t value = 0;
  for (std::size_t y = 0; y < view.height(); ++y) {
    for (std::size_t x = 0; x < view.width(); ++x) {
      view.row(y)[x] = static_cast<T>(value++);
    }
  }
}

}  // namespace avsut::test
