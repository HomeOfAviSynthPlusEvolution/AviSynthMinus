#ifndef AVS_SIMD_HIGHWAY_CONFIG_H
#define AVS_SIMD_HIGHWAY_CONFIG_H

// Common configuration header for Highway in AviSynth+.
// Include this before any <hwy/highway.h> or <hwy/foreach_target.h> in a
// consumer translation unit. The hwy CMake target also propagates the static
// linkage definition to consumers.

#if defined(_WIN32) || defined(WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

// Ensure Highway uses static linkage annotations across all consumers
#ifndef HWY_STATIC_DEFINE
#define HWY_STATIC_DEFINE
#endif

#endif // AVS_SIMD_HIGHWAY_CONFIG_H
