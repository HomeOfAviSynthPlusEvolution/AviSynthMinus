#include <avisynth_cx_legacy.h>
#include <cstring>
const AVS_Linkage *AVS_linkage = nullptr;
namespace {
AVSValue __cdecl Answer(AVSValue, void *, IScriptEnvironment *) { return 42; }
}
extern "C" AVS_CX_EXPORT const char *__stdcall
AvisynthPluginInit3(IScriptEnvironment *env, const AVS_Linkage *linkage) {
  AVS_linkage = linkage;
  env->AddFunction("CXBeforeNested", "", Answer, nullptr);
  env->Invoke("CXLoadNestedDependency", AVSValue(nullptr, 0));
  env->AddFunction("CXAfterNested", "", Answer, nullptr);
  bool missing = false;
  try {
    env->Invoke("CXLoadMissingDependency", AVSValue(nullptr, 0));
  } catch (const AvisynthError &) {
    missing = true;
  }
  if (!missing) env->ThrowError("missing dependency unexpectedly loaded");
  env->AddFunction("CXAfterFailedNested", "", Answer, nullptr);
  bool threw = false;
  try {
    env->Invoke("CXLoadThrowingDependency", AVSValue(nullptr, 0));
  } catch (const AvisynthError &error) {
    threw = std::strstr(error.msg, "CX dependency Init3 deliberately failed") != nullptr;
  }
  if (!threw) env->ThrowError("dependency did not reach its throwing Init3");
  env->AddFunction("CXAfterThrowingNested", "", Answer, nullptr);
  return "nested registration test";
}
AVS_CX_PLUGIN_INIT(AvisynthPluginInit3)
