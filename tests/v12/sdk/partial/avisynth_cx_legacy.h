// Add this header and AVS_CX_PLUGIN_INIT(AvisynthPluginInit3) to one translation
// unit, and compile avs/cx/legacy.cpp into the plugin with the same compiler.
#ifndef AVISYNTH_CX_LEGACY_H
#define AVISYNTH_CX_LEGACY_H
#include "avisynth.h"
#include "avs/cx/abi.h"
namespace avs {
namespace cx {
typedef const char *(__stdcall *legacy_init)(IScriptEnvironment *, const AVS_Linkage *);
avs_cx_status initialize_legacy(const avs_cx_host_v1 *, legacy_init) noexcept;
} // namespace cx
} // namespace avs
#define AVS_CX_PLUGIN_INIT(init)                                                                   \
  extern "C" AVS_CX_EXPORT avs_cx_status AVS_CX_CALL AvisynthPluginInitCX1(                        \
      const avs_cx_host_v1 *host) noexcept {                                                       \
    return avs::cx::initialize_legacy(host, &(init));                                              \
  }
#endif
