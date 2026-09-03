#include <gtest/gtest.h>
#include <avs/config.h>

namespace avsut::test {
namespace {

AVS_FORCEINLINE int test_forceinline_function(int a, int b) {
  return a + b;
}

TEST(ConfigMacros, ForceInlineFunctionCompilesAndExecutes) {
  EXPECT_EQ(test_forceinline_function(10, 20), 30);
}

}  // namespace
}  // namespace avsut::test
