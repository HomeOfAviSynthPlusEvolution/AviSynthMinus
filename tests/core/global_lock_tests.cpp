#include "support/avisynth_environment.h"
#include <avisynth_c.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <exception>
#include <memory>
#include <new>
#include <system_error>
#include <thread>

namespace {
// Only the calling thread is affected, and assertions run after injection ends.
thread_local int allocations_before_failure = -1;
thread_local std::exception_ptr allocation_exception;

class AllocationFailure {
public:
  explicit AllocationFailure(int successful_allocations, std::exception_ptr error = {}) {
    allocation_exception = error;
    allocations_before_failure = successful_allocations;
  }
  ~AllocationFailure() {
    allocations_before_failure = -1;
    allocation_exception = {};
  }
  AllocationFailure(const AllocationFailure&) = delete;
  AllocationFailure& operator=(const AllocationFailure&) = delete;
};
}

void* operator new(std::size_t size) {
  if (allocations_before_failure == 0) {
    if (allocation_exception)
      std::rethrow_exception(allocation_exception);
    throw std::bad_alloc();
  }
  if (allocations_before_failure > 0)
    --allocations_before_failure;
  if (void* p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace avsut::test {
namespace {

void AcquireOnAnotherThread(IScriptEnvironment* env, const char* name) {
  bool acquired = false;
  std::thread waiter([&] {
    acquired = env->AcquireGlobalLock(name);
    if (acquired)
      env->ReleaseGlobalLock(name);
  });
  waiter.join(); // CTest's process timeout also covers a failed unlock.
  EXPECT_TRUE(acquired);
}

TEST(GlobalLockAllocation, FailedAcquireDoesNotLeaveLockHeld) {
  AviSynthEnvironment owner;
  AviSynthEnvironment waiter;
  int failures = 0;
  bool succeeded = false;
  // Fail each allocation in turn, including allocations made after locking.
  for (int budget = 0; budget < 32; ++budget) {
    char name[128];
    std::snprintf(name, sizeof(name), "global-lock-allocation-failure-long-name-%d", budget);
    bool acquired = false;
    try {
      AllocationFailure inject(budget);
      acquired = owner.get()->AcquireGlobalLock(name);
    }
    catch (const std::bad_alloc&) {
      ++failures;
    }
    if (acquired)
      owner.get()->ReleaseGlobalLock(name);
    AcquireOnAnotherThread(waiter.get(), name);
    if (acquired) {
      succeeded = true;
      break;
    }
  }
  EXPECT_GT(failures, 0);
  EXPECT_TRUE(succeeded);
}

TEST(GlobalLockAllocation, ReleaseLongNameDoesNotAllocate) {
  AviSynthEnvironment owner;
  AviSynthEnvironment waiter;
  constexpr const char* name = "global-lock-release-long-name-exceeding-small-string-storage";
  ASSERT_TRUE(owner.get()->AcquireGlobalLock(name));
  {
    AllocationFailure inject(0);
    owner.get()->ReleaseGlobalLock(name);
  }
  AcquireOnAnotherThread(waiter.get(), name);
}

using CEnvironment = std::unique_ptr<AVS_ScriptEnvironment,
                                     decltype(&avs_delete_script_environment)>;

TEST(CApiGlobalLockAllocation, FailedAcquireReturnsErrorAndLeavesLockAvailable) {
  CEnvironment owner(avs_create_script_environment(AVISYNTH_INTERFACE_VERSION),
                     avs_delete_script_environment);
  ASSERT_NE(owner, nullptr);
  AviSynthEnvironment waiter;
  int failures = 0;
  bool succeeded = false;
  for (int budget = 0; budget < 32; ++budget) {
    char name[128];
    std::snprintf(name, sizeof(name), "c-api-global-lock-allocation-failure-long-name-%d", budget);
    int acquired;
    {
      AllocationFailure inject(budget);
      acquired = avs_acquire_global_lock(owner.get(), name);
    }
    if (acquired) {
      EXPECT_EQ(avs_get_error(owner.get()), nullptr);
      {
        AllocationFailure inject(0);
        avs_release_global_lock(owner.get(), name);
      }
      EXPECT_EQ(avs_get_error(owner.get()), nullptr);
    } else {
      ++failures;
      ASSERT_NE(avs_get_error(owner.get()), nullptr);
      EXPECT_NE(*avs_get_error(owner.get()), '\0');
    }
    AcquireOnAnotherThread(waiter.get(), name);
    if (acquired) {
      succeeded = true;
      break;
    }
  }
  EXPECT_GT(failures, 0);
  EXPECT_TRUE(succeeded);
}

TEST(CApiGlobalLockAllocation, ContainsSystemErrorsAndUnknownExceptions) {
  CEnvironment env(avs_create_script_environment(AVISYNTH_INTERFACE_VERSION),
                   avs_delete_script_environment);
  ASSERT_NE(env, nullptr);
  constexpr const char* name = "c-api-global-lock-exception-boundary-long-name";
  // Reuse the allocator hook to exercise the C boundary with exception types
  // other than bad_alloc, without depending on platform-specific mutex errors.
  const std::exception_ptr errors[] = {
    std::make_exception_ptr(std::system_error(
      std::make_error_code(std::errc::resource_unavailable_try_again))),
    std::make_exception_ptr(42)
  };
  for (const auto& error : errors) {
    int acquired;
    {
      AllocationFailure inject(0, error);
      acquired = avs_acquire_global_lock(env.get(), name);
    }
    EXPECT_EQ(acquired, 0);
    ASSERT_NE(avs_get_error(env.get()), nullptr);
    EXPECT_NE(*avs_get_error(env.get()), '\0');
    ASSERT_EQ(avs_acquire_global_lock(env.get(), name), 1);
    EXPECT_EQ(avs_get_error(env.get()), nullptr);
    avs_release_global_lock(env.get(), name);
    EXPECT_EQ(avs_get_error(env.get()), nullptr);
  }
}

} // namespace
} // namespace avsut::test
