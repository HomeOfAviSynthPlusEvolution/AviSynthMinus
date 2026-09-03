#include "core/bitblt.h"

#include "support/guarded_video_buffer.h"
#include "support/stable_hash.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace avsut::test {
namespace {

struct BitBltCase {
  std::size_t row_size{};
  std::size_t height{};
  std::size_t source_pitch{};
  std::size_t destination_pitch{};
  std::size_t source_alignment_offset{};
  std::size_t destination_alignment_offset{};
  bool reverse_source{};
  std::string expected_hash;
  std::string name;
};

inline std::string bitblt_case_name(const BitBltCase& test_case) {
  std::ostringstream stream;
  stream << "Rows" << test_case.row_size << "_Height" << test_case.height << "_SrcPitch"
         << test_case.source_pitch << "_DstPitch" << test_case.destination_pitch << "_SrcAlign"
         << test_case.source_alignment_offset << "_DstAlign"
         << test_case.destination_alignment_offset
         << (test_case.reverse_source ? "_NegativeSourcePitch" : "_PositiveSourcePitch")
         << "_PatternFixedBytes";
  return stream.str();
}

inline BitBltCase make_bitblt_case(std::size_t row_size, std::size_t height,
                                   std::size_t source_pitch, std::size_t destination_pitch,
                                   std::size_t source_alignment_offset,
                                   std::size_t destination_alignment_offset, bool reverse_source,
                                   std::string expected_hash) {
  BitBltCase result{row_size,
                    height,
                    source_pitch,
                    destination_pitch,
                    source_alignment_offset,
                    destination_alignment_offset,
                    reverse_source,
                    std::move(expected_hash),
                    {}};
  result.name = bitblt_case_name(result);
  return result;
}

inline void PrintTo(const BitBltCase& test_case, std::ostream* stream) {
  *stream << test_case.name;
}

inline std::vector<BitBltCase> bitblt_cases() {
  return {
      make_bitblt_case(37, 3, 64, 80, 3, 5, false, "8d4bd671fa3a691e"),
      make_bitblt_case(23, 1, 64, 80, 1, 7, false, "370cca8cb09e366c"),
      make_bitblt_case(29, 4, 64, 96, 1, 7, true, "f410442c357c8394"),
  };
}

class BitBltKernels : public ::testing::TestWithParam<BitBltCase> {};

inline void fill_source(PlaneView<std::uint8_t> source, std::size_t row_size) {
  for (std::size_t y = 0; y < source.height(); ++y) {
    for (std::size_t x = 0; x < row_size; ++x) {
      source.row(y)[x] = static_cast<std::uint8_t>((x * 37U + y * 91U + 0x2DU) & 0xFFU);
    }
  }
}

TEST_P(BitBltKernels, CopiesRowsAndPreservesMemory) {
  const auto& test_case = GetParam();
  GuardedVideoBuffer<std::uint8_t> source(test_case.row_size, test_case.height,
                                          test_case.source_pitch, 64,
                                          test_case.source_alignment_offset);
  GuardedVideoBuffer<std::uint8_t> destination(test_case.row_size, test_case.height,
                                               test_case.destination_pitch, 64,
                                               test_case.destination_alignment_offset);

  fill_source(source.view(), test_case.row_size);
  const auto source_snapshot = source.snapshot_active();

  auto* source_start = source.view().data();
  int source_pitch = static_cast<int>(test_case.source_pitch);
  if (test_case.reverse_source) {
    source_start = source.view().row(test_case.height - 1);
    source_pitch = -source_pitch;
  }

  BitBlt(reinterpret_cast<BYTE*>(destination.view().data()),
         static_cast<int>(test_case.destination_pitch), reinterpret_cast<const BYTE*>(source_start),
         source_pitch, static_cast<int>(test_case.row_size), static_cast<int>(test_case.height));

  for (std::size_t y = 0; y < test_case.height; ++y) {
    const auto source_row = test_case.reverse_source ? test_case.height - 1 - y : y;
    ASSERT_EQ(std::vector<std::uint8_t>(destination.view().row(y),
                                        destination.view().row(y) + test_case.row_size),
              std::vector<std::uint8_t>(source.view().row(source_row),
                                        source.view().row(source_row) + test_case.row_size))
        << test_case.name << " row=" << y;
  }

  EXPECT_EQ(format_hash(hash_active(destination.view().as_const())), test_case.expected_hash)
      << test_case.name << " stable output hash mismatch";
  EXPECT_TRUE(source.active_matches(source_snapshot)) << test_case.name;
  EXPECT_TRUE(source.memory_intact()) << test_case.name;
  EXPECT_TRUE(destination.memory_intact()) << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(Kernels, BitBltKernels, ::testing::ValuesIn(bitblt_cases()),
                         [](const ::testing::TestParamInfo<BitBltCase>& info) {
                           return info.param.name;
                         });

TEST(BitBlt, IgnoresZeroRowsAndHeight) {
  constexpr std::size_t row_size = 31;
  constexpr std::size_t height = 3;
  GuardedVideoBuffer<std::uint8_t> source(row_size, height, 64, 64, 2);
  GuardedVideoBuffer<std::uint8_t> destination(row_size, height, 80, 64, 6);
  fill_source(source.view(), row_size);
  const auto source_snapshot = source.snapshot_active();
  const auto destination_snapshot = destination.snapshot_active();

  BitBlt(reinterpret_cast<BYTE*>(destination.view().data()), 80,
         reinterpret_cast<const BYTE*>(source.view().data()), 64, 0, static_cast<int>(height));
  BitBlt(reinterpret_cast<BYTE*>(destination.view().data()), 80,
         reinterpret_cast<const BYTE*>(source.view().data()), 64, static_cast<int>(row_size), 0);

  EXPECT_TRUE(source.active_matches(source_snapshot));
  EXPECT_TRUE(destination.active_matches(destination_snapshot));
  EXPECT_TRUE(source.memory_intact());
  EXPECT_TRUE(destination.memory_intact());
}

}  // namespace
}  // namespace avsut::test
