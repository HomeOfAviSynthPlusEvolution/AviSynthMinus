# Shared-library API regression tests

This standalone CMake project exercises Audio, Video and Composite through an
installed shared core and its public headers. It reuses host filter regression
cases, initializes the public AVS linkage table, and does not link `AvsCore` or
any independent kernel library directly. The depth fixture comes from the pinned
Video submodule. Resize cases use independent PointResize coordinates and
BilinearResize interpolation expectations across 12 grayscale, planar 4:4:4 and
packed RGB formats. They cover crop/shrink/enlarge, alpha, frame properties,
timing, source immutability and C/native dispatch; subsampled chroma and other
resize kernels remain covered by the static host suites.

Linkage regressions also check that a client with an uninitialized `AVS_linkage`
receives the core table and can load a legacy Init3 plugin. On Linux, the core
binds internal function references locally so SDK forwarding methods exported
by a client cannot replace core implementations or linkage-table entries.

After building and installing a shared core, run from the repository root:

```sh
cmake -S tests/shared_api -B build/shared-api -DAVS_INSTALL_PREFIX=/absolute/path/to/stage
cmake --build build/shared-api --config Release --parallel
ctest --test-dir build/shared-api -C Release --output-on-failure
```

Use the same architecture as the installed core (for example, `-A Win32` with a
Visual Studio generator for x86). GoogleTest 1.17.0 is fetched at configuration.
On Windows the tested DLL is copied beside the executable; POSIX builds use the
linked library's build RPATH. The main build workflow runs these tests for each
shared-library platform, in addition to a separate full static regression job.
