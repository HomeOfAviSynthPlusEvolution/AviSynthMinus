#ifndef TESTS_SIMD_DISPATCH_PROBE_H
#define TESTS_SIMD_DISPATCH_PROBE_H

#include <cstddef>
#include <cstdint>

namespace probe {

// Probe kernel function pointer type.
// Takes source, destination, count, and writes out the executing target name.
using ProbeKernelFn = void (*)(const float* src, float* dst, size_t count, const char** out_target_name);

// Standard C implementation (ordinary function, not Highway scalar)
void ProbeKernel_C(const float* src, float* dst, size_t count, const char** out_target_name);

// Module resolver: maps AVS CPU flags to an ordinary function pointer
ProbeKernelFn ResolveProbeKernel(int64_t avs_cpu_flags);

// Resolves a function pointer for a specific chosen target bit (or TARGET_C_FALLBACK)
ProbeKernelFn ResolveProbeKernelForTarget(int64_t target);

// Diagnostic helpers
int64_t GetProbeKernelChosenTarget(int64_t avs_cpu_flags);
int64_t GetProbeKernelCompiledTargets();

} // namespace probe

#endif // TESTS_SIMD_DISPATCH_PROBE_H
