// Plugin-to-host registration feature for the AviSynth CX ABI.
#ifndef AVS_CX_REGISTRY_H
#define AVS_CX_REGISTRY_H

#include "value.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 4)

typedef avs_cx_status(AVS_CX_CALL *avs_cx_apply_function_v1)(
    void *plugin_user_data, const avs_cx_call_context_v1 *call, const avs_cx_value_v1 *arguments,
    uint32_t argument_count, avs_cx_value_v1 *result_out, avs_cx_error_v1 *error_out);

// No callback may let an exception unwind across this boundary. The host owns
// argument storage. The plugin writes result_out and follows value.h ownership
// rules; the host consumes it synchronously before invoking another callback.

typedef void(AVS_CX_CALL *avs_cx_shutdown_v1)(void *plugin_user_data);

typedef struct avs_cx_registry_feature_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void *context;
  avs_cx_status(AVS_CX_CALL *set_plugin_name)(void *context, const avs_cx_string_view_v1 *name);
  avs_cx_status(AVS_CX_CALL *register_function)(void *context, const avs_cx_string_view_v1 *name,
                                                const avs_cx_string_view_v1 *parameter_string,
                                                avs_cx_apply_function_v1 apply,
                                                void *plugin_user_data);
  avs_cx_status(AVS_CX_CALL *register_shutdown)(void *context, avs_cx_shutdown_v1 shutdown,
                                                void *plugin_user_data);
  void *reserved[8];
} avs_cx_registry_feature_v1;

// Registration is immediately visible, including during initialization and
// later callbacks. Failed initialization removes this session's registrations.
// The host provides the same registration synchronization as its legacy API.
// The host copies the
// pointed-to name and parameter bytes before each registration call returns.
// Registered callbacks and plugin_user_data remain valid until shutdown.

#pragma pack(pop)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // AVS_CX_REGISTRY_H
