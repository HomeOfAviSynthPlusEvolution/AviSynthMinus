#include <avs/cx/clip.h>
#include <avs/cx/environment.h>
#include <avs/cx/feature_keys.h>
#include <avs/cx/registry.h>
#include <avs/cx/sdk.h>
#include <avs/cx/message.h>

#if defined(__cplusplus)
#error "CX protocol headers must also compile as C"
#endif

#define AVS_CX_C_ASSERT(name, expression) typedef char name[(expression) ? 1 : -1]

AVS_CX_C_ASSERT(avs_cx_value_size_is_stable, sizeof(avs_cx_value_v1) == 40);
AVS_CX_C_ASSERT(avs_cx_video_info_size_is_stable, sizeof(avs_cx_video_info_v1) == 128);
AVS_CX_C_ASSERT(avs_cx_frame_ref_is_two_pointers,
                sizeof(avs_cx_frame_ref_v1) == sizeof(void *) * 2);
AVS_CX_C_ASSERT(avs_cx_clip_ref_is_two_pointers, sizeof(avs_cx_clip_ref_v1) == sizeof(void *) * 2);
AVS_CX_C_ASSERT(avs_cx_host_table_size_is_stable, sizeof(avs_cx_host_v1) == 8 + sizeof(void *) * 6);
AVS_CX_C_ASSERT(avs_cx_sdk_existing_ids_do_not_move,
                AVS_CX_SDK_SAVE_STRING == 0 && AVS_CX_SDK_MAKE_PROPERTY_WRITABLE == 15 &&
                AVS_CX_SDK_PROPSETDATAH == 45 && AVS_CX_SDK_SUBFRAME == 46 &&
                AVS_CX_SDK_INVOKE2 == 47 && AVS_CX_SDK_PROP_SET_FRAME == 51);

int avs_cx_c_header_compile_probe(void) {
  return AVS_CX_ABI_VERSION_1 == 1 && AVS_CX_FEATURE_REGISTRY != 0;
}
