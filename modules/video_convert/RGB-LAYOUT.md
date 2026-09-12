# RGB layout adapters

All packed RGB layout routes use AviSynthConvertVideo C/Highway kernels:

| Source | Destination | Kernel |
| --- | --- | --- |
| BGR24/BGR48 | BGRA32/BGRA64 | repack_bgr, opaque alpha fill |
| BGRA32/BGRA64 | BGR24/BGR48 | repack_bgr, alpha discard |
| Packed BGR/BGRA | Planar RGB/RGBA | unpack_bgr |
| Planar RGB/RGBA | Packed BGR/BGRA | pack_bgr |

The filter binds its immutable function table at construction using AVS CPU
policy. SetMaxCPU("none") selects ordinary C. Highway availability is independent
of the legacy ENABLE_INTEL_SIMD option. The library handles U8/U16 storage;
layout conversion does not change sample depth or color range.

Packed RGB rows are bottom-up. Pack/unpack descriptors use negative packed
strides to express logical image orientation without copying intermediate frames.
Alpha insertion/removal uses packed-to-packed execution and needs no planar
scratch. Frame properties are copied with NewVideoFrameP; adding alpha uses
255/65535, preserving existing alpha copies its samples exactly.

The old C, MMX, SSE and AVX2 implementations are retired. Independent layout tests
cover guarded buffers, exact allocation boundaries, signed/independent strides
and partial row bands. Host tests cover short widths, tails, all U8/U16 alpha
combinations, orientation, unchanged source frames and C/native dispatch.

This acceptance is about correctness and integration. Previous performance
reports remain historical measurements; this batch adds no new timings.
