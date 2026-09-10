// Per-call environment operations for the AviSynth CX ABI.
#ifndef AVS_CX_ENVIRONMENT_H
#define AVS_CX_ENVIRONMENT_H

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 4)

typedef struct avs_cx_environment_feature_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void *context;
  avs_cx_status(AVS_CX_CALL *make_writable)(void *context, void *environment,
                                            avs_cx_frame_ref_v1 *frame, avs_cx_error_v1 *error_out);
  avs_cx_status(AVS_CX_CALL *get_cpu_flags)(void *context, void *environment, uint64_t *flags_out);
  void *reserved[8];
} avs_cx_environment_feature_v1;

// make_writable consumes and replaces the one owned reference in *frame. It
// leaves *frame owning exactly one reference on both success and failure. It is
// zero-copy when the core can write in place; normal core COW rules still
// apply.

#pragma pack(pop)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // AVS_CX_ENVIRONMENT_H
