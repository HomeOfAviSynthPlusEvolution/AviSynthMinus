=============
Merge Filters
=============

Set of filters to merge (blend) two clips together:

* **Merge** merges all channels (RGB(A) or YUV(A)) from one video clip into another.
* **MergeChroma** merges only the chroma (U/V channels) from one video clip
  into another.
* **MergeLuma** merges only the luma (Y channel) from one video clip into
  another.

There is an optional weighting, so a percentage between the two clips can be
specified.

.. _composite-precision:

AviSynthMinus compositing precision
-----------------------------------

AviSynthMinus uses the independent Composite library for Merge, MergeLuma,
MergeChroma, video blending in Dissolve and ConvertFPS, Layer, and Overlay.
Script parameters and frame handling remain in the host.

Two different kinds of numerical differences must be distinguished:

* The reviewed C arithmetic replaces some historical weight scales, truncation,
  and product formulas. Differences from older AviSynth implementations are not
  covered by a universal 1 LSB bound.
* Relative to that new C reference, eligible continuous-weight integer SIMD
  kernels permit at most **1 LSB per call**, including rounding boundaries.
  This applies to eligible Merge and video-transition blending paths, Overlay
  Blend, and interior-opacity integer YUV Multiply. Integer CODE-weight paths,
  including ordinary integer Layer blending, do not inherit this allowance.
  Existing exact weight endpoints retain their documented behavior.

Selected contiguous F32 masked MIX/PRODUCT kernels and YUV Multiply kernels
(with no mask or one shared mask) permit an error of
``16 * FLT_EPSILON * max(1, S)``. With base ``a``, source ``b``, and effective
weight ``w = opacity * mask`` (or just opacity without a mask), MIX uses
``S = abs(a)*(1-w) + abs(b)*w``; PRODUCT uses
``S = abs(a)*((1-w) + abs(b)*w)``. YUV Multiply uses source Y as ``b`` for each
base channel. For nonnegative inputs in [0,1], the allowance is at most about
1.91e-6. It does not apply to all float operations or layouts. Nonfinite and
ill-conditioned cases retain the library's reference fallback. Full conditions
are documented in `Composite numerical behavior
<https://github.com/HomeOfAviSynthPlusEvolution/AviSynthComposite/blob/6fe4f13b98814bb4fdfd814ca29a8d0c7902d2e3/NUMERICS.md>`_.

Repeated calls can accumulate error. To reduce the normalized size of integer
rounding, increase the working bit depth before processing. Set
``SetMaxCPU("none")`` before creating the filters to select C reference kernels;
this does not restore the historical formulas. Unprocessed channels retain their
specified preservation behavior.

Syntax and Parameters
---------------------

::

    Merge (clip clip1, clip clip2, float "weight")
    MergeChroma (clip clip1, clip clip2, float "weight")
    MergeLuma (clip clip1, clip  clip2, float "weight")

    MergeChroma (clip clip1, clip clip2, float "chromaweight")
    MergeLuma (clip clip1, clip clip2, float "lumaweight")

.. describe:: clip1, clip2

    Source clips:

    * ``clip1``; the clip that has the pixels merged into (the base clip).
    * ``clip2``; the clip from which the pixel data is taken (the overlay clip).
    * **Merge** supports all RGB(A)/YUV(A) color formats.
    * **MergeChroma** and **MergeLuma**, only YUV(A) color formats supported.

    | Clips must have the same color format and dimensions.
    | Audio, FrameRate and FrameCount are taken from the first clip.
    | If clips contain an alpha channel, it is also processed.

.. describe:: weight

    Defines how much influence the new clip should have. Range is 0.0–1.0.

    * At 0.0, ``clip2`` has no influence on the output.
    * At 0.5, the output is the average of ``clip1`` and ``clip2``.
    * At 1.0, ``clip2`` replaces ``clip1`` completely.

      * For **MergeChroma**, output chroma taken only from ``clip2``.
      * For **MergeLuma**,  output luma taken only from ``clip2``.

    | Default: 0.5 (Merge)
    | Default: 1.0 (MergeChroma, MergeLuma)

    Note that the alternate parameter names ``chromaweight`` and ``lumaweight``
    are considered deprecated.

Examples
--------

::

    # Blur the Luma channel.
    MPEG2Source("main.d2v")
    clipY = Blur(1.0)
    MergeLuma(clipY)

::

    # Do a spatial smooth on the chroma channel
    # that will be mixed 50/50 with the original image.
    MPEG2Source("main.d2v")
    clipC = SpatialSoften(2,3)
    MergeChroma(clipC, weight=0.5)

::

    # Run a temporal smoother and a soft spatial
    # smoother on the luma channel, and a more aggressive
    # spatial smoother on the chroma channel.
    # The original luma channel is then added with the
    # smoothed version at 75%. The chroma channel is
    # fully replaced with the blurred version.
    MPEG2Source("main.d2v")
    clipY = TemporalSoften(2,3).SpatialSoften(3,10,10)
    clipC = SpatialSoften(3,40,40)
    MergeLuma(clipY, weight=0.75)
    MergeChroma(clipC)

::

    # Average two video sources.
    vid1 = AviSource("main.avi")
    vid2 = AviSource("main2.avi")
    Merge(vid1, vid2)


Changelog
---------

+-----------------+-----------------------------------------------------------------+
| Version         | Changes                                                         |
+=================+=================================================================+
| 0.3.0           | AviSynthMinus: use Composite kernels for Merge, MergeLuma and   |
|                 | MergeChroma with continuous weights. Document differences from  |
|                 | legacy arithmetic and the 1 LSB allowance for eligible integer  |
|                 | SIMD paths relative to the new C reference. See                 |
|                 | :ref:`composite-precision`.                                     |
+-----------------+-----------------------------------------------------------------+
| AviSynth+ r2487 || Merge: added planar RGB(A) and YUV(A) support.                 |
|                 || Merge: SSE2 for 10-14 bits (10-16 for SSE4.1 still work).      |
|                 || Merge (Merge,MergeChroma/Luma): add AVX2.                      |
|                 || Merge: float to sse2 (weighted and average).                   |
+-----------------+-----------------------------------------------------------------+
| AviSynth+ r2290 | Merge filters: added 16/32 bit support.                         |
+-----------------+-----------------------------------------------------------------+
| AviSynth 2.6.0  || MergeChroma and MergeLuma: Added alias ``weight`` for          |
|                 |  ``chromaweight`` and ``lumaweight``.                           |
|                 || Added support for Y8, YV16, YV24 and YV411 color formats.      |
+-----------------+-----------------------------------------------------------------+
| AviSynth 2.5.6  | Added Merge filter.                                             |
+-----------------+-----------------------------------------------------------------+

$Date: 2022/03/10 16:46:19 $
