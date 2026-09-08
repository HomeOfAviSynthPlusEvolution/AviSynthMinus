#include <avisynth_cx_legacy.h>
extern "C" const char *__stdcall AvisynthPluginInit3(IScriptEnvironment *, const AVS_Linkage *);
namespace {
AVSValue __cdecl TransportProof(AVSValue, void *, IScriptEnvironment *) { return true; }
const char *__stdcall InitWithProof(IScriptEnvironment *env, const AVS_Linkage *linkage) {
  const char *name = AvisynthPluginInit3(env, linkage);
  env->AddFunction("CXSdkOnly", "", &TransportProof, nullptr);
  return name;
}
} // namespace
AVS_CX_PLUGIN_INIT(InitWithProof)
