# V12 plugin compatibility

The stable `IScriptEnvironment` interface includes the V12 global-lock methods,
`ApplyMessageEx`, and `GetCPUFlagsEx`, in upstream virtual-method order. The C
CPU entry point is `avs_get_cpu_flags_ex`.
`CACHE_INFORM_NUM_THREADS` and `AEP_CACHESIZE_L2` use the existing cache-hint and
environment-property channels.

This work targets plugin interface compatibility. It does not import the entire
upstream filter implementation. In particular, message rendering keeps the
existing renderer and font metrics, adding the UTF-8 selector without importing
variable-width BDF or font-measurement changes. Legacy `ApplyMessage` retains
its existing encoding behavior.

## Binary boundaries

- Stable V11 plugins continue to use their original method slots.
- Minus 0.2.0 plugins can use the two existing V12 global-lock slots.
- Complete upstream V12 plugins can also call the new message and CPU methods.
- `IScriptEnvironment2` and `INeoEnv` are unstable extended interfaces. Adding
  methods to their base interface shifts derived slots; old binaries using
  these extensions may need rebuilding against the matching SDK.
- These C++ claims apply to the supported native compiler ABI. CX provides the
  separate C transport for plugins built with a different compiler.

CX keeps its released v1 layouts unchanged. Its existing 64-bit CPU query now
forwards the full host-approved mask; unknown capabilities remain zero.
The optional `AVS_CX_FEATURE_MESSAGE` v1 table provides message rendering with
host-owned output frames and copy-on-write input preservation. A new SDK on a
host without that feature reports an unsupported operation when rendering is
requested. Existing unrelated operations remain available.

## Coverage

`sdk/README.md` records the original, pinned headers used to compile independent
V11, partial V12, and full V12 plugins. They are deliberately not generated from
the host headers. Additional plugins use the current CX SDK, the original
0.2.0 CX SDK, and a host view with the message feature unavailable.

The tests exercise plugin loading, global locks, full-width CPU flags, L2 cache
queries, C entry points, legacy/UTF-8 rendering, source-frame preservation, and
Prefetch notifications for nice, multi-instance, and serialized filters.
Serialized filters receive a concurrency hint of one. CPU unit tests separately
cover AVX-512 OS-state requirements, half-precision feature decoding, and
restriction masks that must not enable unavailable features.

Run `v12_tests`, `core_tests`, and `cx_tests` with CTest. The broader CX compiler
matrix is available through `tests/cx/run_matrix.ps1`. Runtime validation on
Windows x64 does not establish Linux, ARM64, or 32-bit runtime correctness.

## Upstream provenance

Upstream baseline: `5c82777b374bdef16e13007a11e77d735ac1e4eb`.
Individual port commits retain source author and provenance. The functional
sources are `4d6faecb` (message API), selected message-rendering hunks from
`ccc7cc0a`, `fe797248` (thread hint), `842beb98` (L2 property), `461ddb95`
(64-bit CPU API), `ec399744` (CPU restrictions), `17d5a26f`, selected CPU hunks
from `2deba8e5` and `611295d0` (ARM detection), `fed2b3b2` (header casing), and
`e4fac7ec` (AVX-512 groups). CX adaptation and CPU correctness fixes are kept
in separate Minus commits.
