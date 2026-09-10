// AviSynth CX plugin ABI. This file is C-compatible by design.
//
// ABI rules for every avs/cx header:
// - published v1 structures and callback signatures are immutable;
// - new capabilities use a new uint64_t feature key (and start at version 1);
// - structures cross the DLL boundary only by pointer, never by value;
// - the producer sets struct_size and zeros every reserved field;
// - C++ objects, exceptions, RTTI, allocators, and STL types never cross;
// - callbacks and tables remain valid for the lifetime documented by their
//   owning feature or reference.
#ifndef AVS_CX_ABI_H
#define AVS_CX_ABI_H

#include <stddef.h>
#include <stdint.h>

#define AVS_CX_ABI_VERSION_1 UINT32_C(1)

#if defined(_WIN32)
#if defined(_MSC_VER)
#define AVS_CX_CALL __cdecl
#define AVS_CX_EXPORT __declspec(dllexport)
#else
#define AVS_CX_CALL __attribute__((cdecl))
#define AVS_CX_EXPORT __attribute__((dllexport))
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define AVS_CX_CALL
#define AVS_CX_EXPORT __attribute__((visibility("default")))
#else
#define AVS_CX_CALL
#define AVS_CX_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 4)

typedef int32_t avs_cx_status;

enum avs_cx_status_code {
  AVS_CX_STATUS_OK = 0,
  AVS_CX_STATUS_FEATURE_NOT_FOUND = 1,
  AVS_CX_STATUS_INVALID_ARGUMENT = -1,
  AVS_CX_STATUS_ABI_MISMATCH = -2,
  AVS_CX_STATUS_PLUGIN_ERROR = -3,
  AVS_CX_STATUS_HOST_ERROR = -4,
  AVS_CX_STATUS_OUT_OF_MEMORY = -5,
  AVS_CX_STATUS_UNSUPPORTED = -6
};

typedef struct avs_cx_string_view_v1 {
  const char *data;
  uint32_t size;
  uint32_t reserved;
} avs_cx_string_view_v1;

typedef struct avs_cx_error_v1 {
  uint32_t struct_size;
  int32_t code;
  avs_cx_string_view_v1 message;
  uint64_t reserved[2];
} avs_cx_error_v1;

// Error messages are borrowed UTF-8 bytes. A consumer must copy them before
// its next call into the same provider on the same thread.

struct avs_cx_host_v1;

typedef avs_cx_status(AVS_CX_CALL *avs_cx_query_feature_fn)(void *host_context,
                                                            uint64_t feature_key,
                                                            uint32_t exact_version,
                                                            uint32_t minimum_struct_size,
                                                            const void **feature_out);

// query_feature is plugin-to-host only. exact_version identifies one frozen
// table layout; minimum_struct_size states how much of that layout the plugin
// will read. feature_out is NULL on every non-OK result. Returned feature
// tables are immutable and remain valid until plugin shutdown.

typedef struct avs_cx_host_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void *host_context;
  avs_cx_query_feature_fn query_feature;
  void *reserved[4];
} avs_cx_host_v1;

typedef struct avs_cx_call_context_v1 {
  uint32_t struct_size;
  uint32_t abi_version;
  const avs_cx_host_v1 *host;
  void *environment;
  void *reserved[4];
} avs_cx_call_context_v1;

// environment is an opaque per-call token. Plugins may only pass it back to a
// host callback during this call; they must not inspect or retain it.

typedef avs_cx_status(AVS_CX_CALL *avs_cx_plugin_init_cx1_fn)(const avs_cx_host_v1 *host);

#pragma pack(pop)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // AVS_CX_ABI_H
