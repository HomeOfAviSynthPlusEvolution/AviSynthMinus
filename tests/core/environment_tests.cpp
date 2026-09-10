#include "core/internal.h"
#include <gtest/gtest.h>
#include "support/avisynth_environment.h"
#include <cstring>

namespace avsut::test {
namespace {

TEST(EnvironmentStrings, ExplicitLengthIncludesBytesAfterNul) {
  AviSynthEnvironment environment;
  auto *env = environment.get();
  // These two records have the same DJB2 hash, independently of bucket count.
  const char a[] = {'a', 0, 1, 33}, b[] = {'a', 0, 2, 0};
  const char *first = env->SaveString(a, sizeof(a));
  const char *second = env->SaveString(b, sizeof(b));
  EXPECT_EQ(std::memcmp(first, a, sizeof(a)), 0);
  EXPECT_EQ(std::memcmp(second, b, sizeof(b)), 0);
  EXPECT_EQ(std::memcmp(env->SaveString(a, sizeof(a)), a, sizeof(a)), 0);
  EXPECT_STREQ(env->SaveString("ordinary string"), "ordinary string");
  EXPECT_STREQ(env->SaveString("", 0), "");
}

TEST(V12CpuPolicy, LimitsNeverEnableFlagsOrAffectOtherEnvironments) {
  AviSynthEnvironment first, second;
  auto *env = first.get();
  const int64_t native = second.get()->GetCPUFlagsEx();
#if defined(X86_32) || defined(X86_64)
  for (const char *limit : {"avx2", "avx512fast", "avx512base", "none", "avx512fast"}) {
#else
  for (const char *limit : {"none"}) {
#endif
    const int64_t before = env->GetCPUFlagsEx();
    env->Invoke("SetMaxCPU", AVSValue(limit));
    const int64_t after = env->GetCPUFlagsEx();
    EXPECT_EQ(after & ~before, 0) << limit;
    EXPECT_EQ(static_cast<uint32_t>(env->GetCPUFlags()), static_cast<uint32_t>(after));
    EXPECT_EQ(second.get()->GetCPUFlagsEx(), native);
  }
}

TEST(WorkingDirectory, GetCurrentWorkingDirectoryReturnsNonEmpty) {
#ifdef AVS_WINDOWS
  const std::wstring cwd = CWDChanger::GetCurrentWorkingDirectory();
  EXPECT_FALSE(cwd.empty());
#else
  const std::string cwd = CWDChanger::GetCurrentWorkingDirectory();
  EXPECT_FALSE(cwd.empty());
#endif
}

} // namespace
} // namespace avsut::test
