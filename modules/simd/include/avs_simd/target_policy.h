#ifndef AVS_SIMD_TARGET_POLICY_H
#define AVS_SIMD_TARGET_POLICY_H

#include "avs_simd/highway_config.h"
#include <cstdint>

namespace avs_simd {

// Explicit constant for standard C/C++ fallback.
// Highway dynamic targets are single-bit powers of 2 (>= 1).
// Zero unambiguously denotes non-SIMD scalar fallback.
constexpr int64_t TARGET_C_FALLBACK = 0;

// Converts raw AviSynth CPU flags (e.g. from IScriptEnvironment::GetCPUFlags())
// into a Highway target bitmask representing which SIMD targets are permitted
// by the AviSynth runtime environment.
int64_t AvsFlagsToHighwayMask(int avs_cpu_flags);

// Pure function calculating the target bit from the intersection of three sets:
//   1. avs_cpu_flags: AVS CPU feature flags (CPUF_*)
//   2. hardware_supported: targets supported by hardware/OS (e.g. hwy::SupportedTargets())
//   3. generated_targets: targets compiled into the caller translation unit (HWY_TARGETS)
//
// Returns the single highest-priority HWY_* target bit from the intersection,
// or TARGET_C_FALLBACK (0) if no SIMD target is viable.
int64_t ChooseTarget(int avs_cpu_flags, int64_t hardware_supported, int64_t generated_targets);

// Convenience overload that queries the current machine's hardware-supported targets.
int64_t ChooseTarget(int avs_cpu_flags, int64_t generated_targets);

// Queries hardware/OS supported targets on the current machine without modifying state.
int64_t GetHardwareSupportedTargets();

// Returns human-readable name of target ("C" for TARGET_C_FALLBACK, or hwy::TargetName).
const char* TargetName(int64_t target);

} // namespace avs_simd

#endif // AVS_SIMD_TARGET_POLICY_H
