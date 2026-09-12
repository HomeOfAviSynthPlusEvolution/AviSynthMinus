// Avisynth v2.5.  Copyright 2002-2009 Ben Rudiak-Gould et al.
// http://avisynth.nl

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .
//
// Linking Avisynth statically or dynamically with other modules is making a
// combined work based on Avisynth.  Thus, the terms and conditions of the GNU
// General Public License cover the whole combination.
//
// As a special exception, the copyright holders of Avisynth give you
// permission to link Avisynth with independent modules that communicate with
// Avisynth solely through the interfaces defined in avisynth.h, regardless of the license
// terms of these independent modules, and to copy and distribute the
// resulting combined work under terms of your choice, provided that
// every copy of the combined work is accompanied by a complete copy of
// the source code of Avisynth (the version of Avisynth used to produce the
// combined work), being distributed under the terms of the GNU General
// Public License plus this exception.  An independent module is a module
// which is not derived from or based on Avisynth, such as 3rd-party filters,
// import and export plugins, or graphical user interfaces.

#include "convert_bits.h"
#include "convert_helper.h"
#include <algorithm>

ConvertBits::ConvertBits(PClip _child, const int _dither_mode, const int _target_bitdepth, bool _truerange,
  int _ColorRange_src, int _ColorRange_dest,
  int _dither_bitdepth, IScriptEnvironment* env, bool _source_range_from_frame) :
  GenericVideoFilter(_child),
  target_bitdepth(_target_bitdepth), dither_mode(_dither_mode), dither_bitdepth(_dither_bitdepth),
  fulls(false), fulld(false), truerange(_truerange),
  source_range_from_frame(_source_range_from_frame), default_source_full(false)
{

  default_source_full = vi.IsRGB();
  pixelsize = vi.ComponentSize();
  bits_per_pixel = vi.BitsPerComponent();
  format_change_only = false;

  // full or limited decision
  // dest: if undefined, use src
  if (_ColorRange_dest != ColorRange_e::AVS_RANGE_LIMITED && _ColorRange_dest != ColorRange_e::AVS_RANGE_FULL) {
    _ColorRange_dest = _ColorRange_src;
  }
  //
  fulls = _ColorRange_src == ColorRange_e::AVS_RANGE_FULL;
  fulld = _ColorRange_dest == ColorRange_e::AVS_RANGE_FULL;

  if (!truerange) {
    if ((target_bitdepth == 8 || target_bitdepth == 32) && pixelsize == 2)
      bits_per_pixel = 16;
    if (target_bitdepth > 8 && target_bitdepth <= 16 && (bits_per_pixel == 8 || bits_per_pixel == 32))
      target_bitdepth = 16;
    if (target_bitdepth > 8 && target_bitdepth <= 16 && bits_per_pixel > 8 && bits_per_pixel <= 16)
      format_change_only = true;
  }

  if (!format_change_only) {
    depth_plans = std::make_unique<avs_video_convert::DepthPlans>(bits_per_pixel, target_bitdepth, fulld, env);
    if (dither_mode >= 0)
      dither_plans = std::make_unique<avs_video_convert::DitherPlans>(bits_per_pixel, target_bitdepth, dither_bitdepth, dither_mode, fulld, env);
  }

  // Set VideoInfo
  if (target_bitdepth == 8) {
    if (vi.NumComponents() == 1)
      vi.pixel_type = VideoInfo::CS_Y8;
    else if (vi.IsYV411())
      vi.pixel_type = VideoInfo::CS_YV411;
    else if (vi.Is420() || vi.IsYV12())
      vi.pixel_type = vi.IsYUVA() ? VideoInfo::CS_YUVA420 : VideoInfo::CS_YV12;
    else if (vi.Is422())
      vi.pixel_type = vi.IsYUVA() ? VideoInfo::CS_YUVA422 : VideoInfo::CS_YV16;
    else if (vi.Is444())
      vi.pixel_type = vi.IsYUVA() ? VideoInfo::CS_YUVA444 : VideoInfo::CS_YV24;
    else if (vi.IsRGB48() || vi.IsRGB24())
      vi.pixel_type = VideoInfo::CS_BGR24;
    else if (vi.IsRGB64() || vi.IsRGB32())
      vi.pixel_type = VideoInfo::CS_BGR32;
    else if (vi.IsPlanarRGB())
      vi.pixel_type = VideoInfo::CS_RGBP;
    else if (vi.IsPlanarRGBA())
      vi.pixel_type = VideoInfo::CS_RGBAP;
    else
      env->ThrowError("ConvertTo8bit: unsupported color space");

    return;
  }
  else if (target_bitdepth > 8 && target_bitdepth <= 16) {
    // set output vi format
    if (vi.IsRGB24() || vi.IsRGB48()) {
      vi.pixel_type = VideoInfo::CS_BGR48;
    }
    else if (vi.IsRGB32() || vi.IsRGB64()) {
      vi.pixel_type = VideoInfo::CS_BGR64;
    }
    else {
      // Y or YUV(A) or PlanarRGB(A)
      if (vi.IsYV12()) // YV12 can have an exotic compatibility constant
        vi.pixel_type = VideoInfo::CS_YV12; // override for known
      int new_bitdepth_bits;
      switch (target_bitdepth) {
      case 8: new_bitdepth_bits = VideoInfo::CS_Sample_Bits_8; break;
      case 10: new_bitdepth_bits = VideoInfo::CS_Sample_Bits_10; break;
      case 12: new_bitdepth_bits = VideoInfo::CS_Sample_Bits_12; break;
      case 14: new_bitdepth_bits = VideoInfo::CS_Sample_Bits_14; break;
      case 16: new_bitdepth_bits = VideoInfo::CS_Sample_Bits_16; break;
      case 32: new_bitdepth_bits = VideoInfo::CS_Sample_Bits_32; break;
      }
      vi.pixel_type = (vi.pixel_type & ~VideoInfo::CS_Sample_Bits_Mask) | new_bitdepth_bits;
    }
    return;
  }
  else if (target_bitdepth == 32) {
    if (vi.NumComponents() == 1)
      vi.pixel_type = VideoInfo::CS_Y32;
    else if (vi.Is420())
      vi.pixel_type = vi.IsYUVA() ? VideoInfo::CS_YUVA420PS : VideoInfo::CS_YUV420PS;
    else if (vi.Is422())
      vi.pixel_type = vi.IsYUVA() ? VideoInfo::CS_YUVA422PS : VideoInfo::CS_YUV422PS;
    else if (vi.Is444())
      vi.pixel_type = vi.IsYUVA() ? VideoInfo::CS_YUVA444PS : VideoInfo::CS_YUV444PS;
    else if (vi.IsPlanarRGB())
      vi.pixel_type = VideoInfo::CS_RGBPS;
    else if (vi.IsPlanarRGBA())
      vi.pixel_type = VideoInfo::CS_RGBAPS;
    else
      env->ThrowError("ConvertToFloat: unsupported color space");

    return;
  }

  env->ThrowError("ConvertBits: unsupported target bit-depth (%d)", target_bitdepth);

}

AVSValue __cdecl ConvertBits::Create(AVSValue args, void* user_data, IScriptEnvironment* env) {
  PClip clip = args[0].AsClip();
  //0   1        2        3         4         5           6
  //c[bits]i[truerange]b[dither]i[dither_bits]i[fulls]b[fulld]b

  const VideoInfo &vi = clip->GetVideoInfo();

  int create_param = (int)reinterpret_cast<intptr_t>(user_data);

  // when converting from/true 10-16 bit formats, truerange=false indicates bitdepth of 16 bits regardless of the 10-12-14 bit format
  // FIXME: stop supporting this parameter (a workaround in the dawn of hbd?)
  bool assume_truerange = args[2].AsBool(true); // n/a for non planar formats

  int source_bitdepth = vi.BitsPerComponent();
  // default comes from old legacy To8,To16,ToFloat functions
  // or the clip's actual bit depth
  const int default_target_bitdepth = create_param == 0 ? source_bitdepth : create_param;
  int target_bitdepth = args[1].AsInt(default_target_bitdepth); // "bits" parameter
  int dither_bitdepth = args[4].AsInt(target_bitdepth); // "dither_bits" parameter

  if(target_bitdepth!=8 && target_bitdepth!=10 && target_bitdepth!=12 && target_bitdepth!=14 && target_bitdepth!=16 && target_bitdepth!=32)
    env->ThrowError("ConvertBits: invalid bit depth: %d", target_bitdepth);

  if(create_param == 8 && target_bitdepth !=8)
    env->ThrowError("ConvertTo8Bit: invalid bit depth: %d", target_bitdepth);
  if(create_param == 32 && target_bitdepth !=32)
    env->ThrowError("ConvertToFloat: invalid bit depth: %d", target_bitdepth);
  if(create_param == 16 && (target_bitdepth == 8 || target_bitdepth ==32))
    env->ThrowError("ConvertTo16bit: invalid bit depth: %d", target_bitdepth);

  if (args[2].Defined()) {
    if (!vi.IsPlanar())
      env->ThrowError("ConvertBits: truerange specified for non-planar source");
  }

  // retrieve full/limited
  int ColorRange_src;
  int ColorRange_dest;
  const bool source_range_from_frame = !args[5].Defined();
  if (args[5].Defined())
    ColorRange_src = args[5].AsBool() ? ColorRange_e::AVS_RANGE_FULL : ColorRange_e::AVS_RANGE_LIMITED;
  else
    ColorRange_src = -1; // undefined. A frame property may override
  if (args[6].Defined())
    ColorRange_dest = args[6].AsBool() ? ColorRange_e::AVS_RANGE_FULL : ColorRange_e::AVS_RANGE_LIMITED;
  else
    ColorRange_dest = -1; // undefined. A frame property or ColorRange_src may override
  if (ColorRange_src != ColorRange_e::AVS_RANGE_LIMITED && ColorRange_src != ColorRange_e::AVS_RANGE_FULL) {
    // try getting frame props if parameter is not specified
    auto frame0 = clip->GetFrame(0, env);
    const AVSMap* props = env->getFramePropsRO(frame0);
    if (env->propNumElements(props, "_ColorRange") > 0) {
      ColorRange_src = (int)env->propGetIntSaturated(props, "_ColorRange", 0, nullptr);
      if (ColorRange_src != ColorRange_e::AVS_RANGE_LIMITED &&
          ColorRange_src != ColorRange_e::AVS_RANGE_FULL)
        env->ThrowError("ConvertBits: unsupported _ColorRange value: %d", ColorRange_src);
    }
    else {
      // no param, no frame property -> rgb is full others are limited
      ColorRange_src = vi.IsRGB() ? ColorRange_e::AVS_RANGE_FULL : ColorRange_e::AVS_RANGE_LIMITED;
    }
  }
  // cr_dest = cr_source if not specified
  if (ColorRange_dest != ColorRange_e::AVS_RANGE_LIMITED && ColorRange_dest != ColorRange_e::AVS_RANGE_FULL) {
    ColorRange_dest = ColorRange_src;
  }
  bool fulls = ColorRange_src == ColorRange_e::AVS_RANGE_FULL;
  bool fulld = ColorRange_dest == ColorRange_e::AVS_RANGE_FULL;


  int dither_type = args[3].AsInt(-1);
  bool dither_defined = args[3].Defined();
  if(dither_defined && dither_type != 1 && dither_type != 0 && dither_type != -1)
    env->ThrowError("ConvertBits: invalid dither type parameter. Only -1 (disabled), 0 (ordered dither) or 1 (Floyd-S) is allowed");

  if (dither_type >= 0) {
    if (source_bitdepth < target_bitdepth)
      env->ThrowError("ConvertBits: dithering is allowed only for scale down");
    if (dither_bitdepth > target_bitdepth)
      env->ThrowError("ConvertBits: dither_bits must be <= target bitdepth");
    if (target_bitdepth == 32)
      env->ThrowError("ConvertBits: dithering is not allowed into 32 bit float target");
  }

  // 3.7.1 t25
  // Unfortunately 32 bit float dithering is not implemented, thus we convert to 16 bit 
  // intermediate clip
  if (source_bitdepth == 32 && (dither_type == 0 || dither_type == 1)) {
    // c[bits]i[truerange]b[dither]i[dither_bits]i[fulls]b[fulld]b

    source_bitdepth = 16;
    // solving ordered dither maximum bit depth difference of 8 problem
    // by automatic preconversion
    if (dither_type == 0 && source_bitdepth - dither_bitdepth > 8) {
      source_bitdepth = dither_bitdepth + 8;
      if (source_bitdepth % 2)
        source_bitdepth--; // must be even
    }
    
    AVSValue new_args[7] = { clip, source_bitdepth, true, -1 /* no dither */, AVSValue() /*dither_bits*/, fulls, fulld };
    clip = env->Invoke("ConvertBits", AVSValue(new_args, 7)).AsClip();

    clip = env->Invoke("Cache", AVSValue(clip)).AsClip();
    // and now the source range becomes the previous target
    fulls = fulld;
    ColorRange_src = ColorRange_dest;
  }

  // solving ordered dither maximum bit depth difference of 8 problem
  // by automatic preconversion
  if (source_bitdepth <= 16 && dither_type == 0 && source_bitdepth - dither_bitdepth > 8) {
    // c[bits]i[truerange]b[dither]i[dither_bits]i[fulls]b[fulld]b
    source_bitdepth = dither_bitdepth + 8;
    if (source_bitdepth % 2)
      source_bitdepth--; // must be even

    AVSValue new_args[7] = { clip, source_bitdepth, true, -1 /* no dither */, AVSValue() /*dither_bits*/, fulls, fulld };
    clip = env->Invoke("ConvertBits", AVSValue(new_args, 7)).AsClip();

    clip = env->Invoke("Cache", AVSValue(clip)).AsClip();
    // and now the source range becomes the previous target
    fulls = fulld;
    ColorRange_src = ColorRange_dest;
  }

  if (source_bitdepth == dither_bitdepth)
    dither_type = -1; // ignore dithering

  if(dither_type == 0) {
    if (dither_bitdepth < 1 || dither_bitdepth > 16)
      env->ThrowError("ConvertBits: ordered dither: invalid dither_bits specified (1-16 allowed)");

    // this error message cannot appear if the automatic bit depth reducing conversions above are done
    if(source_bitdepth - dither_bitdepth > 8)
      env->ThrowError("ConvertBits: dither_bits cannot differ with more than 8 bits from source");
  }

  // floyd
  if (dither_type == 1) {
    if (dither_bitdepth < 1 || dither_bitdepth > 16)
      env->ThrowError("ConvertBits: Floyd-S: invalid dither_bits specified (1-16 allowed)");
  }

  // no change -> return unmodified if no transform required
  if (source_bitdepth == target_bitdepth) { // 10->10 .. 16->16
    if((dither_type < 0 || dither_bitdepth == target_bitdepth) && fulls == fulld)
      return clip;
  }

  // YUY2 conversion is limited
  if (vi.IsYUY2()) {
    if (target_bitdepth != 8)
      env->ThrowError("ConvertBits: YUY2 input must stay in 8 bits");
  }

  if (vi.IsYV411()) {
    if (target_bitdepth != 8)
      env->ThrowError("ConvertBits: YV411 input must stay in 8 bits");
  }

  // packed RGB conversion is limited
  if (vi.IsRGB() && !vi.IsPlanar()) {
    if (target_bitdepth != 8 && target_bitdepth != 16)
      env->ThrowError("ConvertBits: invalid bit-depth for packed RGB formats, only 8 or 16 possible");
  }

    // remark
    // source_10_bit.ConvertTo16bit(truerange=true)  : upscale range
    // source_10_bit.ConvertTo16bit(truerange=false) : leaves data, only format conversion
    // source_10_bit.ConvertTo16bit(bits=12,truerange=true)  : upscale range from 10 to 12
    // source_10_bit.ConvertTo16bit(bits=12,truerange=false) : leaves data, only format conversion
    // source_16_bit.ConvertTo16bit(bits=10, truerange=true)  : downscale range
    // source_16_bit.ConvertTo16bit(bits=10, truerange=false) : leaves data, only format conversion

  // yuy2 is autoconverted to/from YV16. fulls-fulld and dither to lower bit depths are supported
  bool need_convert_yuy2 = vi.IsYUY2();
  // for dither, planar rgb conversion happens
  bool need_convert_24 = vi.IsRGB24() && dither_type >= 0;
  bool need_convert_32 = vi.IsRGB32() && dither_type >= 0;
  bool need_convert_48 = vi.IsRGB48() && dither_type >= 0;
  bool need_convert_64 = vi.IsRGB64() && dither_type >= 0;

  // convert to planar on the fly if dither was asked
  if (need_convert_24 || need_convert_48) {
    AVSValue new_args[1] = { clip };
    clip = env->Invoke("ConvertToPlanarRGB", AVSValue(new_args, 1)).AsClip();
  }
  else if (need_convert_32 || need_convert_64) {
    AVSValue new_args[1] = { clip };
    clip = env->Invoke("ConvertToPlanarRGBA", AVSValue(new_args, 1)).AsClip();
  }
  else if (need_convert_yuy2) {
    AVSValue new_args[1] = { clip };
    clip = env->Invoke("ConvertToYV16", AVSValue(new_args, 1)).AsClip();
  }

  // For large gaps originated from a larger dither_bits, we created an intermediate clip,
  // because the algorithm can handle only a maximum of 8 bits difference.
  // In the callee get_convert_any_bits_functions only selects a dither implementation when
  // target_bitdepth <= source_bitdepth ("dither is only down"). The automatic
  // preconversion would result in a (possibly) reduced source_bitdepth that has been
  // changed above, and is _lower_ than the still original target_bitdepth.
  // Case: 16->16 with dither_bits=1 gets preconverted to an 8-bit intermediate, but still
  // has to reach 16-bit output. So we do dither in-place at source_bitdepth first, then simply expand
  // it back up.
  const bool need_expand_back_after_dither = (dither_type >= 0 && target_bitdepth > source_bitdepth);
  const int final_target_bitdepth = target_bitdepth;
  if (need_expand_back_after_dither)
    target_bitdepth = source_bitdepth; // temporarily change in order to do the dithering effectively

  AVSValue result = new ConvertBits(clip, dither_type, target_bitdepth, assume_truerange, ColorRange_src, ColorRange_dest, dither_bitdepth, env, source_range_from_frame);

  if (need_expand_back_after_dither) {
    // scale back w/o dithering
    AVSValue new_args[7] = { result, final_target_bitdepth, true, -1 /* no dither */, AVSValue() /*dither_bits*/, fulld, fulld };
    result = env->Invoke("ConvertBits", AVSValue(new_args, 7)).AsClip();
    target_bitdepth = final_target_bitdepth; // back to the originally requested bit-depth
  }

  // convert back to packed rgb from planar on the fly
  if (need_convert_24 || need_convert_48) {
    AVSValue new_args[1] = { result };
    if(target_bitdepth == 8)
      result = env->Invoke("ConvertToRGB24", AVSValue(new_args, 1)).AsClip();
    else
      result = env->Invoke("ConvertToRGB48", AVSValue(new_args, 1)).AsClip();
  } else if (need_convert_32 || need_convert_64) {
    AVSValue new_args[1] = { result };
    if (target_bitdepth == 8)
      result = env->Invoke("ConvertToRGB32", AVSValue(new_args, 1)).AsClip();
    else
      result = env->Invoke("ConvertToRGB64", AVSValue(new_args, 1)).AsClip();
  }
  else if (need_convert_yuy2) {
    AVSValue new_args[1] = { result };
    result = env->Invoke("ConvertToYUY2", AVSValue(new_args, 1)).AsClip();
  }

  return result;
}


PVideoFrame __stdcall ConvertBits::GetFrame(int n, IScriptEnvironment* env) {
  PVideoFrame src = child->GetFrame(n, env);

  bool frame_source_full = fulls;
  if (source_range_from_frame) {
    int color_range = -1;
    const AVSMap* props = env->getFramePropsRO(src);
    if (env->propNumElements(props, "_ColorRange") > 0) {
      color_range = (int)env->propGetIntSaturated(props, "_ColorRange", 0, nullptr);
      if (color_range != ColorRange_e::AVS_RANGE_LIMITED &&
          color_range != ColorRange_e::AVS_RANGE_FULL)
        env->ThrowError("ConvertBits: unsupported _ColorRange value: %d", color_range);
    }

    frame_source_full = color_range == ColorRange_e::AVS_RANGE_FULL ||
      (color_range < 0 && default_source_full);
  }

  if (format_change_only)
  {
    // for 10-16 bit: simple format override in constructor
    env->MakeWritable(&src);
    src->AmendPixelType(vi.pixel_type);
    return src;
  }

  PVideoFrame dst = env->NewVideoFrameP(vi, &src);

  auto props = env->getFramePropsRW(dst);
  update_ColorRange(props, fulld ? ColorRange_e::AVS_RANGE_FULL : ColorRange_e::AVS_RANGE_LIMITED, env);

  if (dither_mode < 0) {
    depth_plans->Convert(src, dst, vi, frame_source_full, env);
    return dst;
  }

  if(vi.IsPlanar())
  {
    int planes_y[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
    int planes_r[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
    int *planes = (vi.IsYUV() || vi.IsYUVA()) ? planes_y : planes_r;
    for (int p = 0; p < vi.NumComponents(); ++p) {
      const int plane = planes[p];
      if (plane == PLANAR_A) {
        depth_plans->ConvertAlpha(src, dst, env);
      }
      else {
        dither_plans->ConvertPlane(src, dst, plane, frame_source_full, env);
      }
    }
  }
  else {
    // The public factory planarizes packed clips before applying dithering.
    env->ThrowError("ConvertBits: dithering requires a planar intermediate");
  }
  return dst;
}
