
Overlay
=======

.. note::

   AviSynthMinus uses Composite kernels for this filter. See
   :ref:`composite-precision` for C-reference behavior, SIMD rounding allowances,
   and the distinction from historical output.


Puts clip ``overlay`` on top of clip ``base`` using different blend ``modes``, and 
with optional ``x``, ``y`` positioning, ``mask`` operation and ``opacity``. 

* In some modes the input clips (``base``, ``overlay`` and ``mask``) are converted 
  to 4:4:4 internally. The output is re-converted to the input colorspace (or to the 
  ``output`` colorspace, if specified). 
  But, if possible, original colorspace if preserved during the processing, such as 
  in "blend", "luma" and "chroma" modes; see ``use444``, below.
* In general all clips are treated as full-range values. This means that numbers will 
  not be clipped at TV range; you may use :doc:`Limiter <limiter>` for this task afterwards. 
  If your ``mask`` is TV-range, you should convert it to full-range, or the mask will 
  never be fully opaque. You can use :doc:`Histogram <histogram>` in ``Histogram("levels")`` 
  mode to view the color distributions. If your mask is of limited (TV) range, use
  ``ConvertBits(fulls=false, fulld=true)`` or ``ColorYUV(levels="TV->PC`` to upscale 
  the color levels.
* It is not recommended to do overlays on interlaced material, unless you know what 
  you are doing. 


Syntax and Parameters
---------------------
::

    Overlay (clip, clip overlay, int "x", int "y", clip "mask", float "opacity",
             string "mode", bool "greymask", string "output", 
             bool "ignore_conditional", bool "pc_range", bool "use444", 
             string "condvarsuffix")

.. describe:: clip

    This clip will be the base, and the overlay picture will be placed on top of
    this.

.. describe:: overlay
    
    This is the image that will be placed on top of the base clip. The colorspace
    or image dimensions do not have to match the base clip.

.. describe:: x, y

    These two variables define the placement of the overlay image on the base
    clip in pixels. The variable can be positive or negative.

    Default: 0, 0

.. describe:: mask

    Optional transparency mask for the overlay image. Must be the same size as 
    the overlay clip. Where mask is darker, overlay will be more transparent. 
    
    By default only the greyscale (luma) components are used, but this can be 
    overridden with ``greymask``=``false``. 

    There is no default, but not specifying is equivalent to supplying a fully
    255 (in general ``2^bit_depth - 1``  for 8-16 bit formats or ``1.0`` for 
    32 bit float format) clip. Maximum pixel value means a 1.0 mask multiplier,
    that means full opacity.

.. describe:: opacity

    This will set how transparent your image will be. The value is from 0.0 to
    1.0, where 0.0 is transparent and 1.0 is fully opaque (if no mask is used).
    When used together with a mask this value is multiplied by the mask value to
    form the **final** opacity.
    
    Default: 1.0

.. describe:: mode

    Mode defines how your clip should be overlaid on your image.

    Default: "Blend"

+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| Mode      |                                                       | Description                                                                                           |
+===========+=======================================================+=======================================================================================================+
| Blend     |  .. image:: ./pictures/Layer-base-Lena.png            | This is the default mode. When opacity is 1.0 and there is no mask the                                |
|           |  .. image:: ./pictures/Layer-over-grad.png            | overlay image will be copied on top of the original. Ordinary transparent blending is used otherwise. |
|           |  .. image:: ./pictures/Overlay-example-blend.png      |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
|           |                                                       |                                                                                                       |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| Add       |  .. image:: ./pictures/Overlay-example-add.png        | This will add the overlay video to the base video, making the video                                   |
|           |                                                       | brighter. To make this as comparable to RGB, overbright luma areas are                                |
|           |                                                       | influencing chroma and making them more white.                                                        |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| Subtract  |  .. image:: ./pictures/Overlay-example-subtract.png   | The opposite of Add. This will make the areas darker.                                                 |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| Multiply  |  .. image:: ./pictures/Overlay-example-multiply.png   | This will also darken the image, but it works different than subtract.                                |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| Chroma    |  .. image:: ./pictures/Overlay-example-chroma.png     | This will only overlay the color information of the overlay clip on to the base image.                |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| Luma      |  .. image:: ./pictures/Overlay-example-luma.png       | This will only overlay the luminosity information of the overlay clip on to the base image.           |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| Lighten   |  .. image:: ./pictures/Overlay-example-lighten.png    | This will copy the light infomation from the overlay clip to the base                                 |
|           |                                                       | clip, only if the overlay is lighter than the base image.                                             |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| Darken    |  .. image:: ./pictures/Overlay-example-darken.png     | This will copy the light infomation from the overlay clip to the base                                 |
|           |                                                       | clip, only if the overlay is darker than the base image.                                              |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| SoftLight |  .. image:: ./pictures/Overlay-example-softlight.png  | This will ligten or darken the base clip, based on the light level                                    |
|           |                                                       | of the overlay clip. If the overlay is darker than luma = 128 [``(2^bit_depth)-1``], the base image   |
|           |                                                       | will be darker. If the overlay is lighter than luma=128 [``(2^bit_depth)-1``], the base image will    |
|           |                                                       | be lighter. This is useful for adding shadows to an image. Painting with pure                         |
|           |                                                       | black or white produces a distinctly darker or lighter area but does not                              |
|           |                                                       | result in pure black or white.                                                                        |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| HardLight |  .. image:: ./pictures/Overlay-example-hardlight.png  | This will ligten or darken the base clip, based on the light level                                    |
|           |                                                       | of the overlay clip. If the overlay is darker than luma = 128, the base image                         |
|           |                                                       | will be darker. If the overlay is lighter than luma=128, the base image will                          |
|           |                                                       | be lighter. This is useful for adding shadows to an image. Painting with pure                         |
|           |                                                       | black or white results in pure black or white.                                                        |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| Difference|  .. image:: ./pictures/Overlay-example-difference.png | This will display the difference between the clip and the overlay.                                    |
|           |                                                       | Note that like :doc:`Subtract <subtract>` a difference of zero is displayed as grey, but              |
|           |                                                       | with luma=128 instead of 126. If you want the pure difference, use                                    |
|           |                                                       | mode="Subtract" or add `ColorYUV(off_y=-128)`.                                                        |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+
| Exclusion |  .. image:: ./pictures/Overlay-example-exclusion.png  | This will invert the image based on the luminosity of the overlay                                     |
|           |                                                       | image. Blending with white inverts the base color values; blending with black                         |
|           |                                                       | produces no change.                                                                                   |
+-----------+-------------------------------------------------------+-------------------------------------------------------------------------------------------------------+

.. describe:: greymask

    Specifies whether chroma should be used for chroma transparency. Generally you 
    want to leave this alone, this mode shouldn't be disabled. External filters like
    ``mSharpen`` and ``Masktools`` are able to export proper chroma maps. 

    Default: true

.. describe:: output

    It is possible to make Overlay return another colorspace.
    e.g. "YV24", "YUV420P14" or "RGB64" 

    Default: (input colorspace)

.. describe:: ignore_conditional

    Ignore any given conditional (runtime) variables. 

    See also: conditional variables section :doc:`ColorYUV <coloryuv>`, or 
    :doc:`ConditionalReader <conditionalreader>` or 
    http://avisynth.nl/index.php/ColorYUV.

    Default: false

.. describe:: pc_range

    When set to true, this will make all internal RGB→YUV →RGB conversions assume that 
    YUV sources are full-range instead of the default TV range. It is only recommended
    to change this setting if you know what you are doing. See RGB considerations below. 
    
    Default: false

.. describe:: use444

    If set to false, Overlay uses conversionless mode where possible instead of going 
    through YUV 4:4:4. However, for ``Luma`` and ``Chroma`` modes, RGB must be converted 
    to YUV 4:4:4.
    
    Default: (adaptive) 
    
    * false when mode="blend" and format is RGB 
    * false when mode="blend", "luma" or "chroma" and format is YUV420/YUV422 (YV12/YV16). 
      Original format is kept throughout the whole process, no 4:4:4 conversion occurs. 
    * true for all other cases (input is converted internally to 4:4:4)     

.. describe:: condvarsuffix

    Allows multiple filter instances to use differently named conditional parameters.
    Prevents collision and overwrite of variables which are used by different Overlay 
    instances.

    See also: conditional variables section :doc:`ColorYUV <coloryuv>`, or 
    :doc:`ConditionalReader <conditionalreader>`, or :doc:`RGBAdjust <adjust>`.
    
    How does it work: when reading the global variables, the ``condvarsuffix`` parameter is 
    appended to the variable name. E.g. variable name "myvar_a" will be read instead of 
    "myvar" when ``condvarsuffix = "_a"`` is provided.
    
    Useful for ``ColorYUV``, ``RGBAdjust``, ``Overlay`` when the conditional variables
    are enabled (or here, in Overlay, is not disabled).

    In the matching ConditionalReader one have to use the modified name as well:

    ::

        ConditionalReader("overlay_a_offset.txt", "OL_opacity_offset", false, CondVarSuffix = "_a")
        # "_a" is added here by parameter

    or specify the suffixed name directly:

    ::

        ConditionalReader("overlay_a_offset.txt", "OL_opacity_offset_a", false)
        # "_a" is added here manually

    Default: ""



RGB considerations
------------------

RGB inputs are accepted. However, Overlay for specific modes may convert the RGB clip
internally to 4:4:4 (see Avisynth+ exceptions), this will lead to an RGB→YUV conversion. 
There are two modes for this conversion, toggled by the ``pc_range`` parameter. This 
parameter will extend the YUV range from 16-235 (8 bit example) (this is the range 
used by all Avisynth converters) to 0-255. There are some cases where enabling 
``pc_range`` is a good idea:

* When overlaying an RGB clip using the ``add``, ``subtract`` or ``multiply`` modes,
  the range of the overlay clip is better, if it is 0-255, since this will enable 
  completely dark areas not to influence the result (instead of adding 16 to every value).
* When NOT doing a colorspace conversion on output. If the output colorspace 
  (RGB vs. YUV) is different from the input, the scale will be wrong. If 
  ``pc_range=true``, and input is RGB, while output is YV16, the YV16 will have an 
  invalid range, and not CCIR-601 range.
* Planar RGB formats are also supported besides 8 bit packed RGB formats plus RGB48/RGB64.
* "blend" mode keeps original RGB format, no YUV intermediate conversion is used. 

**Outputting RGB**

It might be a good idea to let Overlay output YV24 or YUV444P10-16, even if your 
input colorspace is RGB, as this avoids a colorspace conversion back to RGB from 
YUV. You should however be aware that your material might be "overscaled", as 
mentioned above, if you use ``pc_range=true``. You can correct this by using 
``ConvertBits(fulls=true, fulld=false)`` or ``ColorYUV(levels="pc->tv")`` to 
convert back to 16-235 range (or equivalent ranges to 10+ bits). 

**Inputting RGB for mask clip**

An RGB mask clip may behave a bit oddly if it contains color information. 
If you use a greyscale ``mask``, or if you leave ``greymask=true``, you will get 
the result you would expect. Note that mask values are never scaled, so it 
will automatically be in full-range, directly copied from the RGB values.
Traditionally, the mask is retrieved from channel "B" (Blue).

**Using RGB32, RGB64 or planar RGBA alpha channel**

Overlay ignores the alpha (transparency) channel in an RGB32 clip. If you 
want the alpha, you can use something like 
``Overlay(kitten, mask=kitten.ExtractA())`` or
``Overlay(kitten, mask=kitten.ShowAlpha("RGB32"))``.

See also :doc:`Extract filters <extract>` and :doc:`ShowAlpha <showalpha>`.

**Repeated overlays on RGB base clip**

When doing repeated partial overlays on an RGB base clip, the unchanged parts 
of the base clip may undergo a RGB→YV24→RGB conversion for each call to 
Overlay, producing a progressive loss of color accuracy. In these situations, 
it is better to convert the base clip to 4:4:4 format (e.g. YV24) before doing 
the overlays and convert back to RGB afterwards. Remember, that "Blend" does 
not convert from RGB.

Conditional Variables
---------------------

The global variables ``OL_opacity_offset``, ``OL_x_offset`` and
``OL_y_offset`` are read each frame, and applied. It is possible to modify
these variables using :doc:`FrameEvaluate <conditionalfilter>`. The values of these variables
will be added to the original on each frame. So if you specify "x = 100" as a
filter parameter, and the global variable ``OL_x_offset`` is set to 50, the
overlay will be placed at x = 150.

If you need to use conditional variables in multiple filters, use 
``condvarsuffix`` parameter to make them unique for each filter instance.

In other Overlay filters this can even be disabled by using the
``ignore_conditional = true`` parameter.

There is an example of conditional modification at the
:doc:`ConditionalReader <conditionalreader>` page.


Examples
--------

::

    # Prepares some sources.
    bg = ColorBars(512,384).ConvertToYUY2
    text = BlankClip(bg).Subtitle("Colorbars", size=92, 
    \          text_color=$ffffff).ColorYUV(levels="tv->pc")

    # Overlay the text in three different versions.
    return Overlay(bg, text, x=50, y=20, mode="subtract", opacity=0.25)
    return Overlay(text, x=50, y=120, mode="add", opacity=0.5)
    return Overlay(text, x=50, y=240, mode="blend", opacity=0.7)

    # Overlay yuy2 clip with rgb clip using a yuy2 mask
    # (note that the luma range of the mask is [0-255]). 
    return Overlay(yuy2clip, rgbclip, mask=rgbclip.ShowAlpha("yuy2"))

    # ...which is the same as 
    mask = rgbclip.ShowAlpha("rgb").ConvertToYUY2
    \             .ColorYUV(levels="TV->PC")
    return Overlay(yuy2clip, rgbclip, mask=mask)

    # ...which is the same as 
    mask = rgbclip.ShowAlpha("rgb")
    return Overlay(yuy2clip, rgbclip, mask=mask)

- This will take the average of two clips. It can be used for example to 
  combine two captures of different broadcast captures for reducing noise. 
  A discussion of this idea can be found [`here`_]. A sample script (of course
  you have to ensure that the frames of the two clips matches exactly, 
  using Trim as needed): 

::

    clip1 = AviSource("F:\shakira-underneath_your_clothes1.avi")
    clip2 = AviSource("F:\shakira-underneath_your_clothes2.avi")
    Overlay(clip1, clip2, mode="blend", opacity=0.5)

- Another use is to detect an altered video using Video Error Level Analysis 
  (VELA), where clip2 is clip1 resaved using an h.263 Codec (e.g. XVID). 
  This method is effective when the suspected altered video (clip1) has 
  not been resaved multiple times. Levels is used to exaggerate contrast 
  for view-ability:

::

    clip1 = AviSource("SuspectVideo.avi")
    clip2 = AviSource("SuspectVideo_resaved.avi")
    result= Overlay(clip1,clip2,mode="Subtract").Levels(0, 5.0, 100, 0, 255)

- Use a blue (or any other color) background (blue.jpg is a blue frame
  overlaid with subtitles in a black rectangle) as mask. The black rectangle
  containing the subtitles will be visible on the source clip (which is
  :doc:`ColorBars <colorbars>` here):

::

    testcard = ColorBars()

    # get a blue mask clip (the same blue as in ColorBars is used: R16 G16 B180)
    maskclip = BlankClip(testcard, color=$0f0fb4)

    # Example subtitle file with blue backgroud as above
    subs = ImageSource("F:\TestClips\blue.jpg").ConvertToRGB32

    maskclip = ColorKeyMask(subs, $0f0fb4, 60)

    Overlay(testcard, subs, mask=ShowAlpha(maskclip), mode="blend", opacity=1)

.. image:: pictures/overlay_blue.jpg
.. image:: pictures/overlay_subs.png


A tolerance of 60 is used here because the blue is not entirely uniform. Near
the black rectangles the blue is given by R23 G22 B124. Probably due to the
compression of blue.jpg.

- Move a red (or any other color) dot on a clip using ``ConditionalReader``
  (dot.bmp is a red dot on a black background):

::

    a1 = ColorBars().Trim(0,399)
    a2 = ImageSource("F:\TestClips\dot.bmp").ConvertToRGB32

    # a2.GreyScale returns a grey dot on a black background; Levels makes the dot white
    mask_clip = Mask(a2, a2.GreyScale.Levels(0, 1, 75, 0, 255))
    Overlay(a1, a2, mask=ShowAlpha(mask_clip), y=0, x=0, mode="blend", opacity=1)

    ConditionalReader("xoffset.txt", "ol_x_offset", false)
    ConditionalReader("yoffset.txt", "ol_y_offset", false)

Make xoffset.txt containing the x-positions and yoffset.txt containing the
y-positions of the moving dot (see :doc:`ConditionalReader <conditionalreader>` for more info), and
put it in the same folder as your script:

::

    xoffset.txt

    Type int
    Default -50

    R 0 100 20
    I 100 200 20 250
    R 200 300 250
    I 300 400 250 400 ::yoffset.txt

    Type int
    Default -50

    R 0 100 20
    I 100 200 20 350
    R 200 300 350
    I 300 400 350 40

.. image:: pictures/overlay_dot.png
.. image:: pictures/overlay_dot2.png

thus the dot moves in the following way: (20,20) -> (250,350) -> (400,40).
Nb, it's also possible to do this with Animate.

And the same with using ``condvarsuffix``:

::

    a1 = ColorBars().Trim(0,399)
    a2 = ImageSource("F:\TestClips\dot.bmp").ConvertToRGB32

    # a2.GreyScale returns a grey dot on a black background; Levels makes the dot white
    mask_clip = Mask(a2, a2.GreyScale.Levels(0, 1, 75, 0, 255))
    Overlay(a1, a2, mask=ShowAlpha(mask_clip), y=0, x=0, mode="blend", opacity=1, \
            condvarsuffix="_a")

    # directly read the _a prefixed variable
    ConditionalReader("xoffset.txt", "ol_x_offset_a", false)
    # or _a as exactly named parameter
    ConditionalReader("yoffset.txt", "ol_y_offset", false, condvarsuffix="a")


  
+-----------+------------------------------------------------------------------------+
| Changelog |                                                                        |
+===========+========================================================================+
| 0.3.0     | AviSynthMinus: use Composite kernels for Overlay modes. Document       |
|           | reviewed arithmetic, the 1 LSB allowance for eligible continuous       |
|           | integer blending and interior-opacity YUV Multiply, and selected F32   |
|           | tolerances. Historical blend_compat arithmetic remains available. See  |
|           | :ref:`composite-precision`.                                            |
+-----------+------------------------------------------------------------------------+
| 3.7.2     | Address issue #255: "blend": now using accurate formula using float    |
|           | calculation.                                                           |
+-----------+------------------------------------------------------------------------+
| 3.7.1     | Overlay mode "multiply": overlay clip is not converted to 4:4:4        |
|           | when when 420 or 422, since only Y is used from it (speed).            |
+-----------+------------------------------------------------------------------------+
| 3.7.0     || allow 4:1:1 input                                                     |
|           || fix crash when mask is YUV411 and greymask=false                      |
+-----------+------------------------------------------------------------------------+
| 3.4.0     | Add "condvarsuffix" parameter                                          |
+-----------+------------------------------------------------------------------------+
| r2502     | Correct masked blend: keep exact clip1 or clip2 pixel values           |
|           | for mask extremes 255 or 0.                                            |
|           | Previously 0 became 1 for zero mask, similarly 255 changed into        |
|           | 254 for full transparency (255) mask                                   |
+-----------+------------------------------------------------------------------------+
| r2420     || "Blend" native greyscale mode: process y plane only w/o conversion    |
|           || automatic use444=false for "blend"/"luma"/"chroma"                    |
|           |  for inputs: 420/422/444 and any RGB, lossless Planar RGB intermediate |
|           |  for PackedRGB.                                                        |
|           || mask auto-follows input clip format. For compatibility: when          |
|           |  greymask=true (default) and mask is RGB then mask source is           |
|           |   the B channel 254 for full transparency (255) mask                   |
+-----------+------------------------------------------------------------------------+
| r2359     || new parameter: bool use444 (default true for compatibility)           |
|           || lossless RGB "blend" w/o YUV conversion                               |
+-----------+------------------------------------------------------------------------+
| v2.54     | Initial Release                                                        |
+-----------+------------------------------------------------------------------------+

$Date: 2025/03/15 21:16:50 $

.. _here: http://forum.doom9.org/showthread.php?s=&threadid=28438
