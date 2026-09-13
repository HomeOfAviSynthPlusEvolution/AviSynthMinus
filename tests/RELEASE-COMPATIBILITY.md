# Test compatibility on release/0.2

The test import in `8a90cbaf` commented out `layer`, `layer_filter`, and
`merge` together. These tests used interfaces and semantics from the newer
master refactor, while this branch retained the older stable implementation.
Enabling the imported targets unchanged fails to compile, but that does not
make every test in those directories inapplicable.

The compatible tests are now built and registered with CTest. Direct Intel
kernel targets (`layer` and `merge`) require `ENABLE_INTEL_SIMD`; the filter
target is unconditional. Ordinary regression tests belong in their filter or
kernel directories, not in `findings`.

## Adaptations and removed tests

- Use the release `Layer` constructor and its existing MPEG1/MPEG2 placement
  values. The enum is private to `layer.cpp` on this branch.
- Use the release mask-value constructor for the existing packed ResetMask
  test. Remove the test for the unavailable ResetMask opacity overload.
- Remove Layer tests for unavailable AVX2 kernels, the newer exported generic
  YUV/planar RGB/packed blend APIs, separate-mask blending, the newer plane
  Invert API, `mulovr`, and TOPLEFT placement. Remove their unused helpers.
  Existing SSE2 kernels and applicable filter-level tests remain covered.
- Adapt weighted Merge calls to the old row-byte and weight conventions:
  scalar integer weights use 16 bits, SIMD weights use 15 bits. Reference
  calculations and output hashes remain unchanged. Remove unavailable float
  AVX2 weighted-merge and average variants.
- Supply aligned rows to legacy kernels that use aligned loads/stores, while
  retaining odd widths, scalar tails, padding guards, and source immutability
  checks. Kernels supporting unaligned input retain their unaligned cases.
- Compare active pixels across separately allocated output frames in
  ShowChannel and MergeRGB tests. Their uninitialized padding is not an
  output contract. Keep whole-frame snapshots when checking that input
  frames were not modified.

## Pending behavior differences

The following 14 instances retain their imported expectations and are marked
`DISABLED_`, rather than changing their references to agree with release.
Parameterized suites separate these instances from passing cases. Each
disabled test or instantiation also has a local reason comment.

| Cases | Count | Observed release behavior versus imported expectation |
|---|---:|---|
| ResetMask negative mask | 1 | Negative mask selects the default; test expects rejection. |
| Integer Invert chroma | 1 | `255-U`; test expects `min(255, 256-U)`. |
| Integer alpha blending: YUVA420 Add, RGB32 Add, BGR64 Add, RGBAP16 Mul | 4 | Power-of-two scaling/rounding; tests use maximum-code-value scaling. |
| YUVA float Lighten | 1 | Overlay alpha is ignored; test applies it. |
| YUV float Mul with `use_chroma=false` | 2 | Chroma is neutralized at half strength; tests expect full strength. |
| RGBPS base with RGBAPS overlay: Mul, Add (two cases), Lighten | 4 | Constructor rejects differing formats; tests expect blending. |
| RGBAPS Fast | 1 | Destination alpha is preserved; test averages alpha. |

These differences still need a separate compatibility decision before changing
production behavior or accepting new expected values.

## Validation

MSVC 19.51, x64 Release, static AvsCore, Intel SIMD enabled:
118 tests passed across `layer`, `layer_filter`, `merge`, and
`colorkeymask_filter`; CTest reported 14 disabled instances explicitly.
An explicit diagnostic run of all 14 disabled instances reproduced all 14
failures, confirming that no passing instances were disabled.

```powershell
cmake --build build/review-tests --config Release --target layer_tests layer_filter_tests merge_tests colorkeymask_filter_tests -j 8
ctest --test-dir build/review-tests -C Release -R '^(layer\.|layer_filter\.|merge\.|colorkeymask_filter\.)' --output-on-failure
# Deliberately reproduce the pending failures:
& build/review-tests/tests/layer_filter/Release/layer_filter_tests.exe --gtest_also_run_disabled_tests --gtest_filter='*DISABLED_*'
```
