#include "support/avisynth_environment.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdio>
#include <new>
#include <thread>

namespace {
// Only the calling thread is affected, and assertions run after injection ends.
thread_local int allocations_before_failure = -1;

class AllocationFailure {
public:
  explicit AllocationFailure(int successful_allocations) {
    allocations_before_failure = successful_allocations;
  }
  ~AllocationFailure() { allocations_before_failure = -1; }
  AllocationFailure(const AllocationFailure&) = delete;
  AllocationFailure& operator=(const AllocationFailure&) = delete;
};
}

void* operator new(std::size_t size) {
  if (allocations_before_failure == 0)
    throw std::bad_alloc();
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

} // namespace
} // namespace avsut::test
