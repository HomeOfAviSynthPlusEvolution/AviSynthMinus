#include <avisynth_c.h>
#include <gtest/gtest.h>

namespace avsut::test {
namespace {

TEST(CApiValue, VoidStructIsFullyInitialized) {
  EXPECT_EQ(avs_void.type, 'v');
  EXPECT_EQ(avs_void.array_size, 0);
  EXPECT_EQ(avs_void.d.integer, 0);
}

TEST(CApiGlobalLock, AcquiresAndReleasesNamedLock) {
  AVS_ScriptEnvironment *environment =
      avs_create_script_environment(AVISYNTH_INTERFACE_VERSION);
  ASSERT_NE(environment, nullptr);

  EXPECT_EQ(avs_acquire_global_lock(environment, "avs-c-api-test-global-lock"),
            1);
  EXPECT_EQ(avs_get_error(environment), nullptr);
  avs_release_global_lock(environment, "avs-c-api-test-global-lock");
  EXPECT_EQ(avs_get_error(environment), nullptr);

  avs_delete_script_environment(environment);
}

} // namespace
} // namespace avsut::test
