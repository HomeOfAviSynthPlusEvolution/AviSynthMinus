# Source-compatible CX SDK (experimental)

The plugin keeps its existing AviSynth C++ source. A plugin-local adapter
implements the existing classes and `AVS_Linkage` using CX C callbacks. C++
objects, virtual tables, member pointers, allocations and exceptions stay on
their own side of the DLL boundary. Pixel buffers are shared, not serialized.

## Adopting the SDK

1. Replace the plugin's SDK include directory with this complete `include`
   directory. The ordinary `avisynth.h` API is preserved; no filter class,
   `GetFrame`, factory, or registration code needs to be rewritten.
2. After the existing `AvisynthPluginInit3` definition, add:

   ```cpp
   #include <avisynth_cx_legacy.h>
   AVS_CX_PLUGIN_INIT(AvisynthPluginInit3)
   ```

   Alternatively add a separate translation unit like
   `tests/cx/smoke_plugin/simple_sdk_entry.cpp`.
3. Compile **`avs/cx/legacy.cpp`** into the plugin using the plugin's compiler
   and C++17 or later. Keep its `sdk/*.inc` and `sdk/runtime.h` dependencies
   together with the headers. Do not define `BUILDING_AVSCORE`,
   `AVS_STATIC_LIB`, or `AVS_LINKAGE_DLLIMPORT` when building the plugin.

For an existing CMake target:

```cmake
target_include_directories(my_plugin PRIVATE "${AVS_SDK}/include")
target_sources(my_plugin PRIVATE "${AVS_SDK}/include/avs/cx/legacy.cpp")
target_compile_features(my_plugin PRIVATE cxx_std_17)
```

Keep the original `AVS_linkage` definition and `AvisynthPluginInit3` export.
The SDK adds `AvisynthPluginInitCX1`; it does not replace the legacy entry.
If the project uses an explicit export list, make sure CX1 is exported too.
An MSVC plugin can still communicate with a pre-CX MSVC core through Init3.
New cores prefer CX1 and never try Init3 after CX initialization fails.
Compiler runtime dependencies must remain available as for any ordinary DLL.

The same loaded plugin must use one transport for its lifetime: the existing
plugin-global `AVS_linkage` is initialized by that transport. This is not a
mechanism for using two unrelated hosts simultaneously through one DLL.

## Supported scope

The implementation covers ordinary CPU filters using `VideoInfo`, `AVSValue`
(undefined/bool/int64/double/string/clip/arrays), `PClip`, `PVideoFrame`, and
`GenericVideoFilter`. It includes registration, shutdown callbacks, formatted
errors, audio forwarding, parity/cache hints, frame allocation and subframes,
pixel/property copy-on-write, frame metadata, frame properties and maps,
Invoke/Invoke2 and variable helpers, CPU/environment queries, allocation and
global named locks. Null function/device smart pointers can be constructed
and destroyed, but non-null function/device objects are not transported.

The SDK uses a stable environment proxy per session. Nested and concurrent
callbacks select the correct opaque host environment through thread-local
call scopes; a saved C++ environment pointer does not point to a temporary
stack object. Retained frames have independent owning host references. Local
smart-pointer sharing participates in copy-on-write. Read-plane descriptors
are cached with synchronized publication, so row processing uses local data.

This is an experimental compatibility implementation, not a claim that every
AviSynth extension already works. Unsupported callable operations below throw
an error; the Neo environment query returns null to indicate unavailability:

- `ManageCache`, `DeleteScriptEnvironment`, `ApplyMessage`, and Invoke3/Invoke3Try.
- Non-null PFunction, device/GPU APIs, IScriptEnvironment2/INeoEnv extensions.
- Direct VideoFrameBuffer access and direct VideoFrame property-object methods;
  use the normal environment property methods instead.

The ordinary MSVC Init3 path retains the original API behavior, including
operations not yet implemented by the CX adapter. CX does not provide binary
compatibility for an already-built MSVC C++ plugin on a GCC core. Recompiling
with this SDK is required to add the C transport. Existing legacy C plugin
loaders remain in place; CX is a separate C protocol.

## Validation

Measured results and validation limits are recorded in `tests/cx/VALIDATION.md`.

`cx_smoke_dual` and `cx_smoke_legacy` compile the same filter and factory source.
Only the dual target adds the CX entry and SDK implementation. The Init3 test
also calls the old entry of the **complete dual DLL**, proving that linking
the SDK did not override its original inline linkage methods.

`cx_convert_stacked` compiles the repository's existing
`plugins/ConvertStacked/ConvertStacked.cpp` without editing it. Tests exercise
stacked and double-width conversion roundtrips. Other checks cover source
preservation, frame identity, local pixel/property COW, subframes, saved
environments, nested Invoke, arrays, errors and concurrent frame calls.
`cx_smoke_c` is compiled as C and uses only protocol headers.

Build `cx_tests` and `cx_smoke_benchmark` with each compiler, then run
`tests/cx/run_matrix.ps1` with the two build directories and GCC runtime path.
This exercises MSVC and GCC cores against both compilers' CX DLLs, including
the real ConvertStacked plugin and pure C fixture. The GCC core's existing
same-compiler C++ loader is not an MSVC C++ compatibility solution.

Run the performance test without other builds/benchmarks running:

```text
cx_smoke_benchmark.exe 3000 7
```

It alternates transport order, uses identical filter code and compiler, and
reports median/min/max time. Pass-through tests expose fixed per-frame cost;
1080p checker processing includes actual pixel work and normal host COW.
An alternate-compiler override also measures differences in generated pixel
code and must not be interpreted as pure ABI overhead.

## Protocol status

CX is experimental, including when it is part of `master`. It may become a
permanent feature, be restructured, or be removed. Its experimental status
is not an exemption from forward and backward plugin compatibility.

The compatibility commitment covers plugins working across older and newer
core releases. Refactoring or removing CX must preserve plugin operation;
retain a compatible loading and execution path wherever plugins depend on it.
Do not require plugins to be rebuilt merely because the implementation changes.
This commitment does not promise that CX itself or its internal design will
remain, nor does it extend the set of currently supported API operations.

The public adoption surface is the existing C++ API plus the init macro.
`sdk/runtime.h` and linkage helpers are implementation details, not a second
filter API for plugin authors. Once a protocol layout is released, new
capabilities must use new feature keys rather than changing that layout.
