// Frame references and operations for the AviSynth CX ABI.
#ifndef AVS_CX_FRAME_H
#define AVS_CX_FRAME_H

#include "abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 4)

enum avs_cx_plane_id {
  AVS_CX_PLANE_DEFAULT = 0,
  AVS_CX_PLANE_Y = 1,
  AVS_CX_PLANE_U = 2,
  AVS_CX_PLANE_V = 4,
  AVS_CX_PLANE_A = 16,
  AVS_CX_PLANE_R = 32,
  AVS_CX_PLANE_G = 64,
  AVS_CX_PLANE_B = 128
};

enum avs_cx_frame_access { AVS_CX_FRAME_ACCESS_READ = 1, AVS_CX_FRAME_ACCESS_WRITE = 2 };

struct avs_cx_frame_ops_v1;

typedef struct avs_cx_frame_ref_v1 {
  void *object;
  const struct avs_cx_frame_ops_v1 *operations;
} avs_cx_frame_ref_v1;

// A non-null frame ref owns one reference. Copying it requires retain and the
// owner must eventually call release exactly once. The operations table is
// immutable and outlives every reference that points to it.

typedef struct avs_cx_plane_v1 {
  uint32_t struct_size;
  uint32_t flags;
  void *data;
  int32_t pitch;
  int32_t row_size;
  int32_t height;
  int32_t reserved0;
  uint64_t reserved[2];
} avs_cx_plane_v1;

// A plane is a borrowed view into the host frame: no pixels are copied. The
// pointer stays valid while its frame reference is held and until an operation
// that replaces that frame. WRITE access is valid only after make_writable.

typedef struct avs_cx_frame_ops_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void(AVS_CX_CALL *retain)(void *object);
  void(AVS_CX_CALL *release)(void *object);
  avs_cx_status(AVS_CX_CALL *get_plane)(void *object, uint32_t plane, uint32_t access,
                                        avs_cx_plane_v1 *plane_out, avs_cx_error_v1 *error_out);
  avs_cx_status(AVS_CX_CALL *get_pixel_type)(void *object, uint32_t *pixel_type_out);
  void *reserved[8];
} avs_cx_frame_ops_v1;

// retain, release, get_plane, and get_pixel_type are mandatory in v1.

typedef struct avs_cx_frame_feature_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t frame_ops_version;
  uint32_t frame_ref_size;
  uint64_t reserved[4];
} avs_cx_frame_feature_v1;

#pragma pack(pop)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // AVS_CX_FRAME_H
