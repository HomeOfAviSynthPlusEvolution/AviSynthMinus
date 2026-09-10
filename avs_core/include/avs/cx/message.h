// Optional host message rendering, independent of the legacy C++ ABI.
#ifndef AVS_CX_MESSAGE_H
#define AVS_CX_MESSAGE_H
#include "clip.h"
#ifdef __cplusplus
extern "C" {
#endif
#pragma pack(push, 4)
typedef struct avs_cx_message_request_v1 {
  uint32_t struct_size;
  uint32_t utf8; // 0: legacy encoding, 1: UTF-8
  avs_cx_frame_ref_v1 source;
  avs_cx_video_info_v1 video_info;
  avs_cx_string_view_v1 message; // borrowed, no embedded NUL
  int32_t size; // same units as legacy ApplyMessage (GDI units)
  int32_t text_color;
  int32_t halo_color;
  int32_t background_color;
} avs_cx_message_request_v1;

typedef struct avs_cx_message_feature_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void *context;
  avs_cx_status (AVS_CX_CALL *apply)(void *context, void *environment,
      const avs_cx_message_request_v1 *request, avs_cx_frame_ref_v1 *output,
      avs_cx_error_v1 *error);
} avs_cx_message_feature_v1;
// Source is borrowed and remains unchanged. Output must be a distinct, empty
// reference; success returns one owned host frame, failure leaves output empty.
// Geometry and pixel type must match the source. No pointers are retained.
#pragma pack(pop)
#ifdef __cplusplus
}
#endif
#endif
