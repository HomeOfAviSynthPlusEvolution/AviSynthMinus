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
// Explicit IDs are permanent. Allocate new IDs; never renumber existing ones.
enum avs_cx_sdk_operation {
  AVS_CX_SDK_SAVE_STRING = 0,
  AVS_CX_SDK_NEW_FRAME = 1,
  AVS_CX_SDK_FRAME_WRITABLE = 2,
  AVS_CX_SDK_INVOKE = 3,
  AVS_CX_SDK_GET_VAR = 4,
  AVS_CX_SDK_SET_VAR = 5,
  AVS_CX_SDK_SET_GLOBAL_VAR = 6,
  AVS_CX_SDK_FUNCTION_EXISTS = 7,
  AVS_CX_SDK_ENV_PROPERTY = 8,
  AVS_CX_SDK_CHECK_VERSION = 9,
  AVS_CX_SDK_ACQUIRE_LOCK = 10,
  AVS_CX_SDK_RELEASE_LOCK = 11,
  AVS_CX_SDK_COPY_PROPS = 12,
  AVS_CX_SDK_PROPS_RO = 13,
  AVS_CX_SDK_PROPS_RW = 14,
  AVS_CX_SDK_MAKE_PROPERTY_WRITABLE = 15,
  AVS_CX_SDK_PUSHCONTEXT = 16,
  AVS_CX_SDK_POPCONTEXT = 17,
  AVS_CX_SDK_SETMEMORYMAX = 18,
  AVS_CX_SDK_SETWORKINGDIR = 19,
  AVS_CX_SDK_PLANARCHROMAALIGNMENT = 20,
  AVS_CX_SDK_PROPNUMKEYS = 21,
  AVS_CX_SDK_PROPGETKEY = 22,
  AVS_CX_SDK_PROPNUMELEMENTS = 23,
  AVS_CX_SDK_PROPGETTYPE = 24,
  AVS_CX_SDK_PROPGETINT = 25,
  AVS_CX_SDK_PROPGETFLOAT = 26,
  AVS_CX_SDK_PROPGETDATA = 27,
  AVS_CX_SDK_PROPGETDATASIZE = 28,
  AVS_CX_SDK_PROPDELETEKEY = 29,
  AVS_CX_SDK_PROPSETINT = 30,
  AVS_CX_SDK_PROPSETFLOAT = 31,
  AVS_CX_SDK_PROPSETDATA = 32,
  AVS_CX_SDK_PROPGETINTARRAY = 33,
  AVS_CX_SDK_PROPGETFLOATARRAY = 34,
  AVS_CX_SDK_PROPSETINTARRAY = 35,
  AVS_CX_SDK_PROPSETFLOATARRAY = 36,
  AVS_CX_SDK_CREATEMAP = 37,
  AVS_CX_SDK_FREEMAP = 38,
  AVS_CX_SDK_CLEARMAP = 39,
  AVS_CX_SDK_ALLOCATE = 40,
  AVS_CX_SDK_FREE = 41,
  AVS_CX_SDK_PROPGETINTSATURATED = 42,
  AVS_CX_SDK_PROPGETFLOATSATURATED = 43,
  AVS_CX_SDK_PROPGETDATATYPEHINT = 44,
  AVS_CX_SDK_PROPSETDATAH = 45,
  AVS_CX_SDK_SUBFRAME = 46,
  AVS_CX_SDK_INVOKE2 = 47,
  AVS_CX_SDK_PROP_GET_CLIP = 48,
  AVS_CX_SDK_PROP_SET_CLIP = 49,
  AVS_CX_SDK_PROP_GET_FRAME = 50,
  AVS_CX_SDK_PROP_SET_FRAME = 51,
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
