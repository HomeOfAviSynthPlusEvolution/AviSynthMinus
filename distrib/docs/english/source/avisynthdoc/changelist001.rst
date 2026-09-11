AviSynthMinus 0.1 series
========================

This page covers the 0.1.x mainline series, starting with development based on
AviSynth+ 3.7.5. Changes are grouped by release, newest first. Minor versions
have separate change lists: ``changelist001`` covers 0.1.x (major 0 is written
as ``00`` in the filename), and :doc:`changelist002` covers 0.2.x.

Only changes included in AviSynthMinus are listed here. Selected upstream
fixes are identified separately; this series does not include all features
or optimizations described in the upstream 3.7.6 change list.

0.1.3
-----

Plugin interfaces
~~~~~~~~~~~~~~~~~

- Add global named locks for synchronization between plugins and script
  environments. The C++ API provides ``AcquireGlobalLock`` and
  ``ReleaseGlobalLock``; the C API provides ``avs_acquire_global_lock`` and
  ``avs_release_global_lock``. This ports the upstream global-lock addition
  and raises the interface version to 12; it does not import the entire
  upstream v12 feature set.
- Make global-lock operations exception-safe, contain exceptions at the C API
  boundary, and reclaim unused named locks without disrupting owners or waiters.
- Add the experimental CX plugin adapter. It provides a C callback boundary
  and a replacement C++ SDK so supported existing filter and factory source
  can be built with different C++ ABIs. Windows x64 tests exercise MSVC and
  MinGW-w64 GCC cores and plugins in all four combinations, including mixed
  plugin chains. ARM64 remains a target requiring validation.
- Preserve numeric value types, host clip/frame identity, copy-on-write
  behavior, and registration semantics across CX. Synchronize registration,
  restore plugin context after nested loads, and keep callback bindings alive
  for the functions that use them. Failed initialization removes its own
  registrations without removing unrelated nested-plugin registrations.

CX remains experimental on ``master``. It may become permanent, be restructured,
or be removed. Forward and backward plugin compatibility remain required:
refactoring or removal must preserve a compatible loading and execution path
for plugins that depend on it. This does not imply support for every existing
API operation. See the `CX SDK documentation`_ for the supported subset.

Bugfixes
~~~~~~~~

- Fix ``SaveString`` comparison of explicitly sized data containing embedded
  NUL bytes. Different byte sequences must not be merged merely because their
  prefixes before the first NUL match.

Testing
~~~~~~~

- Add global-lock contention, ownership, failure, and lifetime regressions.
  Run lock tests in separate processes with timeouts so deadlocks fail the
  test run instead of hanging it.
- Add CX cross-compiler tests for loading, registration, values, video/audio
  access, object lifetime, and concurrency, plus a transport smoke benchmark.

0.1.2
-----

Bugfixes from upstream
~~~~~~~~~~~~~~~~~~~~~~

- Bounds-check ``ArrayIns``, ``ArraySet``, and ``ArrayDel`` indices to prevent
  access violations on invalid input.
- Preserve the source color range when converting packed RGB between 8 and
  16 bits with ``ConvertToRGB24/32/48/64``.
- Correct the luma offset when converting limited-range RGB to full-range YUV.
- Avoid unintended clamping in the scalar float YUV-to-planar-RGB conversion.
- Make scalar ``Overlay`` multiply results agree with the SIMD implementation.

Additional fixes
~~~~~~~~~~~~~~~~

- Fix SSE2 tail handling for narrow planar RGB conversions, avoiding an
  underflow when the width is smaller than a SIMD block.
- Extend regression coverage for array bounds, color ranges, and scalar/SIMD
  conversion and overlay consistency.

Distribution and documentation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Add the AviSynthMinus mod installer and release packaging tools. The mod
  installer targets x64 Windows 10 or later with an existing AviSynth+ 3.7.5
  installation; it backs up replaced cores for restoration during uninstall.
- Add English, Simplified Chinese, and Japanese project READMEs describing
  release channels, installation, compatibility, and building.

0.1.1
-----

Initial tagged AviSynthMinus release, based on AviSynth+ 3.7.5. The changes
below include the initial 0.1-series development preceding this tag.

Selected fixes from upstream
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Correct ``Abs`` handling of 64-bit integers and fully initialize the C API's
  undefined value (``avs_void``).
- Keep ``SetMaxCPU`` settings local to each script environment.
- Preserve the working directory when ``Prefetch`` creates additional
  instances of ``MT_MULTI_INSTANCE`` filters, so relative paths keep working.
- Improve Windows plugin-loading error text and use UTF-8 filenames in
  ``AviSource`` and ``Import`` error messages.
- Clamp ``Animate`` interpolation to its start/end range.
- Avoid creating cache objects for ``GetFrame`` calls made in a filter
  constructor; prune dead subframes and prevent uncontrolled cache growth
  after ghost hits during repeated access patterns.
- Fix ``ColorBars`` YUV matrix metadata and alpha copying when static frames
  are disabled.
- Fall back to scalar ``Greyscale`` conversion when RGB matrix coefficients
  exceed the signed 16-bit SIMD range.
- Fix 24-bit audio corruption in ``Reverse``, buffer offsets and overlapping
  copies in ``TimeStretch``, and temporary-buffer allocation/overflow checks
  in ``ConvertAudio``.
- Fix array deallocation in source/audio readers, use automatic ownership for
  ``Spline`` and function-construction buffers, and check parser argument
  limits before indexing argument names.
- Fix ``RGBAdjust`` LUT buffer overruns and leaks, and initialize its
  conditional-processing setting.
- Correct AVX2 YV24 source alpha packing and scalar 16-bit ``TemporalSoften``
  rounding.
- Correct ``Expr`` input-plane selection and Vector-C relative-row addressing.
- Use 64-bit accumulators in ``Compare``, correct RGB24/RGB48 normalization,
  and initialize the RGB48/RGB64 mask for deterministic results.

Additional fixes
~~~~~~~~~~~~~~~~

- Validate non-finite filter parameters, inverted ``Limiter`` intervals,
  non-finite ``GeneralConvolution`` matrices, invalid selection/editing
  indices, and invalid frame-rate divisors or rational rates.
- Reject invalid ``Tone`` sample rates/channel counts, oversized ``BlankClip``
  color arrays, invalid ``SpatialSoften`` radii, insufficient ``Histogram``
  audio-bar width, zero ``FixLuminance`` slope, and invalid ``SkewRows`` width.
- Clamp requests at clip boundaries in multi-clip conditional/layer filters,
  ``DoubleWeave``, and ``PeculiarBlend``. Validate ``YToUV`` component sources
  and ``Compare`` frame ranges, including empty summaries.
- Improve YUV/RGB matrix rounding and precision. Make ``ConvertBits`` respect
  per-frame color ranges and retain ordered-dither depth across automatic
  intermediate conversions.
- Convert NaN audio samples to silence consistently across conversion paths.
- Correct channel extraction and integer multiply endpoints in ``Layer``;
  copy alpha panels in ``ShowFiveVersions``.
- Make fully opaque ``Overlay`` masks reproduce unmasked results and align
  luma comparisons. Guard narrow YV12 SIMD conversions and explicitly reject
  float input for unsupported add/subtract modes.
- Normalize ``TemporalSoften`` thresholds and center float ``Tweak`` dither
  bias at each strength.
- Preserve double precision in conditional comparisons and string arrays in
  frame properties. Reject zero-length interpolation and invalid ``propShow``
  coordinates; process every packed-RGB pixel in conditional SSE2 tails.
- Map copied ``Expr`` source planes using each input's format.
- Correct debug memory checks and restore chroma-alignment state after errors.

Build environment and testing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Introduce separate AviSynthMinus version information while retaining the
  AviSynth+ 3.7.5 compatibility baseline in version reporting. Correct Windows
  DLL version metadata.
- Integrate a CMake unit-test suite with regression coverage for core, script,
  audio, conversion, and filter behavior.
- Build shared libraries on modern Windows, Linux, and macOS targets; add a
  Windows XP build and adjust Expr compilation for the legacy MSVC toolset.
  Separate routine test builds from manually requested full release builds.

Report bugs at the `AviSynthMinus issue tracker`_.

.. _CX SDK documentation:
    https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/blob/master/avs_core/include/avs/cx/README.md
.. _AviSynthMinus issue tracker:
    https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/issues
