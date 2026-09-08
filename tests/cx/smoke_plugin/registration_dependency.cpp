#include <avisynth.h>
#include <avs/cx/abi.h>
const AVS_Linkage *AVS_linkage = nullptr;
extern "C" AVS_CX_EXPORT const char *__stdcall
AvisynthPluginInit3(IScriptEnvironment *env, const AVS_Linkage *linkage) {
  AVS_linkage = linkage;
#ifdef CX_REGISTRATION_THROW
  env->ThrowError("CX dependency Init3 deliberately failed");
#endif
#ifdef CX_REGISTRATION_GATE
  env->Invoke("CXHoldRegistrationGate", AVSValue(nullptr, 0));
#endif
  return "registration dependency";
}
