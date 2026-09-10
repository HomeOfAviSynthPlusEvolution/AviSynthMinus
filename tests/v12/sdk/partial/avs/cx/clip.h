// Clip references and operations for the AviSynth CX ABI.
#ifndef AVS_CX_CLIP_H
#define AVS_CX_CLIP_H

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 4)

// AviSynth pixel-type identifiers are data values, not C++ ABI objects.
#define AVS_CX_PIXEL_TYPE_Y8 UINT32_C(0xE0000000)

typedef struct avs_cx_video_info_v1 {
  uint32_t struct_size;
  uint32_t flags;
  int32_t width;
  int32_t height;
  uint32_t fps_numerator;
  uint32_t fps_denominator;
  int64_t num_frames;
  uint32_t pixel_type;
  int32_t audio_samples_per_second;
  int32_t sample_type;
  int32_t channels;
  int64_t num_audio_samples;
  uint32_t image_type;
  uint32_t reserved0;
  uint64_t reserved[8];
} avs_cx_video_info_v1;

struct avs_cx_clip_ops_v1;

typedef struct avs_cx_clip_ref_v1 {
  void *object;
  const struct avs_cx_clip_ops_v1 *operations;
} avs_cx_clip_ref_v1;

// A non-null clip ref owns one reference under the same retain/release rules as
// a frame ref. On successful get_frame, frame_out receives one owned reference.

typedef struct avs_cx_clip_ops_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void(AVS_CX_CALL *retain)(void *object);
  void(AVS_CX_CALL *release)(void *object);
  avs_cx_status(AVS_CX_CALL *get_video_info)(void *object, avs_cx_video_info_v1 *video_info_out,
                                             avs_cx_error_v1 *error_out);
  avs_cx_status(AVS_CX_CALL *get_frame)(void *object, int64_t frame_number,
                                        const avs_cx_call_context_v1 *call,
                                        avs_cx_frame_ref_v1 *frame_out, avs_cx_error_v1 *error_out);
  avs_cx_status(AVS_CX_CALL *get_audio)(void *object, void *buffer, int64_t start, int64_t count,
                                        const avs_cx_call_context_v1 *call,
                                        avs_cx_error_v1 *error_out);
  avs_cx_status(AVS_CX_CALL *get_parity)(void *object, int64_t frame_number, int32_t *parity_out,
                                         avs_cx_error_v1 *error_out);
  avs_cx_status(AVS_CX_CALL *set_cache_hints)(void *object, int32_t cache_hints,
                                              int32_t frame_range, int32_t *result_out,
                                              avs_cx_error_v1 *error_out);
  void *reserved[8];
} avs_cx_clip_ops_v1;

// retain, release, get_video_info, and get_frame are mandatory in v1.
// get_audio, get_parity, and set_cache_hints may be NULL when unsupported.

typedef struct avs_cx_clip_feature_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t clip_ops_version;
  uint32_t clip_ref_size;
  uint64_t reserved[4];
} avs_cx_clip_feature_v1;

#pragma pack(pop)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // AVS_CX_CLIP_H
