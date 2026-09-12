#ifndef TESTS_SIMD_DISPATCH_PROBE_CONSUMER_H
#define TESTS_SIMD_DISPATCH_PROBE_CONSUMER_H

#include <cstddef>
#include <cstdint>

namespace consumer {

// Second consumer module in a distinct namespace, processing int32_t buffers
using ConsumerKernelFn = void (*)(const int32_t* src, int32_t* dst, size_t count, const char** out_target_name);

// Ordinary C kernel
void ConsumerKernel_C(const int32_t* src, int32_t* dst, size_t count, const char** out_target_name);

// Module resolver
ConsumerKernelFn ResolveConsumerKernel(int64_t avs_cpu_flags);

// Diagnostic helpers
int64_t GetConsumerKernelChosenTarget(int64_t avs_cpu_flags);
int64_t GetConsumerKernelCompiledTargets();

} // namespace consumer

#endif // TESTS_SIMD_DISPATCH_PROBE_CONSUMER_H
