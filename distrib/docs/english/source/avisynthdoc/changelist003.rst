AviSynthMinus 0.3 series
=======================

This page covers the 0.3.x mainline series. Changes are grouped by release,
newest first.

0.3.1
-----

These notes describe the changes for 0.3.1 relative to 0.1.3 / 0.2.0.

Cross-architecture processing
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Audio sample conversion, video conversion/resizing, and compositing now use
independent libraries with ordinary C/C++ and Google Highway kernels. Many paths
formerly implemented with x86 SSE/AVX can now use native ARM64 SIMD. Runtime
selection intersects compiled targets, CPU/OS capabilities, and the host's CPU
restrictions; NEON and conditional SVE/SVE2 paths are supported by this policy.
Not every operation is vectorized: Floyd–Steinberg error diffusion remains scalar,
and kernels may fall back to C where the required operations are unavailable.

InternalFilters also moves filter families out of the core: focus, rotation,
cropping, source generation, masks/channels, compositing adapters, histogram,
frame/field operations, color adjustment, and convolution. Splitting a filter
into a module does not by itself establish a speedup or a new host integration.

The five modules are built and linked with the core. No separate module plugins
need to be installed. Audio, Video, Composite, and Iris expose C interfaces usable
by other hosts with matching source versions and suitable adapters. This release
does not introduce a complete plugin for a different host.

Expr and IrisExpr
~~~~~~~~~~~~~~~~~

``Expr`` now uses Iris; ``IrisExpr`` exposes the new interface without old optimization
flags. Existing Expr parameter names, types, and positions remain accepted.
``optAvx2``, ``optSingleMode``, ``optSSE2``, and ``optVectorC`` are ignored. New arguments
``backend``, ``optimize``, and ``lut_max_mb`` follow the old arguments.

Available backends are ``scalar``, ``llvm``, ``sleef``, and ``sleef-fast``, depending on
build dependencies. Iris JIT currently does not honor ``SetMaxCPU``; select
``backend="scalar"`` for the interpreter. LLVM/SLEEF and optimization settings can
change floating-point results. The standard mainline configuration uses SLEEF;
Windows ARM64 uses LLVM without SLEEF, and the XP configuration uses scalar Iris.

Iris uses binary32 intermediates, stricter parsing, half-away-from-zero ``round``,
and half-up integer output rounding with saturation and NaN mapped to zero.
Negative ``sqrt`` returns positive zero; NaN remains NaN. Empty plane expressions
copy input planes, so a changed output bit depth needs explicit expressions.
Manual LUTs have a default 256 MiB budget per filter instance; ``lut_max_mb`` adjusts
it. See the `Iris reference <https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/blob/master/third_party/iris/README.md>`_ for exact restrictions.

Numerical changes in migrated filters
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Audio conversion retains truncation, saturation, NaN-to-silence, and packed S24
  semantics. This module changes sample format, not sample rate or channel mixing.
- Video conversion uses shared matrix/depth/layout plans. Integer matrix output
  no longer depends on choosing historical fixed-point versus float SIMD paths.
  Float YUV-to-RGB retains out-of-range values; packed RGB alpha is converted
  independently of color range. Float-to-integer depth conversion saturates safely.
- Merge, Dissolve, ConvertFPS blending, Layer, and Overlay use the Composite
  arithmetic contracts. Continuous weights, endpoint scaling, and rounding can
  differ from old formulas. Eligible integer SIMD operations allow up to one code
  value per call relative to the new C reference, not relative to old releases.
  ``SetMaxCPU("none")`` selects new C arithmetic; it does not restore old formulas.
  Overlay's explicit ``blend_compat`` retains its historical integer blend formula.

Read the `Video adapter <https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/blob/master/modules/video_convert/README.md>`_ and
`Composite precision notes <https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/blob/master/modules/composite/README.md>`_ for operation-specific
behavior. Repeated operations can accumulate rounding differences.

SDK and platform compatibility
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

V12 support adds UTF-8 message rendering, full-width CPU flags, L2 cache queries,
and Prefetch thread-count notifications. Legacy message rendering retains its
encoding behavior. This does not import every upstream filter or font-rendering
change. CX exposes optional message rendering while preserving its released v1
layouts; callers must handle a missing optional feature on older hosts.

Stable V11 plugin slots and the existing V12 global-lock slots are retained.
Plugins using the unstable ``IScriptEnvironment2`` or ``INeoEnv`` extensions may need
rebuilding with matching headers. Native C++ ABI compatibility and the CX C
transport are separate integration paths. Module extraction alone does not
require every existing plugin to be rebuilt.

The SDK no longer supplies ``<avs/filesystem.h>``; replace that include with
``<filesystem>`` and use ``std::filesystem``. Complete source builds require CMake
3.24 or newer and a C++17 compiler/standard library. macOS now requires 15.0 or
newer on both Intel and Apple Silicon.

Fixes
~~~~~

- Keep InternalFilters Highway SIMD independent of ``ENABLE_INTEL_SIMD``;
  ARM64 builds no longer implicitly disable NEON. ``AIF_SCALAR_ONLY`` remains
  an explicit opt-out.
- Work around a GCC ARM64 compiler bug in loop unswitching by disabling
  that pass only for the VideoConvert resampling Highway source.
- Fix Iris float parsing on FreeBSD by including ``xlocale.h`` in both the
  capability probe and the locale-specific fallback.
- Prevent narrow RGB32 SSE2 Mask rows from reading before the buffer.
- Keep frame-property storage alive during self-assignment.
- Format large floating-point values without fixed-buffer overflow; preserve
  64-bit integers in Format.
- Reject oversized anonymous-function capture lists before writing past the limit.
- Bind Linux shared-core calls and linkage access to the intended implementation,
  avoiding client symbol interposition during public API and plugin loading.

Building, installation, and validation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Clone recursively and update pinned submodules after switching revisions.
GitHub's automatic source archives omit their contents. See the `repository build instructions <https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/blob/master/README.md#building-and-testing>`_ for dependencies and commands. Component license notices remain in their respective source directories.

The mod installer requires an existing official AviSynth+ 3.7.5 installation on
x64 Windows 10 or later, and replaces installed x86/x64 cores. Files-only packages
contain the core, not a complete SDK, plugin collection, or runtime redistributable.
Linux LLVM-enabled builds require the matching runtime; Windows/macOS builds embed
LLVM. Use the release asset list for the architectures actually distributed.

Routine CI configures regression and installed shared-API tests on Windows
x86/x64, Linux x64, and macOS ARM64. The release build matrix additionally includes
Linux ARM64, macOS Intel, Windows ARM64, and XP configurations; those build jobs do
not establish equivalent runtime coverage. No new cross-platform performance
measurement is claimed here. Existing `audio <https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/blob/master/third_party/audio_convert/PERFORMANCE.md>`_
and `video <https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/blob/master/third_party/video_convert/PERFORMANCE.md>`_ reports state their CPUs,
compilers, workloads, and numerical comparisons. Highway portability does not
imply a universal speedup or support for every platform Highway can target.
