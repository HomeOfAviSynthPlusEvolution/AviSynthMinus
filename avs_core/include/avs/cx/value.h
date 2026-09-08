// ABI-neutral values used by AviSynth CX registration callbacks.
#ifndef AVS_CX_VALUE_H
#define AVS_CX_VALUE_H

#include "clip.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 4)

enum avs_cx_value_type {
  AVS_CX_VALUE_UNDEFINED = 0,
  AVS_CX_VALUE_BOOL = 1,
  AVS_CX_VALUE_INT = 2,
  AVS_CX_VALUE_FLOAT = 3,
  AVS_CX_VALUE_STRING = 4,
  AVS_CX_VALUE_CLIP = 5,
  AVS_CX_VALUE_ARRAY = 6,
  // Preserve the public AVSValue type, not just its numeric value.
  AVS_CX_VALUE_INT32 = 7,
  AVS_CX_VALUE_FLOAT32 = 8
};

struct avs_cx_value_v1;

typedef struct avs_cx_array_view_v1 {
  const struct avs_cx_value_v1 *values;
  uint32_t size;
  uint32_t reserved;
} avs_cx_array_view_v1;

typedef union avs_cx_value_payload_v1 {
  int32_t boolean;
  int64_t integer;
  double floating_point;
  avs_cx_string_view_v1 string;
  avs_cx_clip_ref_v1 clip;
  avs_cx_array_view_v1 array;
  uint64_t raw[2];
} avs_cx_value_payload_v1;

typedef struct avs_cx_value_v1 {
  uint32_t struct_size;
  uint32_t type;
  avs_cx_value_payload_v1 value;
  uint64_t reserved[2];
} avs_cx_value_v1;

// Apply-call arguments and their string/array storage are borrowed for the
// duration of the callback. A returned clip transfers one owned reference to
// the host. Returned string/array storage is borrowed and must remain valid
// until the host has synchronously consumed the callback result.

#pragma pack(pop)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // AVS_CX_VALUE_H
