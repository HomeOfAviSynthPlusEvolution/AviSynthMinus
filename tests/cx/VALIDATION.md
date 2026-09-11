# CX SDK validation

This guide describes the Windows x64 Release test setup. The test executables
link the core statically and load real plugin DLLs. This setup does not establish
compatibility with a production shared-core package, ARM64, 32-bit Windows,
Linux, every existing plugin, or sanitizer instrumentation.

## Running the compiler matrix

Configure two separate build directories with `ENABLE_TESTS=ON` and
`BUILD_SHARED_LIBS=OFF`: one using MSVC x64 with a multi-configuration generator,
and one using MinGW-w64 GCC x64 with a single-configuration generator and
`CMAKE_BUILD_TYPE=Release`. CMake fetches the test dependencies during configuration.
Use the same source revision for both builds.

From the repository root, after configuring the example build directories:

```powershell
cmake --build build/cx-sdk --config Release --target cx_tests cx_smoke_benchmark
cmake --build build/cx-sdk-gcc --target cx_tests cx_smoke_benchmark
$gccRuntime = Read-Host 'Directory containing the runtime DLLs for the GCC build'
./tests/cx/run_matrix.ps1 -MsvcBuild build/cx-sdk -GccBuild build/cx-sdk-gcc -GccRuntime $gccRuntime
```

Check that each build succeeds before running the matrix. Adjust the build
paths to match the configured directories. The script expects MSVC artifacts
under `tests/cx/Release` and GCC artifacts under `tests/cx` in their respective
build directories. It temporarily adds the supplied GCC runtime directory to
PATH and restores the caller's environment when finished.

The matrix runs all four combinations:

| Core compiler | CX plugin compiler |
| --- | --- |
| MSVC | MSVC |
| MSVC | GCC |
| GCC | MSVC |
| GCC | GCC |

Record the source revision, compiler versions, build configuration, and complete
output with each validation run. Test counts depend on the revision and optional
dependencies; the table describes the required routes, not a recorded pass result.

On Unix hosts, the mixed chain also supports Clang/libc++ and GCC/libstdc++.
Build separate trees with the same source revision, then set
`AVS_CX_OTHER_DUAL_PATH` to the absolute path of the other compiler's
`cx_smoke_dual.so` before running CTest. The fixture registers distinct Clang
and GCC functions so the chain actually alternates between the two plugins.
On FreeBSD, the GCC package's runtime libraries may need to be selected with
`LD_LIBRARY_PATH` (for example, `/usr/local/lib/gcc14` for the GCC 14 package).
Keep legacy Init3-only plugins matched to the core's C++ ABI.

## Correctness coverage

The generated-value parity check is registered when Python is available.
The matrix supplies the alternate compiler DLL
for the six-stage mixed chain; running CTest alone skips that mixed-chain test.
Both complete dual-entry plugins compile with RTTI disabled. The
semantic test compares registration, numeric types, host clip identity and
write-probe results against Init3 using the same source.

The CX matrix includes an embedded-NUL SaveString hash-collision check through Init3 and CX,
and a batch registration regression: two environments each register 2,048 functions in two
batches with alternating callbacks and different user_data. Interleaved checks
verify that growing the registry preserves earlier bindings and session isolation.

Registration regression tests hold the host plugin lock in a legacy DLL's init
while another thread registers from GetFrame through Init3 or CX, followed by
eight concurrent registrations. A separate CX plugin loads a same-core-compiler
legacy dependency, a missing dependency, and a DLL whose Init3 deliberately
throws during init, then checks the outer
plugin's qualified function names. The CX outer plugin participates in the
cross-compiler matrix; the legacy dependency always matches its core ABI.

Each combination exercises the ordinary C++ SDK adapter, a pure-C CX plugin,
and the unchanged `plugins/ConvertStacked/ConvertStacked.cpp`. The latter
roundtrips planar 16-bit Y/YUV420/YUV422/YUV444 through stacked and double-width
representations. Fixtures cover nested Invoke, saved environments, concurrent
GetFrame, values and maps, frame identity, pixel/property COW and subframes.

MSVC additionally tests the original Init3 entry of the complete dual-entry
DLL directly, without entering CX. Both cores also test their existing
same-compiler Init3 loader. CX init failure must not invoke the legacy entry
or retain staged function registrations. Existing `avisynth_c_plugin_init`
and `avisynth_c_plugin_init2` loaders are preserved; the pure-C fixture here
tests CX1, not those older protocols.

Global lock tests cover two legacy-source lock roundtrips, nine
owner/waiter combinations across CX, Init3 and the existing C API, and a
different-name independence test. The C API caller is a C translation unit
linked into the test executable, not a dynamically loaded legacy C plugin.
The CX DLL is exchanged in the compiler matrix; Init3 always uses its core's
compiler. No duplicate GLOBLOCK feature or public CX C++ API is introduced.
All CX tests run individually through CTest with 15-second process timeouts.

### Boundary cases

The same ordinary plugin source is tested through same-compiler Init3 and each
CX matrix route. A deterministic host source retains three distinct 70x33 Y8
frames and emits position-dependent interleaved stereo INT32 audio. Tests cover:

- Multiple upstream clips and three simultaneous frames from one upstream,
  with retained read pointers and all visible pixels checked after more imports.
- Copy-on-write while the original frame and its read pointer remain retained.
- Two independent audio ranges at nonzero offsets, buffer guards, and a
  zero-length read.
- A filter retaining both inputs after the factory arguments are destroyed,
  and holding a frame/read pointer across out-of-order GetFrame callbacks.
- A plugin-owned worker using the environment saved by its factory in a later
  GetFrame call, including frame retrieval and copy-on-write. The worker joins
  before the callback returns; detached workers, environment destruction races,
  and every environment service are not covered.

`RepeatedHostFramePreservesPluginObjectIdentity` checks that repeated imports
of the same cached host frame reuse the plugin-local wrapper. Further
regressions exercise four plugin threads importing/releasing
frames (1,000 iterations each, with one permanently held frame and two without
permanent local anchors), plus new-frame, subframe and copied-frame identity
after a host roundtrip. Copy-on-write must keep the original indexed wrapper
and its pixels intact; a unique new frame must not be unnecessarily copied.
These tests do not establish complete clip/function/device identity support.

## Performance

PowerShell command: `./cx_smoke_benchmark.exe 3000 7`. One thread, 1920x1080 Y8,
600,000 passthrough frames and 3,000 checker frames per sample. Each result
is the median of seven samples, alternating transport order. Each comparison
uses the same compiler and identical filter source for Init3 and CX1.

The benchmark takes a positive checker frame count and a positive odd sample
count. Passthrough uses `max(200000, frames * 200)` frames per sample.
Run the executable from `tests/cx/Release` in the MSVC build or `tests/cx` in
the GCC build. For a same-compiler comparison, leave `AVS_CX_SMOKE_DUAL_PATH`
unset so the compiled-in plugin path is used. An override selecting a plugin
built by another compiler also includes differences in the compiled filter kernel.
GCC runtime DLLs must be available on PATH when loading GCC artifacts.

Record the revision, CPU, compiler, configuration, arguments, process priority,
and full output (including sample ranges) with results. Compare identical
conditions and retain repeated samples; a local smoke benchmark is not a
statistically controlled performance guarantee. Test long chains of trivial
filters and multithreaded contention separately, and measure ARM64 on target
hardware. Pixel buffers are not copied merely to cross CX; normal filter
copy-on-write still applies.

## Compatibility boundary

Adoption requires the complete replacement SDK headers, one CX init macro
and compiling `avs/cx/legacy.cpp`. Filter and factory source is unchanged for
the supported subset. This is not yet universal source compatibility:
function/device objects, Neo/IScriptEnvironment2 extensions, raw frame-buffer
access and several environment calls are not implemented. See the [SDK README](../../avs_core/include/avs/cx/README.md)
for the exact scope before migrating another plugin.
