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

Layer samples subsampled alpha and luminance guides from the full source image,
including the phase of a clipped overlap. Chroma decisions run before luma writes.
YUV alpha stays unchanged; legacy YUV Lighten/Darken continues to ignore source
alpha. RGB operations use source alpha; planar Fast preserves destination alpha,
while packed Fast averages it. Empty overlaps use checked wide intersection math.

`tests/composite_api` tests real registered filters using independent formulas,
source snapshots, frame properties, packed/planar layouts, high bit depths, scalar
and native dispatch, clipped alpha sampling and extreme offsets. Standalone tests
add C ABI, concurrency, guard pages and per-target equivalence checks. Older
low-level Overlay SIMD tests remain as reference coverage; those kernels are no
longer used by Overlay's production dispatch.

## Dependency

Composite is published at
https://github.com/HomeOfAviSynthPlusEvolution/AviSynthComposite.git and pinned
through the `third_party/composite` submodule. Initialize it with
`git submodule update --init --recursive`. No release tag or version bump is part
of this integration.
