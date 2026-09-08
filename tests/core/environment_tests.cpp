#include "core/internal.h"
#include "support/avisynth_environment.h"
#include <avisynth.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>

namespace avsut::test {
namespace {

TEST(WorkingDirectory, GetCurrentWorkingDirectoryReturnsNonEmpty) {
#ifdef AVS_WINDOWS
  const std::wstring cwd = CWDChanger::GetCurrentWorkingDirectory();
  EXPECT_FALSE(cwd.empty());
#else
  const std::string cwd = CWDChanger::GetCurrentWorkingDirectory();
  EXPECT_FALSE(cwd.empty());
#endif
}

TEST(GlobalLock, SerializesTheSameNameAcrossEnvironments) {
  AviSynthEnvironment owner_environment;
  AviSynthEnvironment waiting_environment;
  constexpr const char *lock_name = "avs-core-test-global-lock";

  ASSERT_TRUE(owner_environment.get()->AcquireGlobalLock(lock_name));

  std::promise<void> attempting_lock;
  std::future<void> attempting = attempting_lock.get_future();
  std::future<bool> acquired = std::async(std::launch::async, [&] {
    attempting_lock.set_value();
    const bool result = waiting_environment.get()->AcquireGlobalLock(lock_name);
    if (result)
      waiting_environment.get()->ReleaseGlobalLock(lock_name);
    return result;
  });

  attempting.wait();
  EXPECT_EQ(acquired.wait_for(std::chrono::milliseconds(50)),
            std::future_status::timeout);

  owner_environment.get()->ReleaseGlobalLock(lock_name);
  ASSERT_EQ(acquired.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_TRUE(acquired.get());
}

TEST(GlobalLock, RejectsNullName) {
  AviSynthEnvironment environment;
  EXPECT_FALSE(environment.get()->AcquireGlobalLock(nullptr));
  environment.get()->ReleaseGlobalLock(nullptr);
}

} // namespace
} // namespace avsut::test
