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
