// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#include <avisynth.h>
#include <gtest/gtest.h>

namespace {
class UninitializedClient {
 public:
  UninitializedClient() : previous_(AVS_linkage) {
    AVS_linkage = nullptr;
    env = CreateScriptEnvironment2();
  }
  ~UninitializedClient() {
    if (env) env->DeleteScriptEnvironment();
    AVS_linkage = previous_;
  }
  IScriptEnvironment2* env = nullptr;
 private:
  const AVS_Linkage* previous_;
};

TEST(SharedLinkage, ReturnsCoreTableBeforeClientInitialization) {
  UninitializedClient client;
  ASSERT_NE(client.env, nullptr);
  const auto* table = client.env->GetAVSLinkage();
  ASSERT_NE(table, nullptr);
  EXPECT_GE(table->Size, sizeof(AVS_Linkage));
  EXPECT_EQ(AVS_linkage, nullptr);
}

TEST(SharedLinkage, InitializesLegacyPluginBeforeClientLinkage) {
  UninitializedClient client;
  ASSERT_NE(client.env, nullptr);
  const auto* table = client.env->GetAVSLinkage();
  ASSERT_NE(table, nullptr);
  AVS_linkage = table;
  {
    // LoadPlugin requires a constructed result even while the client pointer is unset.
    AVSValue result;
    AVS_linkage = nullptr;
    const bool loaded = client.env->LoadPlugin(SHARED_LINKAGE_PLUGIN, false, &result);
    EXPECT_EQ(AVS_linkage, nullptr);
    AVS_linkage = table;
    EXPECT_TRUE(loaded);
  }
  AVS_linkage = nullptr;
}
} // namespace
