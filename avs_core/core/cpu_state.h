// Pure decoding helpers for OS-gated x86 SIMD capabilities.
#ifndef AVSCORE_CPU_STATE_H
#define AVSCORE_CPU_STATE_H
#include <cstdint>
#include <avs/cpuid.h>
namespace avs_cpu {
inline bool HasAvx512State(uint32_t xcr0) { return (xcr0 & 0xE6u) == 0xE6u; }
inline int64_t HalfPrecisionFlags(uint32_t leaf7_edx, uint32_t leaf7_1_eax) {
  return ((leaf7_edx & (1u << 23)) ? CPUF_AVX512FP16 : 0) |
         ((leaf7_1_eax & (1u << 5)) ? CPUF_AVX512BF16 : 0);
}
}
#endif
