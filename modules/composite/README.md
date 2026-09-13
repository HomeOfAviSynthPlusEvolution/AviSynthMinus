# Composite host adapter

The `third_party/composite` submodule owns the C/Highway pixel kernels. AVS keeps
script registration, format conversion, frame properties, copy-on-write, clip
lengths and threading. `Merge`, `MergeLuma`, `MergeChroma`, `Dissolve`, the blending
path of `ConvertFPS`, `Layer` and all `Overlay` modes call the library.

Each dispatch intersects the library targets with `GetCPUFlagsEx()` through
`AvsSimd`. This includes ARM64 NEON, dot-product and SVE restrictions. `SetMaxCPU`
`none` selects C without changing Highway's process-wide state. Per-frame scratch
and immutable kernel tables allow concurrent filter evaluation.

## Numerical behavior

The adapter uses the independent library's normalized arithmetic. This is a
development-line behavior change, not a promise of bit-identical legacy output:

- Merge uses continuous weight and final nearest rounding. Its existing near-zero,
  near-one and half-weight fast paths remain. Old C/SIMD weight-scale differences
  no longer determine pixels. Dissolve and ConvertFPS use continuous weight.
- Layer integer opacity is rounded to `0..(2^bits-1)`, then combined with alpha
  on that same scale. Products divide by the maximum code, preserving full-scale
  endpoints. Monochrome YUV multiplication fades chroma halfway toward neutral.
- Overlay Blend and Multiply use continuous opacity/masks. Integer artistic YUV
  modes use code-scale opacity, nearest intermediate rounding, then the existing
  luma overshoot desaturation rule. Exclusion divides by the maximum code rather
  than `2^bits`. These replace the old truncation/256 formulas; low-code values
  may differ. Explicit `blend_compat` still uses the historical integer formula.
- Float color values remain unclipped where the library's operation permits
  excursions. Float masks use nonnegative opacity values.

SIMD rounding allowances are relative to the **new C reference**, not to legacy
AviSynth output. Eligible continuous-weight integer paths (including Merge,
Dissolve/ConvertFPS blending and Overlay Blend) permit at most **1 LSB per call**;
integer YUV Multiply also permits 1 LSB at interior opacity. Integer CODE-weight
operations, including ordinary integer Layer blending, do not inherit this
allowance. Exact weight endpoints retain the operation's documented semantics.

Selected contiguous F32 masked MIX/PRODUCT and YUV Multiply paths permit
`16 * FLT_EPSILON * max(1, S)` error, where `S` is the operation's magnitude scale.
This is at most about `1.91e-6` for normalized nonnegative inputs. The precise
layout, endpoint and fallback conditions are in the pinned library's
[Numerical behavior](../../third_party/composite/NUMERICS.md). Other operations
must not be assigned this tolerance indiscriminately.

Repeated operations can accumulate rounding error. `SetMaxCPU("none")` selects
the C reference for these kernels; it does not restore legacy formulas. Increasing
integer working bit depth before processing reduces the normalized size of a
one-code error. See also the [user-facing precision notes](../../distrib/docs/english/source/avisynthdoc/corefilters/merge.rst).

Layer samples subsampled alpha and luminance guides from the full source image,
including the phase of a clipped overlap. Chroma decisions run before luma writes.
YUV alpha stays unchanged; legacy YUV Lighten/Darken continues to ignore source
alpha. RGB operations use source alpha; planar Fast preserves destination alpha,
while packed Fast averages it. Empty overlaps use checked wide intersection math.

`tests/composite_api` tests real registered filters using independent formulas,
source snapshots, frame properties, packed/planar layouts, high bit depths, scalar
and native dispatch, clipped alpha sampling and extreme offsets. Standalone tests
add C ABI, concurrency, guard pages and per-target equivalence checks. The replaced host Overlay blend and SSE4.1/AVX2 Multiply kernels, headers and
exclusive raw-kernel tests have been removed, together with the disabled legacy
Merge kernel suite. The disabled Layer raw-kernel suite and its obsolete blend-formula tests have
also been removed. Historical code and hashes remain in Git. The retained Layer
helper-filter suite is enabled and covers Mask, ColorKeyMask, ResetMask,
ShowChannel, MergeRGB, Invert and Subtract using current host interfaces. Public Overlay/Merge/Layer tests and independent Composite tests cover the
active implementation. Overlay's 444 format conversion still serves production
host adaptation and is retained.

Additional registered-filter tests exercise Prefetch with clipped YUVA Layer,
nonsequential frame requests, frame properties and immutable sources, plus
interleaved C/native masked Overlay calls checked against an independent formula.
Dissolve tests cover overlap lengths 1/2/3; ConvertFPS tests cover doubled frame
rates, intermediate blends and final-frame extension through the source cache.
Both check pixels, alpha, frame properties, timing and source immutability across
seven packed/planar formats under C/native dispatch.

## Dependency

Composite is published at
https://github.com/HomeOfAviSynthPlusEvolution/AviSynthComposite.git and pinned
through the `third_party/composite` submodule. Initialize it with
`git submodule update --init --recursive`. The parent Git revision pins the
required source version; use matching headers and sources.
