#include <avs/cx/sdk/runtime.h>

// A deliberately failing dual-entry plugin. The test proves that a host which
// finds CX1 never retries the compiler-specific Init3 entry after CX1 rejects
// initialization.

const AVS_Linkage *AVS_linkage = nullptr;

namespace {

avs_cx_status AVS_CX_CALL NeverCalled(void *, const avs_cx_call_context_v1 *,
                                      const avs_cx_value_v1 *, uint32_t, avs_cx_value_v1 *,
                                      avs_cx_error_v1 *) noexcept {
  return AVS_CX_STATUS_PLUGIN_ERROR;
}

} // namespace

extern "C" AVS_CX_EXPORT const char *__stdcall
AvisynthPluginInit3(IScriptEnvironment *, const AVS_Linkage *const linkage) {
  AVS_linkage = linkage;
  return "ERROR: legacy fallback was executed";
}

extern "C" AVS_CX_EXPORT avs_cx_status AVS_CX_CALL
AvisynthPluginInitCX1(const avs_cx_host_v1 *host) noexcept {
  avs::cx::runtime runtime;
  avs_cx_status status = runtime.initialize(host);
  if (status != AVS_CX_STATUS_OK)
    return status;

  status = runtime.register_function("CXRejectedStagedFunction", "c", &NeverCalled, nullptr);
  if (status != AVS_CX_STATUS_OK)
    return status;

  // The second registration is invalid. The first must remain staged and must
  // not leak into the environment when initialization fails.
  return runtime.register_function("CXRejectedInvalidFunction", "c[", &NeverCalled, nullptr);
}
