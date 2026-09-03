#include <gtest/gtest.h>
#include <avisynth.h>
#include "support/avisynth_environment.h"

#include <string>

namespace avsut::test {
namespace {

TEST(ScriptImport, ThrowsErrorWithUtf8FileNameWhenFileNotFound) {
  AviSynthEnvironment environment;
  const AVSValue filename("non_existent_script_test_12345.avs");
  try {
    environment.get()->Invoke("Import", AVSValue(&filename, 1));
    FAIL() << "Expected AvisynthError";
  } catch (const AvisynthError& e) {
    EXPECT_NE(std::string(e.msg).find("non_existent_script"), std::string::npos);
  }
}

}  // namespace
}  // namespace avsut::test
