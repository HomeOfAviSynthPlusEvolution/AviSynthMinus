AviSynthMinus 0.2 series
========================

This page covers the 0.2.x series. Changes are grouped by release, newest first.
Earlier changes are listed in :doc:`changelist001`.

0.2.1
-----

Plugin interfaces
~~~~~~~~~~~~~~~~~

- Add the V12 ``ApplyMessageEx`` API with an explicit UTF-8 selector. Legacy
  ``ApplyMessage`` retains its encoding behavior; rendering keeps the existing
  font metrics rather than importing all upstream text-rendering changes.
- Add ``GetCPUFlagsEx`` and ``avs_get_cpu_flags_ex`` for 64-bit CPU capability
  masks, and expose L2 cache size through ``AEP_CACHESIZE_L2``.
- Notify filters of Prefetch concurrency through ``CACHE_INFORM_NUM_THREADS``.
  Serialized filters receive a concurrency hint of one.
- Add optional CX message rendering through ``AVS_CX_FEATURE_MESSAGE`` v1.
  The SDK uses this feature for ``ApplyMessage`` and ``ApplyMessageEx`` and
  preserves the source frame while returning a writable output frame.
  Existing CX v1 layouts remain unchanged. On older CX hosts, the new SDK
  reports an unsupported operation only when message rendering is requested;
  unrelated operations remain available.
- Forward the full host-approved CPU mask through the existing 64-bit CX
  environment query, without adding a new feature table.

The native C++ additions retain the stable V11 method slots and the global-lock
slots introduced in 0.2.0. Plugins using the unstable ``IScriptEnvironment2``
or ``INeoEnv`` extensions may require rebuilding because added base methods
shift their method slots. CX uses a separate C transport. See the
`plugin compatibility notes`_ for the tested interfaces and limitations.

CPU detection
~~~~~~~~~~~~~

- Extend ``SetMaxCPU`` to the V12 CPU flags. Restriction masks must not enable
  capabilities absent from the detected host mask.
- Correct AVX-512 operating-system state checks and half-precision feature
  detection while retaining upstream AVX-512 restriction groups.
- Extend ARM64 capability and L2 cache detection across platforms, including
  native FreeBSD queries. Detect SVE2.1 from operating-system capabilities
  and clear I8MM and SVE2.1 when excluded by ARM CPU limits.

Bugfixes
~~~~~~~~

- Validate ``Expr`` executable-memory allocations, release JIT mappings,
  correct JIT operand alignment, and skip initialization when no JIT buffer
  is allocated.
- Release ``Tone`` waveform generators with their owning clip and avoid
  signed overflow when generating YUY2 source frames.
- Use an unsigned fractional scale in audio resampling to avoid signed-shift
  undefined behavior.
- Avoid invalid integer setup for float video formats, packed RGB pixel
  overreads and unaligned accesses, and AVX2 resampling alignment violations.
  Correct packed-pixel writes in ``AddBorders``.
- Isolate plugin symbols from shared-core exports on Linux and FreeBSD to
  prevent symbol interposition across plugin boundaries.

Build and testing
~~~~~~~~~~~~~~~~~

- Add compatibility tests built from pinned V11, partial V12, full V12, and
  released CX SDK headers, including a host without the message feature.
- Extend coverage for message rendering, source-frame preservation, CPU
  masks, cache queries, and Prefetch notifications. Add native FreeBSD test
  matrices and enable C for shared-library API tests.
- Correct regression-test expectations and fixtures for Overlay, packed RGB
  extraction, levels, histogram, and sanitizer allocation/alignment checks.

0.2.0
-----

Start the 0.2 series with the same implementation as 0.1.3; only the fork
version changes. See :doc:`changelist001` for its global-lock API, experimental
CX adapter, bugfixes, and test coverage.

.. _plugin compatibility notes:
    https://github.com/HomeOfAviSynthPlusEvolution/AviSynthMinus/blob/release/0.2/tests/v12/README.md
