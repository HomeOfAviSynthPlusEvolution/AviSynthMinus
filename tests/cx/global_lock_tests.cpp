#include "support/avisynth_environment.h"
#include <avisynth_c.h>
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>

extern "C" int cx_test_c_acquire(AVS_ScriptEnvironment *, const char *);
extern "C" void cx_test_c_release(AVS_ScriptEnvironment *, const char *);

namespace avsut::test {
namespace {
enum Route { Cx, LegacyCpp, LegacyC };

class Routes {
  AviSynthEnvironment cx_, legacy_;
  std::unique_ptr<AVS_ScriptEnvironment, decltype(&avs_delete_script_environment)>
      c_{avs_create_script_environment(AVISYNTH_INTERFACE_VERSION), &avs_delete_script_environment};

public:
  Routes() {
    if (!c_) throw std::bad_alloc();
    const char *path = std::getenv("AVS_CX_SMOKE_DUAL_PATH");
    AVSValue result;
    static_cast<IScriptEnvironment2 *>(cx_.get())->LoadPlugin(
        path && *path ? path : CX_SMOKE_DUAL_PATH, true, &result);
    static_cast<IScriptEnvironment2 *>(legacy_.get())->LoadPlugin(
        CX_SMOKE_LEGACY_PATH, true, &result);
  }
  IScriptEnvironment *Env(int route) { return route == Cx ? cx_.get() : legacy_.get(); }
  bool Acquire(int route, const char *name) {
    if (route == LegacyC) return cx_test_c_acquire(c_.get(), name) != 0;
    const AVSValue arg(name);
    return Env(route)->Invoke("CXAcquireLock", AVSValue(&arg, 1)).AsBool();
  }
  void Release(int route, const char *name) {
    if (route == LegacyC) {
      cx_test_c_release(c_.get(), name);
      EXPECT_EQ(avs_get_error(c_.get()), nullptr);
    } else {
      const AVSValue arg(name);
      Env(route)->Invoke("CXReleaseLock", AVSValue(&arg, 1));
    }
  }
};

class CxGlobalLockRoutes : public testing::TestWithParam<int> {};

TEST_P(CxGlobalLockRoutes, SerializesAcrossInterfaces) {
  // Invoke serializes calls per environment. Use independent environments so
  // a blocked Invoke cannot prevent the owner from invoking ReleaseLock.
  Routes routes, waiting_routes;
  const int owner = GetParam() / 3, waiter = GetParam() % 3;
  // Long names also exercise paths beyond small-string optimization.
  constexpr char name[] = "avs-cx-global-lock-cross-interface-long-name-exceeding-small-string-storage";
  ASSERT_TRUE(routes.Acquire(owner, name));
  std::promise<void> starting;
  auto started = starting.get_future();
  auto acquired = std::async(std::launch::async, [&] {
    starting.set_value();
    const bool ok = waiting_routes.Acquire(waiter, name);
    if (ok) waiting_routes.Release(waiter, name);
    return ok;
  });
  started.wait();
  EXPECT_EQ(acquired.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
  // Always release before assertions which might return from the test.
  routes.Release(owner, name);
  EXPECT_TRUE(acquired.get()); // A deadlock is killed by the CTest process timeout.
}

INSTANTIATE_TEST_SUITE_P(AllDirections, CxGlobalLockRoutes, testing::Range(0, 9));

TEST(CxGlobalLock, RoundTripsThroughExistingSdk) {
  Routes routes;
  EXPECT_TRUE(routes.Env(Cx)->Invoke("CXGlobalLockRoundTrip", AVSValue(nullptr, 0)).AsBool());
}

TEST(CxGlobalLock, LegacyEntryRoundTripsThroughV12) {
  Routes routes;
  EXPECT_TRUE(routes.Env(LegacyCpp)->Invoke("CXGlobalLockRoundTrip", AVSValue(nullptr, 0)).AsBool());
}

TEST(CxGlobalLock, DifferentNamesDoNotBlock) {
  Routes routes;
  ASSERT_TRUE(routes.Acquire(LegacyC, "cx-lock-owner"));
  auto other = std::async(std::launch::async, [&] {
    const bool ok = routes.Acquire(Cx, "cx-lock-other");
    if (ok) routes.Release(Cx, "cx-lock-other");
    return ok;
  });
  EXPECT_EQ(other.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  routes.Release(LegacyC, "cx-lock-owner");
  EXPECT_TRUE(other.get());
}
} // namespace
} // namespace avsut::test
