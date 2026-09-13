
Layer
=====

.. note::

   AviSynthMinus uses Composite kernels for this filter. See
   :ref:`composite-precision` for C-reference behavior, SIMD rounding allowances,
   and the distinction from historical output.


Layer (aka overlay, blend, merge) merges two clips of possibly different sizes, but with the same color format.

For pixel-wise transparency information, the alpha channel of an RGBA overlay_clip is used as a mask.

Note that some modes can be similar to :doc:`Overlay <overlay>`, but the two filters are still different.

- Overlay accepts mask clip, Layer would use existing A plane.
- Overlay "blend" is Layer "add", Overlay "add" is different.
- Lighten and darken is a bit different in Overlay.
- Layer has "placement" parameter for proper mask positioning over chroma.

.. rubric:: Syntax and Parameters

::

    Layer (clip base_clip, clip overlay_clip, [string "op", int "level", int "x", 
           int "y", int "threshold", bool "use_chroma", float "opacity", string "placement"] )

.. describe:: base_clip

    the underlying clip which determines the size and all other video
    and audio properties of the result. YV411 is not supported.

.. describe:: overlay_clip

    the clip which is merged onto clip. If RGB32 or other alpha-aware color space, 
    the alpha channel is used as a mask. Non-alpha plane YUV/planar RGB color spaces act as having 
    a fully transparent alpha channel. Color format must match base_clip.

    Note: if destination is YUVA/RGBA, the overlay clip also has to be Alpha-aware type.
    Alpha channel is not updated for YUVA targets, but RGBA targets do get the Alpha 
    updated (like the old RGB32 mode did - compatibility)

.. describe:: op

    the performed merge operation, which can be: "add", "subtract", "lighten", 
    "darken", "fast", "mul"

    +----------+-------------------------------------------------+-------------------------------------------------------------------------------------------------------------+
    | Operation| Example                                         | Description                                                                                                 |
    +----------+-------------------------------------------------+-------------------------------------------------------------------------------------------------------------+
    | add      | .. image:: ./pictures/Layer-base-Lena.png       | | This is the default mode. Equivalent to ``Overlay(mode="blend")``                                         |
    |          | .. image:: ./pictures/Layer-over-grad.png       |                                                                                                             |
    |          | .. image:: ./pictures/Layer-example-add.png     | | ``overlay_clip`` will be copied on top of the original, in proportion to ``opacity`` or ``level``         |
    |          |                                                 |   and subject to the alpha channel.                                                                         |
    |          |                                                 |                                                                                                             |
    |          |                                                 | | The difference between ``base_clip`` and ``overlay_clip`` is multiplied with alpha and added to           |
    |          |                                                 | | ``base_clip``.                                                                                            |
    |          |                                                 |                                                                                                             |
    |          |                                                 | | - alpha=0d   → only ``base_clip`` visible                                                                 |
    |          |                                                 | | - alpha=128d → ``base_clip`` and ``overlay_clip`` equally blended                                         |
    |          |                                                 | | - alpha=255d → only ``overlay_clip`` visible                                                              |
    |          |                                                 |                                                                                                             |
    |          |                                                 | | Formula used :                                                                                            |
    |          |                                                 | |                                                                                                           |
    |          |                                                 | | - using ``opacity`` parameter                                                                             |
    |          |                                                 | |   - Alpha-aware: ``base = base + (overlay - base) * opacity * alpha / max_range``                         |
    |          |                                                 | |   - No alpha: ``base = base + (overlay - base) * opacity``                                                |
    |          |                                                 |                                                                                                             |
    |          |                                                 | | - Deprecated method using ``level`` for 8 bit RGB and YUY2 formats                                        |
    |          |                                                 | |   - RGB:  ``base = base + ((overlay - base) * (alpha * level + 1) / 256) / 256``                          |
    |          |                                                 | |   - YUY2: ``base = base + ((overlay - base) * level) / 256``                                              |
    +----------+-------------------------------------------------+-------------------------------------------------------------------------------------------------------------+
    | subtract | .. image:: ./pictures/Layer-example-sub.png     | | ``base_clip`` minus ``overlay_clip``. The same as "add", but ``overlay_clip`` is inverted before adding.  |
    |          |                                                 |                                                                                                             |
    |          |                                                 | | If both clips are equal and ``opacity`` = 0.5 (``level`` = 128), a flat gray field is returned            |
    |          |                                                 |   compare to :doc:`Subtract <subtract>` .                                                                   |
    +----------+-------------------------------------------------+-------------------------------------------------------------------------------------------------------------+
    | lighten  | .. image:: ./pictures/Layer-example-lite.png    | | Copy ``overlay_clip`` over ``base_clip`` in areas where ``overlay_clip`` is lighter by threshold.         |
    |          |                                                 | |                                                                                                           |
    |          |                                                 | | Performs the same operation as "add", but only when ``overlay_clip`` is BRIGHTER than ``base_clip``.      |
    |          |                                                 | |                                                                                                           |
    |          |                                                 | | ``use_chroma`` must be true.                                                                              |
    |          |                                                 | |                                                                                                           |
    |          |                                                 | | Also known as lighter color.                                                                              |
    +----------+-------------------------------------------------+-------------------------------------------------------------------------------------------------------------+
    | darken   | .. image:: ./pictures/Layer-example-dark.png    | | Copy ``overlay_clip`` over ``base_clip`` in areas where ``overlay_clip`` is darker by threshold.          |
    |          |                                                 | |                                                                                                           |
    |          |                                                 | | The same as "lighten", but it is performed only when ``overlay_clip`` is DARKER than ``base_clip``.       |
    |          |                                                 | |                                                                                                           |
    |          |                                                 | | ``use_chroma`` must be true.                                                                              |
    |          |                                                 | |                                                                                                           |
    |          |                                                 | | Also known as darker color.                                                                               |
    +----------+-------------------------------------------------+-------------------------------------------------------------------------------------------------------------+
    | mul      | .. image:: ./pictures/Layer-example-mul-rgb.png | | ``base_clip`` multiplied by ``overlay_clip``. This will generally make the output darker.                 |
    |          |                                                 | | - alpha=0d    → only ``base_clip`` visible.                                                               |
    |          |                                                 | | - alpha=255d → approx. the same luminance as ``base_clip`` but with the colors of ``overlay_clip``.       |
    |          |                                                 | | See GIMP: Multiply                                                                                        |
    +----------+-------------------------------------------------+-------------------------------------------------------------------------------------------------------------+
    | fast     |                                                 | Like "add", but without masking. ``use_chroma`` must be true; ``opacity``, ``level`` and ``threshold``      |
    |          |                                                 | are not used. The result is simply the average of ``base_clip`` and ``overlay_clip``.                       |
    +----------+-------------------------------------------------+-------------------------------------------------------------------------------------------------------------+

.. describe:: level

    Note: deprecated in Avisynth+, use "opacity" instead.

    Original meaning: the strength of the performed operation.

    - 0: the ``base_clip`` is returned unchanged, 
    - 257 (256 for YUY2): the maximal strength is used.

.. describe:: x, y

    offset position of the ``overlay_clip``

.. describe:: threshold

    Changes the transition point of op = "darken", "lighten.".

    Automatically scaled for bit depths over 8, keep it between 0 and 255 

.. describe:: use_chroma

    Use chroma of the ``overlay_clip``, default=true. 
    
    When false only luma is used. Must be true for op = "darken", "lighten", "add."

.. describe:: opacity

    Transparency level.

    | Usable for all bit depths, replaces the previous ``level`` parameter.
    | Similar to "opacity" in "Overlay".

    Valid values are 0.0 to 1.0. Default value is 1.0 if ``level`` does not exist.
    (1.0 means full opacity)

    If ``level`` parameter is given then ``opacity`` is calculated as:

    - for color spaces having alpha: ``opacity = level / ((1 << bits_per_pixel) + 1)`` which gives 1.0 for level=257 (@8bit) and 65537 (@16 bits) 
    - for color spaces not having alpha: ``opacity = level / ((1 << bits_per_pixel))`` e.g. for YUY2 or other non-Alpha, gives 1.0 for level=256 (@8bit) 

    "opacity" parameter is bit depth independent (unlike ``level`` which was maxed with level=257 when RGB32 but level=256 for YUY2/YUV)

    In AviSynthMinus, the legacy ``level``-to-``opacity`` conversion above is
    retained at the script interface. Ordinary integer Layer kernels then round
    opacity onto ``0..((1 << bits_per_pixel) - 1)`` and combine it with alpha on
    that same maximum-code scale. Products divide by the maximum code, preserving
    full-scale endpoints. Fast mode retains its separate half-weight behavior.
    This replaces the historical ``(alpha*level + 1) / range_size`` arithmetic;
    it is not a claim of bit-identical output to older implementations.

.. describe:: placement

    chroma placement for 420 and 422 YUV formats.

    Possible values: "mpeg2" (default), "mpeg1".

    Used in "mul", "darken" and "lighten", "add" and "subtract" modes with planar YUV 
    4:2:0 or 4:2:2 color spaces (not available for YUY2) in order to properly apply 
    luma/overlay mask on U and V chroma channels. 

Other notes
-----------

Audio, FrameRate and FrameCount are taken from the first clip. 

There are some differences in the behaviour and the allowed parameter depending on the color format and the operation; here are the details:

    - When there is no mask (alpha channel), the alpha channel is assumed to be fully opaque (255d) everywhere. 

    - in alpha-aware color spaces alpha channel is multiplied with opacity, so the resulting alpha is 

        ``alpha * opacity`` 

      This means for full strength of operation, alpha has to be 255d and opacity has to be 1.0. 

Examples
~~~~~~~~

This can be used to combine two captures of different broadcasts for reducing
noise. A discussion of this idea can be found `in this thread`_. A sample script (of
course you have to ensure that the frames of the two clips match exactly --
use :doc:`DeleteFrame <deleteframe>` if necessary):

::

    clip1 = AviSource("F:\shakira-underneath_your_clothes.avi").ConvertToYUY2
    clip2 = AviSource("F:\shakira-
    underneath_your_clothes2.avi").ConvertToYUY2
    return Layer(clip1, clip2, "fast")


Changelog
----------

+-----------------+---------------------------------------------------------------+
| Version         | Changes                                                       |
+=================+===============================================================+
| 0.3.0           | AviSynthMinus: use Composite kernels. Retain legacy           |
|                 | level-to-opacity conversion at the script interface; ordinary |
|                 | integer kernels use maximum-code opacity/alpha weights and    |
|                 | product scaling. Document SIMD precision separately from      |
|                 | legacy output differences. See :ref:`composite-precision`.    |
+-----------------+---------------------------------------------------------------+
| 3.5.0           | Layer: support RGB24 and RGB48                                |
+-----------------+---------------------------------------------------------------+
| 3.4.0           | | Layer: support almost all formats, not only RGB32 and YUY2  |
|                 |   except RGB24, RGB48, YV411                                  |
|                 | | add "opacity" and "placement" parameters                    |
|                 | | Fix: add proper rounding for add/subtract/lighten/darken    |
|                 | | Fix: "lighten" and "darken" gave different results between  |
|                 |   yuy2 and rgb32 when Threshold<>0                            |
|                 | | Fix: "darken" for RGB32 when Threshold<>0                   |
|                 | | Fix: "lighten" and "darken" for YUY2 when Threshold<>0      |
+-----------------+---------------------------------------------------------------+


$Date: 2025/01/15 13:15:00 $

.. _in this thread: http://forum.doom9.org/showthread.php?s=&threadid=28438
