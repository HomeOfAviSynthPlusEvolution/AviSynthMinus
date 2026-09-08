#ifndef AVS_TESTS_CX_CHECKER_KERNEL_H
#define AVS_TESTS_CX_CHECKER_KERNEL_H

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cx_smoke {

inline void InvertCheckerboard(std::uint8_t *data, int pitch, int row_size, int height,
                               int block_size) noexcept {
  for (int y = 0; y < height; ++y) {
    std::uint8_t *row = data + static_cast<std::ptrdiff_t>(y) * pitch;
    bool invert = ((y / block_size) & 1) != 0;
    int x = 0;
    while (x < row_size) {
      const int next_boundary = std::min(row_size, ((x / block_size) + 1) * block_size);
      if (invert) {
        for (int i = x; i < next_boundary; ++i) {
          row[i] = static_cast<std::uint8_t>(~row[i]);
        }
      }
      x = next_boundary;
      invert = !invert;
    }
  }
}

} // namespace cx_smoke

#endif // AVS_TESTS_CX_CHECKER_KERNEL_H
