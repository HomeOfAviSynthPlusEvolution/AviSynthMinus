#include "core/internal.h"
#include <gtest/gtest.h>

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

} // namespace
} // namespace avsut::test
