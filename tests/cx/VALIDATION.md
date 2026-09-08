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

## Correctness coverage

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
