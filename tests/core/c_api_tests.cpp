#include <gtest/gtest.h>
#include <avisynth_c.h>

namespace avsut::test {
namespace {

TEST(CApiValue, VoidStructIsFullyInitialized) {
  EXPECT_EQ(avs_void.type, 'v');
  EXPECT_EQ(avs_void.array_size, 0);
  EXPECT_EQ(avs_void.d.integer, 0);
}

}  // namespace
}  // namespace avsut::test
