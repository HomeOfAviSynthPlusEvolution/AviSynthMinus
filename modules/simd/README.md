# AviSynth+ SIMD Infrastructure (`modules/simd`)

This module provides common SIMD configuration and target selection policy for
AviSynth+ using vendored Google Highway 1.4.0 (`third_party/highway`).

## Design Principles

1. **Instance-Level Dispatch**: Each filter instance selects an ordinary function pointer at initialization time based on its `IScriptEnvironment` CPU restrictions. No global Highway dispatch state (`DisableTargets`, `SetSupportedTargetsForTest`, `GetChosenTarget().Update`) is modified.
2. **Three-Set Intersection**: Selected targets are strictly computed from the intersection of:
   - Targets supported by the physical hardware and OS (`hwy::SupportedTargets()`).
   - Targets permitted by AviSynth CPU flags (`IScriptEnvironment::GetCPUFlags()`).
   - Targets actually compiled into the calling kernel translation unit (`HWY_TARGETS`).
3. **Dedicated C Fallback**: When `SetMaxCPU("none")` is specified or when no SIMD target is viable, the policy returns `TARGET_C_FALLBACK` (0). The module's resolver returns its own standard C/C++ function pointer rather than relying on Highway's scalar emulation.
4. **Decoupled from `ENABLE_INTEL_SIMD`**: The legacy `ENABLE_INTEL_SIMD` CMake option manages legacy x86 intrinsics only. Highway SIMD targets are governed by toolchain capabilities and architecture detection.

## Compiled target enumeration

`avs_simd/targets.inc` is a repeatable X-macro include. After including
`hwy/highway.h`, define `AVS_SIMD_TARGET(target, choose)`, include the file,
and undefine the macro. It enumerates compiled SIMD targets using the current
translation unit's `HWY_TARGETS`; each `choose` macro maps a function name to
its target-specific pointer. Consumers own their function tables and C fallback.
`HWY_SCALAR` and `HWY_EMU128` are intentionally omitted.

## Target Mapping Table (x86 / x86_64)

| Highway Target | Bit Value | Required AviSynth CPU Flags | Highway Hardware Detection Responsible For | Fallback |
| :--- | :--- | :--- | :--- | :--- |
| `HWY_SSE2` | `1LL << 14` | `CPUF_SSE2` | `kSSE`, `kSSE2` (always present on x64) | Module C Kernel |
| `HWY_SSSE3` | `1LL << 12` | `CPUF_SSE2`, `CPUF_SSSE3` | `kSSE3`, `kSSSE3` | Module C Kernel |
| `HWY_SSE4` | `1LL << 11` | `CPUF_SSE2`, `CPUF_SSSE3`, `CPUF_SSE4_1`, `CPUF_SSE4_2`, `CPUF_AES` | `kCLMUL` | Module C Kernel |
| `HWY_AVX2` | `1LL << 9` | `HWY_SSE4` reqs + `CPUF_AVX`, `CPUF_AVX2`, `CPUF_FMA3`, `CPUF_F16C` | `kLZCNT`, `kBMI`, `kBMI2` | Module C Kernel |
| `HWY_AVX3` | `1LL << 8` | `HWY_AVX2` reqs + `CPUF_AVX512F`, `CPUF_AVX512VL`, `CPUF_AVX512DQ`, `CPUF_AVX512BW`, `CPUF_AVX512CD` | XSAVE state validation for ZMM registers | Module C Kernel |
| `HWY_AVX3_DL` | `1LL << 7` | `HWY_AVX3` reqs + `CPUF_AVX512VBMI` | `kVNNI`, `kVPCLMULQDQ`, `kVBMI2`, `kVAES`, `kPOPCNTDQ`, `kBITALG`, `kGFNI` | Module C Kernel |
| `HWY_AVX3_ZEN4`| `1LL << 6` | `HWY_AVX3_DL` reqs | `kAVX512BF16` (not defined in AVS flags) | Module C Kernel |
| `HWY_AVX3_SPR` | `1LL << 4` | `HWY_AVX3_ZEN4` reqs | `kAVX512FP16` (not defined in AVS flags) | Module C Kernel |
| `HWY_AVX10_2` | `1LL << 3` | `HWY_AVX3_DL` reqs | AVX10/APX and 512-bit-vector checks | Module C Kernel |

### Notes on Flag Checks

- Target names cannot be mapped blindly: `HWY_AVX2` requires `FMA` and `F16C` in addition to `AVX2`. If an environment disables `FMA3` or `F16C`, `HWY_AVX2` will not be selected.
- Features without corresponding AVS CPU flags (e.g. `BMI`, `BMI2`, `LZCNT`, `CLMUL`, `AVX512BF16`, `AVX512FP16`) are validated by Highway's runtime CPUID check.
- `HWY_AVX10_2` is gated by the strongest AVS-visible x86 tier. AviSynth does
  not expose AVX10 or APX flags; requiring the AVX-512-visible tier preserves
  the meaning of `SetMaxCPU("avx2")`, while Highway checks the actual AVX10
  requirements.

## Non-x86 Architectures (ARM, RISC-V, etc.)

On non-x86, `SetMaxCPU("none")` records zero CPU flags and selects the module's
C fallback. All other settings (including an empty string) restore
`CPUF_FORCE`, the default native value. Matching `none` ignores case and
surrounding whitespace; x86 feature lists are not interpreted on non-x86.

For native dispatch, Highway selects from hardware-supported targets compiled
into the kernel. If no SIMD target is available, the module uses C. This policy
is independent of `ENABLE_INTEL_SIMD` and does not require architecture-specific
AVS vector flags.

## MSVC Target Generation

On the current MSVC x64 configuration, Highway 1.4.0 generates `HWY_SSE2`,
`HWY_SSSE3`, `HWY_SSE4`, and `HWY_AVX2` (mask `0x5a00`). Upstream excludes the
AVX-512 and AVX10 families for this compiler through its own compatibility
blocklist. Other toolchains may generate additional targets; the resolver only
chooses targets that are also hardware-supported and allowed by AVS flags.
