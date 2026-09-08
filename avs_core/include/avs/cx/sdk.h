// Experimental source-compatibility services. C ABI; no legacy object layouts.
#ifndef AVS_CX_SDK_H
#define AVS_CX_SDK_H
#include "frame.h"
#include "registry.h"
#define AVS_CX_FEATURE_SDK UINT64_C(0x415653435853444B)
#ifdef __cplusplus
extern "C" {
#endif
#pragma pack(push, 4)
enum avs_cx_sdk_operation {
  AVS_CX_SDK_SAVE_STRING,
  AVS_CX_SDK_NEW_FRAME,
  AVS_CX_SDK_FRAME_WRITABLE,
  AVS_CX_SDK_INVOKE,
  AVS_CX_SDK_GET_VAR,
  AVS_CX_SDK_SET_VAR,
  AVS_CX_SDK_SET_GLOBAL_VAR,
  AVS_CX_SDK_FUNCTION_EXISTS,
  AVS_CX_SDK_ENV_PROPERTY,
  AVS_CX_SDK_CHECK_VERSION,
  AVS_CX_SDK_ACQUIRE_LOCK,
  AVS_CX_SDK_RELEASE_LOCK,
  AVS_CX_SDK_COPY_PROPS,
  AVS_CX_SDK_PROPS_RO,
  AVS_CX_SDK_PROPS_RW,
  AVS_CX_SDK_MAKE_PROPERTY_WRITABLE,
#include "sdk/operation_ids.inc"
  AVS_CX_SDK_SUBFRAME,
  AVS_CX_SDK_INVOKE2,
  AVS_CX_SDK_PROP_GET_CLIP,
  AVS_CX_SDK_PROP_SET_CLIP,
  AVS_CX_SDK_PROP_GET_FRAME,
  AVS_CX_SDK_PROP_SET_FRAME,
};
// Input frame/value refs are borrowed. Output frame refs are owned. Output
// values belong to result_owner until release_result; copy/retain before release.
typedef struct avs_cx_sdk_request_v1 {
  uint32_t struct_size;
  uint32_t operation;
  int64_t integer;
  const char *text;
  const char *const *names;
  const avs_cx_video_info_v1 *video_info;
  const avs_cx_value_v1 *value;
  const avs_cx_value_v1 *other_value;
  avs_cx_frame_ref_v1 frame;
  avs_cx_frame_ref_v1 other_frame;
  int64_t integers[8];
  double real;
  const void *pointers[4];
} avs_cx_sdk_request_v1;
typedef struct avs_cx_sdk_result_v1 {
  uint32_t struct_size;
  int32_t reserved;
  int64_t integer;
  void *pointer;
  avs_cx_frame_ref_v1 frame;
  avs_cx_value_v1 value;
  void *result_owner;
  double real;
} avs_cx_sdk_result_v1;
typedef struct avs_cx_sdk_feature_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void *context;
  void *initialization_environment;
  avs_cx_status(AVS_CX_CALL *dispatch)(void *context, void *environment,
                                       const avs_cx_sdk_request_v1 *, avs_cx_sdk_result_v1 *,
                                       avs_cx_error_v1 *);
  void(AVS_CX_CALL *release_result)(void *result_owner);
  // 0: IsWritable, 1: IsPropertyWritable, 2: AmendPixelType (input *value).
  avs_cx_status(AVS_CX_CALL *frame_metadata)(const avs_cx_frame_ref_v1 *, uint32_t operation,
                                             int32_t *value, avs_cx_error_v1 *);
} avs_cx_sdk_feature_v1;
#pragma pack(pop)
#ifdef __cplusplus
}
#endif
#endif
