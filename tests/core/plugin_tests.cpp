#include <gtest/gtest.h>
#include <avisynth.h>
#include "support/avisynth_environment.h"

namespace avsut::test {
namespace {

TEST(PluginLoading, ThrowsAvisynthErrorOnNonExistentPlugin) {
  AviSynthEnvironment environment;
  const AVSValue filename("non_existent_plugin_xyz12345.dll");
  EXPECT_THROW(
      { environment.get()->Invoke("LoadPlugin", AVSValue(&filename, 1)); },
      AvisynthError);
}

}  // namespace
}  // namespace avsut::test
