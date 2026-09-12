# AVS video conversion integration

The independent `third_party/video_convert` submodule owns layout and resampling
kernels and exposes C interfaces. AVS owns script defaults, CPU policy, frames,
properties, chroma placement, and operation order. `AvsCore` links the static
`AviSynth::ConvertVideo` target. AudioConvert and VideoConvert both reuse the
single host `hwy` target created by `modules/simd`.

`ENABLE_TESTS` also enables the independent video tests. `ENABLE_INTEL_SIMD` does
not disable Highway; the host maps AVS CPU flags through `AvsSimd`, with `none`
selecting ordinary C. Other/default requests on non-x86 select native.

The submodule URL is relative to the parent repository's remote, allowing sibling
repositories in the same organization. Local migration uses the sibling checkout
as the submodule origin until the independent repository is published. Do not push
parent commits before the referenced submodule commits are available remotely.

Initial build integration pins `d45d9f0`. The clang-cl Release host build and 190
selected public API/C consumer/existing Resize filter tests pass. This batch only
adds build dependencies; the following adapter batch changes production Resize.

## Resize adapter

`AvsVideoConvert` owns the private frame adapter. Public Resize factories pass a
C filter specification to immutable, target-bound resampling plans. Luma/RGB/alpha
share one plan, U/V share another when subsampled. Packed RGB horizontal execution
unpacks and repacks one row using VideoConvert layout kernels; vertical execution
keeps interleaved channels and the original bottom-up crop adjustment. YUY2 horizontal execution also uses row layout, sharing its U/V plan and
preserving the former YV16 wrapper chroma grid.

Script registrations, defaults, no-op handling, chroma centers, full H/V order,
intermediate storage and frame properties remain in AVS. Bob, Turn, AddBorders and
chroma conversion use the same factories. AddBorders queries effective filter
support through the C API, including Gaussian automatic support. Script tap values
retain the original 1..150 normalization.

Validation: clang-cl Debug and Release each run 298 selected cases: 296 pass and
2 pre-existing ConvertToPlanar cases fail identically in the older MSVC test
executable (luma extraction through an internal constructor and one interlaced
point row expectation). All 154 new public Resize native/none comparisons and the
242 Resize/transform/field/turn cases pass. The shared Release AviSynth DLL builds.
Legacy H/V filter classes, coefficient implementations and SSE/AVX2 resampler
files have been removed from AvsCore. Their duplicate kernel tests are replaced
by the independent project tests; ReduceBy2 retains its original input generator,
reference calculation and golden hashes. A local copy of removed files is stored
under ignored `tmp/resample-before-cleanup` for investigations.
Kernel AVX2/native performance gaps remain documented in the independent project;
this is functional adapter acceptance, not full-operation performance acceptance.

Cleanup validation: the 300 selected Release cases retain only the same two known
ConvertToPlanar failures. The 184 Resize cases pass with `ENABLE_INTEL_SIMD=OFF`
(MSVC), and the clang-cl shared Release DLL builds without any old resampler
implementation. Numerical test constants now live in the independent reference
code rather than being imported from removed private implementation headers.

Packed YUY2 acceptance: all 184 Resize cases pass in clang-cl Release and MSVC
ENABLE_INTEL_SIMD=OFF. Every YUY2 script variant also compares exact bytes with the
former full-frame layout wrapper chain, using the original centered default grid
and both H/V orders. Generic conversion scripts are not a substitute for that
reference: they can deliberately change chroma placement.

## Planar matrix conversion

The AVS filter retains script parsing, frame allocation, alpha and metadata.
`MatrixPlan` translates numeric Kr/Kb, depth, precision and ranges into an
immutable VideoConvert C plan. AVS CPU flags select the allowed Highway target
once; `SetMaxCPU("none")` uses ordinary C. No AVS type enters the independent
coefficient or pixel implementation.

Planar RGB(A) / YUV(A)444 now use the shared matrix rows. Integer output follows
the reviewed 15-bit forward / 13-bit reverse fixed-point C contract, including
nominal limited spans. This intentionally removes the previous CPU-dependent
choice of fixed-point versus float arithmetic at higher integer depths. F32
retains the C path's clipping and ordered arithmetic. Obsolete planar C/SSE/AVX2
implementations have been removed; packed matrix and grayscale remain separate
migration work.

Public tests use independent dot-product expectations for 8/10/12/14/16/F32,
three matrices, supported range combinations, none/native, alpha, frame properties
and source immutability. Reverse script range suffixes describe input YUV range;
`:same` also permits limited RGB output.

Planar acceptance: full host clang-cl Release 3,574/3,574; matrix Debug and
MSVC no-Intel-SIMD Release 64/64 each. Independent matrix sanitizer/scalar
acceptance is recorded in the submodule's docs/MATRIX.md.


## Packed matrix composition

RGB24/32/48/64 now reuse the layout and matrix plans with three scratch rows.
Storage follows logical top-to-bottom rows despite AVS packed bottom-up frames.
Alpha is copied directly during unpack/pack or filled opaque when absent. Scratch
belongs to each GetFrame call, so concurrent frames do not share mutable storage.
No full-frame planar temporary or legacy SIMD matrix fallback remains.

Public composition tests compare planar numerical output and packed orientation,
source immutability, alpha copy/fill and widths 1/17/33, under none/native.
Affected matrix/planar suites pass 97/97 in clang-cl Debug/Release and MSVC with
Intel SIMD disabled. The shared Release DLL builds. Retired packed SIMD tests
and their original hashes remain available in Git history and local tmp reference
copies; their 24 old-function cases are replaced by public composition coverage.
Packed full-operation performance remains part of the open performance pass.

## RGB luma extraction

ConvertToY uses the same numeric plan for planar and packed RGB. Integer results
are saturated to the declared code depth; F32 luma keeps its existing unclipped
behavior. Planar public arithmetic tests now check luma as well as YUV, and packed
composition tests also compare grayscale orientation and values. Obsolete RGB
luma C/SSE/MMX routines have been removed. Debug and no-Intel-SIMD matrix/planar
suites pass 97/97; initial Release matrix/planar/greyscale acceptance is 105/105.

Remaining matrix callers include legacy YUY2/RGB conversions. The latter fuse chroma interpolation with matrix quantization and
need their own numerical baseline; generic 4:4:4 composition is not assumed to
be equivalent.

## RGB Greyscale write-back

Greyscale now reuses the shared luma plan, with per-call row scratch and layout
unpack/pack for packed RGB. All three color channels receive the computed luma;
alpha and source frames are preserved. Integer saturation fixes limited-to-full
white/superwhite wrapping; F32 retains unclipped luma. Retired RGB C/SSE/MMX
implementations and their raw-function tests are removed; YUY2 routines remain.
Public tests check independent arithmetic, endpoints, alpha and packed layouts
under none/native. clang-cl Debug/Release pass 75/75 affected tests each; the
MSVC build with old Intel SIMD disabled passes 73/73 public tests.

RGB Greyscale now writes the numeric plan's output `_ColorRange`, preserving
RGB matrix identity and unrelated properties. Public none/native tests cover
both range directions, unchanged ranges, and source property isolation; affected
Release suites pass 73/73. Upstream integer wrapping and stale-range defects
were independently reproduced and documented as local B12/B13 drafts.

## Bit-depth host integration

Nondithered ConvertBits now uses the submodule's immutable depth plans. Both input
ranges are selected locally per frame; legacy dither function selection is also
local rather than swapping shared state. Public tests cover 288 pinned numeric
profiles, alpha, metadata/source isolation, reverse frame requests, extreme F32
values, packed orientation and truerange=false storage interpretation.

Packed RGB32/64 alpha is converted full-to-full independently of color range,
using row-local layout scratch. This fixes upstream's opacity change during
RGB limited/full conversion (B15, independently reproduced). The new safe depth
kernel also fixes upstream's float cast-before-saturation defect (B14).

Release public API acceptance is 293/293; Debug public API and ConvertBits
findings are 297/297; MSVC with old Intel SIMD disabled passes 293/293. Earlier
Release legacy bit-depth/finding acceptance is 50/50. Old nondithered helper
removal and the dither family migration follow this adapter acceptance. Remaining
kernel/full-filter performance gaps are recorded in the submodule's depth docs.

## Integration behavior

Float YUV-to-RGB preserves values outside [0, 1] on C and SIMD targets, retaining the host scalar contract and making target selection numerically consistent. The standalone default remains clipped; the adapter explicitly requests `VC_YUV_TO_RGB_UNCLIPPED`. Integer limited-range endpoints use the nominal 16/235 codes scaled by bit depth; full range ends at `(1 << depth) - 1`.
