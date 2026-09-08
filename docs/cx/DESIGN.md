# CX compatibility design

The primary target is native Windows ARM64 plugins built with either the MSVC
or GCC/MinGW C++ ABI in one process. The cross-DLL boundary uses C callbacks and opaque references. Plugin authors should keep their filter
classes and registrations, adding only SDK build integration and an entry point.
The current x64 implementation is a proving ground, not ARM64 validation.

## Semantic constraints

* Values preserve int32/int64 and float32/float64 independently of the payload
  width. INT and FLOAT retain their existing 64-bit meaning; explicit INT32 and
  FLOAT32 tags preserve `GetType` and strict type predicates on roundtrips.
* Registration is visible immediately during initialization and later calls.
  Failed initialization removes only this session's function entries and all
  their aliases, preserving unrelated registrations from nested plugin loads.
  AtExit ownership is committed once. Registration still follows the core's
  normal synchronization requirements; arbitrary concurrent mutation is not
  made safe by this change. Export-list variables and arbitrary plugin side
  effects are not transactionally restored.
* A host clip's opaque token is its core-owned IClip address, retained and
  released only by core callbacks. Incoming host clips reuse plugin-local
  proxies while they are alive. The proxy index is weak: last local release
  removes its entries before destruction, rather than retaining every clip
  until environment shutdown. Duplicate host-clip arguments therefore share
  the same local IClip identity. No foreign C++ object is dereferenced by the SDK.
* A non-writable frame returns a successful write probe with a null data
  pointer. Invalid requests and actual host failures remain errors.

The local proxy index avoids dynamic_cast, so the complete smoke plugin
is built without RTTI. Reference/index updates use a short recursive mutex;
proxy construction and destruction which may call another DLL happen outside
that lock. This has a synchronization cost, so benchmarks remain necessary.

## Maintenance and protocol evolution

SDK operation IDs are explicit. Preserve existing numeric values and allocate
unused IDs for new operations. C compilation checks anchor the existing boundary IDs.

[tools/generate_cx_values.py](../../tools/generate_cx_values.py) reproducibly derives the four value-class method
groups from `avs_core/core/interface.cpp`, applying the SDK's name and reference-owner
transformations. `--check` compares C++ tokens with the checked-in result and
is registered with CTest when Python is available. Plugin consumers need no
Python. Regeneration is a maintainer action and its diff must be reviewed.
The extractor uses the current core source structure and deliberately fails
on missing section markers; it is not a general C++ parser. Linkage and service
dispatch still need a common declarative source in a later change.

CX is experimental, including when it is part of `master`. It may become a
permanent feature, be restructured, or be removed. Its experimental status
is not an exemption from forward and backward plugin compatibility.

The compatibility commitment covers plugins working across older and newer
core releases. Refactoring or removing CX must preserve plugin operation;
retain a compatible loading and execution path wherever plugins depend on it.
Do not require plugins to be rebuilt merely because the implementation changes.
This commitment does not promise that CX itself or its internal design will
remain, nor does it extend the set of currently supported API operations.

Preserve released protocol layouts and semantics. Add capabilities through new
feature keys rather than changing an existing layout. Value negotiation,
capability checks, and object identity must evolve within this compatibility
commitment.

## Validation and compatibility limits

See [CX validation](../../tests/cx/VALIDATION.md) for test procedures and coverage.

The same ordinary plugin source checks immediate and late registration,
numeric type preservation, duplicate clip arguments and shared-frame write
probes through both Init3 and CX. The compiler matrix additionally loads both
compiler DLLs in one process, alternates six checker filters, and reads frames
concurrently before normal teardown. Existing failure/lock/COW tests remain.

Remaining implementation and validation work:

1. Extend identity to plugin-created objects exported and re-imported across
   separate callbacks, including wrappers added by the core and lifetime across
   environments. The current host-proxy index is not full graph interning.
2. Specify capability checks independently of host CheckVersion. Today that
   call still reports host support and cannot certify every adapter method.
3. Generate linkage/service definitions and enforce their parity with the SDK.
4. Add ARM64 toolchain and shared-core package runs, extended properties/audio
   mixed chains, lifecycle stress, and allocation-failure tests for rollback.
5. Measure contention and long chains of trivial filters on target hardware.

Do not require every historical GPU/Neo extension before demonstrating ARM64
ABI coexistence, but do not describe unimplemented or changed ordinary API
semantics as transparent compatibility.
