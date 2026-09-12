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


#include "convert.h"
#include <cmath>
#include "convert_matrix.h"
#include "convert_helper.h"
#include "convert_bits.h"
#include "convert_planar.h"
#include "convert_rgb.h"


#include <avs/alignment.h>
#include <avs/minmax.h>
#include <avs/config.h>
#include <tuple>
#include <map>
#include <algorithm>

#ifdef AVS_WINDOWS
#include <avs/win.h>
#else
#include <avs/posix.h>
#endif

/********************************************************************
***** Declare index of new filters for Avisynth's filter engine *****
********************************************************************/

extern const AVSFunction Convert_filters[] = {       // matrix can be "rec601", "rec709", "PC.601" or "PC.709" or "rec2020" or "PC.2020"
  { "ConvertToRGB",   BUILTIN_FUNC_PREFIX, "c[matrix]s[interlaced]b[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToRGB::Create, (void *)0 },
  { "ConvertToRGB24", BUILTIN_FUNC_PREFIX, "c[matrix]s[interlaced]b[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToRGB::Create, (void *)24 },
  { "ConvertToRGB32", BUILTIN_FUNC_PREFIX, "c[matrix]s[interlaced]b[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToRGB::Create, (void *)32 },
  { "ConvertToRGB48", BUILTIN_FUNC_PREFIX, "c[matrix]s[interlaced]b[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToRGB::Create, (void *)48 },
  { "ConvertToRGB64", BUILTIN_FUNC_PREFIX, "c[matrix]s[interlaced]b[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToRGB::Create, (void *)64 },
  { "ConvertToPlanarRGB",  BUILTIN_FUNC_PREFIX, "c[matrix]s[interlaced]b[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToRGB::Create, (void *)-1 },
  { "ConvertToPlanarRGBA", BUILTIN_FUNC_PREFIX, "c[matrix]s[interlaced]b[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToRGB::Create, (void *)-2 },
  { "ConvertToY8",    BUILTIN_FUNC_PREFIX, "c[matrix]s", ConvertToY::Create, (void*)0 }, // user_data == 0 -> only 8 bit sources
  { "ConvertToYV12",  BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[ChromaOutPlacement]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYUV420, (void*)0 },
  { "ConvertToYV24",  BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYUV444, (void*)0},
  { "ConvertToYV16",  BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[ChromaOutPlacement]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYUV422, (void*)0},
  { "ConvertToYV411", BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYV411, (void*)0},
  { "ConvertToYUY2",  BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f[ChromaOutPlacement]s", ConvertToPlanarGeneric::CreateConvertToYUY2 },
  { "ConvertBackToYUY2", BUILTIN_FUNC_PREFIX, "c[matrix]s", ConvertToPlanarGeneric::CreateConvertBackToYUY2 },
  { "ConvertToY",       BUILTIN_FUNC_PREFIX, "c[matrix]s", ConvertToY::Create, (void*)1 }, // user_data == 1 -> any bit depth sources
  { "ConvertToYUV411", BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYV411, (void*)1}, // alias for ConvertToYV411, 8 bit check later
  { "ConvertToYUV420",  BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[ChromaOutPlacement]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYUV420, (void*)1},
  { "ConvertToYUV422",  BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[ChromaOutPlacement]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYUV422, (void*)1},
  { "ConvertToYUV444",  BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYUV444, (void*)1},
  { "ConvertToYUVA420", BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[ChromaOutPlacement]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYUV420, (void*)2},
  { "ConvertToYUVA422", BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[ChromaOutPlacement]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYUV422, (void*)2},
  { "ConvertToYUVA444", BUILTIN_FUNC_PREFIX, "c[interlaced]b[matrix]s[ChromaInPlacement]s[chromaresample]s[param1]f[param2]f[param3]f", ConvertToPlanarGeneric::CreateYUV444, (void*)2},
  { "ConvertTo8bit",  BUILTIN_FUNC_PREFIX, "c[bits]i[truerange]b[dither]i[dither_bits]i[fulls]b[fulld]b", ConvertBits::Create, (void *)8 },
  { "ConvertTo16bit", BUILTIN_FUNC_PREFIX, "c[bits]i[truerange]b[dither]i[dither_bits]i[fulls]b[fulld]b", ConvertBits::Create, (void *)16 },
  { "ConvertToFloat", BUILTIN_FUNC_PREFIX, "c[bits]i[truerange]b[dither]i[dither_bits]i[fulls]b[fulld]b", ConvertBits::Create, (void *)32 },
  { "ConvertBits",    BUILTIN_FUNC_PREFIX, "c[bits]i[truerange]b[dither]i[dither_bits]i[fulls]b[fulld]b", ConvertBits::Create, (void *)0 },
  { "AddAlphaPlane",  BUILTIN_FUNC_PREFIX, "c[mask].", AddAlphaPlane::Create},
  { "RemoveAlphaPlane",  BUILTIN_FUNC_PREFIX, "c", RemoveAlphaPlane::Create},
  { 0 }
};


/****************************************
*******   Convert to RGB / RGBA   ******
***************************************/

// Shared RGB conversion factory.
AVSValue __cdecl ConvertToRGB::Create(AVSValue args, void* user_data, IScriptEnvironment* env)
{
  const bool haveOpts = args[3].Defined() || args[4].Defined();
  PClip clip = args[0].AsClip();
  const char* const matrix_name = args[1].AsString(0);
  VideoInfo vi = clip->GetVideoInfo();

  // common Create for all CreateRGB24/32/48/64/Planar(RGBP:-1, RGPAP:-2) using user_data
  int target_rgbtype = (int)reinterpret_cast<intptr_t>(user_data);
  // -1,-2: Planar RGB(A)
  //  0: not specified (leave if input is packed RGB, convert to rgb32/64 input colorspace dependent)
  // 24,32,48,64: RGB24/32/48/64

  if (target_rgbtype == 0 && vi.BitsPerComponent() != 8 && vi.BitsPerComponent() != 16)
    env->ThrowError("ConvertToRGB: conversion is allowed only from 8 or 16 bit colorspaces");

  if (vi.IsYUY2()) {
    clip = new ConvertYUY2ToYV16(clip, env);
    vi = clip->GetVideoInfo();
  }

  // planar YUV-like
  if (vi.IsPlanar() && (vi.IsYUV() || vi.IsYUVA())) {
    bool needConvertFinalBitdepth = false;
    int finalBitdepth = -1;

    AVSValue new_args[8] = { clip, args[2], args[1], args[3], args[4], args[5], args[6], args[7]};
    // conversion to planar or packed RGB is always from 444
    // clip, interlaced, matrix, chromainplacement, chromaresample, param1, param2, param3
    clip = ConvertToPlanarGeneric::CreateYUV444(AVSValue(new_args, 8), (void *)1, env).AsClip(); // (void *)1: not restricted to 8 bits
    if ((target_rgbtype == 24 || target_rgbtype == 32)) {
      if (vi.BitsPerComponent() != 8) {
        needConvertFinalBitdepth = true;
        finalBitdepth = 8;
        target_rgbtype = (target_rgbtype == 24) ? -1 : -2; // planar rgb intermediate
      }
    }
    else if ((target_rgbtype == 48 || target_rgbtype == 64)) {
      if (vi.BitsPerComponent() != 16) {
        needConvertFinalBitdepth = true;
        finalBitdepth = 16;
        target_rgbtype = (target_rgbtype == 48) ? -1 : -2; // planar rgb intermediate
      }
    }
    int rgbtype_param = 0;
    bool reallyConvert = true;
    switch (target_rgbtype)
    {
    case -1: case -2:
        rgbtype_param = target_rgbtype; break; // planar RGB(A)
    case 0:
        rgbtype_param = vi.ComponentSize() == 1 ? 4 : 8; break; // input bitdepth adaptive
    case 24:
        rgbtype_param = 3; break; // RGB24
    case 32:
        rgbtype_param = 4; break; // RGB32
    case 48: {
            // instead of C code of YUV444P16->RGB48
            // we convert to PlanarRGB then to RGB48 (both is fast)
          AVSValue new_args2[8] = { clip, args[1], args[2], args[3], args[4], args[5], args[6], args[7] };
          clip = ConvertToRGB::Create(AVSValue(new_args2, 8), (void *)-1, env).AsClip();
          vi = clip->GetVideoInfo();
          reallyConvert = false;
          rgbtype_param = 6; // old option RGB48 target, slow C
        }
        break; // RGB48
    case 64: {
        // instead of C code of YUV(A)444P16->RGB64
        // we convert to PlanarRGB(A) then to RGB64 (both is fast)
        AVSValue new_args2[8] = { clip, args[1], args[2], args[3], args[4], args[5], args[6], args[7] };
        clip = ConvertToRGB::Create(AVSValue(new_args2, 8), vi.IsYUVA() ? (void *)-2 : (void *)-1, env).AsClip();
        vi = clip->GetVideoInfo();
        reallyConvert = false;
        rgbtype_param = 8; // old option RGB64 target, slow C
      }
      break; // RGB64
    }
    if (reallyConvert) {
      clip = new ConvertYUV444ToRGB(clip, matrix_name, rgbtype_param, env);

      if (needConvertFinalBitdepth) {
        // from any planar rgb(a) -> rgb24/32/48/64
        clip = new ConvertBits(clip, -1 /*dither_type*/, finalBitdepth /*target_bitdepth*/, true /*assume_truerange*/, 
          ColorRange_e::AVS_RANGE_FULL /*fulls*/, ColorRange_e::AVS_RANGE_FULL /*fulld*/, 
          8 /*n/a dither_bitdepth*/, env);
        vi = clip->GetVideoInfo();

        // source here is always a 8/16bit planar RGB(A), finally it has to be converted to RGB24/32/48/64
        const bool isRGBA = target_rgbtype == -2;
        clip = new PlanarRGBtoPackedRGB(clip, isRGBA, env);
        vi = clip->GetVideoInfo();
      }
      return clip;
    }
  }

  if (haveOpts)
    env->ThrowError("ConvertToRGB: ChromaPlacement and ChromaResample options are not supported.");

  // planar RGB-like source
  if (vi.IsPlanarRGB() || vi.IsPlanarRGBA())
  {
    if (target_rgbtype < 0) // planar to planar
    {
      if (vi.IsPlanarRGB()) {
        if (target_rgbtype == -1)
          return clip;
        // prgb->prgba create with default alpha
        return new AddAlphaPlane(clip, nullptr, 0.0f, false, env);
      }
      // planar rgba source
      if (target_rgbtype == -2)
        return clip;
      return new RemoveAlphaPlane(clip, env);
    }

    // planar to packed 24/32/48/64
    bool needConvertFinalBitdepth = false;
    int finalBitdepth = -1;

    if (target_rgbtype == 24 || target_rgbtype == 32) {
      if (vi.BitsPerComponent() != 8) {
        needConvertFinalBitdepth = true;
        finalBitdepth = 8;
      }
    }
    else if (target_rgbtype == 48 || target_rgbtype == 64) {
      if (vi.BitsPerComponent() != 16) {
        needConvertFinalBitdepth = true;
        finalBitdepth = 16;
      }
    }

    if (needConvertFinalBitdepth) {
      // plain Invoke instead of "new ConvertBits", this detects and keeps source and target ranges
      AVSValue new_args[2] = { clip, finalBitdepth };
      clip = env->Invoke("ConvertBits", AVSValue(new_args, 2)).AsClip();
      vi = clip->GetVideoInfo();
    }

    bool hasAlpha = target_rgbtype == 32 || target_rgbtype == 64 ||
      (target_rgbtype == 0 && vi.IsPlanarRGBA());

    return new PlanarRGBtoPackedRGB(clip, hasAlpha, env);
  }

  // conversions from packed RGB

  if (target_rgbtype == 24 || target_rgbtype == 32) {
    if (vi.ComponentSize() != 1) {
      // 64->32, 48->24
      // using Invoke instead of new ConvertBits, this detects and keeps source and target ranges
      AVSValue new_args[2] = { clip, 8 };
      clip = env->Invoke("ConvertBits", AVSValue(new_args, 2)).AsClip();
      vi = clip->GetVideoInfo(); // new format
    }
  }
  else if (target_rgbtype == 48 || target_rgbtype == 64) {
    if (vi.ComponentSize() != 2) {
      // 32->64, 24->48
      // using Invoke instead of new ConvertBits, this detects and keeps source and target ranges
      AVSValue new_args[2] = { clip, 16 };
      clip = env->Invoke("ConvertBits", AVSValue(new_args, 2)).AsClip();
      vi = clip->GetVideoInfo(); // new format
    }
  }

  if(target_rgbtype==32 || target_rgbtype==64)
      if (vi.IsRGB24() || vi.IsRGB48())
          return new RGBtoRGBA(clip, env); // 24->32 or 48->64

  if(target_rgbtype==24 || target_rgbtype==48)
      if (vi.IsRGB32() || vi.IsRGB64())
          return new RGBAtoRGB(clip, env); // 32->24 or 64->48

  // <0: target is planar RGB(A)
  if (target_rgbtype < 0) {
    // RGB24/32/48/64 ->
    const bool isSrcRGBA = vi.IsRGB32() || vi.IsRGB64();
    const bool isTargetRGBA = target_rgbtype == -2;
    return new PackedRGBtoPlanarRGB(clip, isSrcRGBA, isTargetRGBA, env);
  }

  return clip;
}


/**********************************
*******   Convert to YV12   ******
*********************************/

AVSValue AddAlphaPlane::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  bool isMaskDefined = args[1].Defined();
  bool maskIsClip = false;
  // if mask is not defined and videoformat has Alpha then we return
  if(isMaskDefined && !args[1].IsClip() && !args[1].IsFloat())
    env->ThrowError("AddAlphaPlane: mask parameter should be clip or number");
  const VideoInfo& vi = args[0].AsClip()->GetVideoInfo();
  if (!isMaskDefined && (vi.IsPlanarRGBA() || vi.IsYUVA() || vi.IsRGB32() || vi.IsRGB64()))
    return args[0].AsClip();
  PClip alphaClip = nullptr;
  if (isMaskDefined && args[1].IsClip()) {
    const VideoInfo& viAlphaClip = args[1].AsClip()->GetVideoInfo();
    maskIsClip = true;
    if(viAlphaClip.BitsPerComponent() != vi.BitsPerComponent())
      env->ThrowError("AddAlphaPlane: alpha clip is of different bit depth");
    if(viAlphaClip.width != vi.width || viAlphaClip.height != vi.height )
      env->ThrowError("AddAlphaPlane: alpha clip is of different size");
    if (viAlphaClip.IsY())
      alphaClip = args[1].AsClip();
    else if (viAlphaClip.NumComponents() == 4) {
      AVSValue new_args[1] = { args[1].AsClip() };
      alphaClip = env->Invoke("ExtractA", AVSValue(new_args, 1)).AsClip();
    } else {
      env->ThrowError("AddAlphaPlane: alpha clip should be greyscale or should have alpha plane");
    }
    // alphaClip is always greyscale here
  }
  float maskAsFloat = -1.0f;
  if (!maskIsClip) {
    maskAsFloat = (float)args[1].AsFloat(-1.0f);
    if (isMaskDefined && std::isnan(maskAsFloat))
      env->ThrowError("AddAlphaPlane: mask/opacity cannot be NaN");
  }
  if (args.ArraySize() >= 3 && args[2].Defined() && args[2].IsFloat() && std::isnan(args[2].AsFloat())) {
    env->ThrowError("AddAlphaPlane: mask/opacity cannot be NaN");
  }
  if (vi.IsRGB24()) {
    AVSValue new_args[1] = { args[0].AsClip() };
    PClip child = env->Invoke("ConvertToRGB32", AVSValue(new_args, 1)).AsClip();
    return new AddAlphaPlane(child, alphaClip, maskAsFloat, isMaskDefined, env);
  } else if(vi.IsRGB48()) {
    AVSValue new_args[1] = { args[0].AsClip() };
    PClip child = env->Invoke("ConvertToRGB64", AVSValue(new_args, 1)).AsClip();
    return new AddAlphaPlane(child, alphaClip, maskAsFloat, isMaskDefined, env);
  }
  return new AddAlphaPlane(args[0].AsClip(), alphaClip, maskAsFloat, isMaskDefined, env);
}

AddAlphaPlane::AddAlphaPlane(PClip _child, PClip _alphaClip, float _mask_f, bool isMaskDefined, IScriptEnvironment* env)
  : GenericVideoFilter(_child), alphaClip(_alphaClip)
{
  if(vi.IsYUY2())
    env->ThrowError("AddAlphaPlane: YUY2 is not allowed");
  if(vi.IsY())
    env->ThrowError("AddAlphaPlane: greyscale source is not allowed");
  if(vi.IsYUV() && !vi.Is420() && !vi.Is422() && !vi.Is444()) // e.g. 410
    env->ThrowError("AddAlphaPlane: YUV format not supported, must be 420, 422 or 444");
  if(!vi.IsYUV() && !vi.IsYUVA() && !vi.IsRGB())
    env->ThrowError("AddAlphaPlane: format not supported");

  pixelsize = vi.ComponentSize();
  bits_per_pixel = vi.BitsPerComponent();

  if (vi.IsYUV()) {
    int pixel_type = vi.pixel_type;
    if (vi.IsYV12())
      pixel_type = VideoInfo::CS_YV12;
    int new_pixel_type = (pixel_type & ~VideoInfo::CS_YUV) | VideoInfo::CS_YUVA;
    vi.pixel_type = new_pixel_type;
  } else if(vi.IsPlanarRGB()) {
    int pixel_type = vi.pixel_type;
    int new_pixel_type = (pixel_type & ~VideoInfo::CS_RGB_TYPE) | VideoInfo::CS_RGBA_TYPE;
    vi.pixel_type = new_pixel_type;
  }
  // RGB24 and RGB48 already converted to 32/64
  // RGB32, RGB64, YUVA and RGBA: no change

  // mask parameter. If none->max transparency

  if (!alphaClip) {
    int max_pixel_value = (1 << bits_per_pixel) - 1; // n/a for float
    if (!isMaskDefined) {
      mask_f = 1.0f;
      mask = max_pixel_value;
    }
    else {
      mask_f = _mask_f;
      mask = (mask_f < 0) ? 0 : (mask_f > max_pixel_value) ? max_pixel_value : (int)mask_f;
      mask = clamp(mask, 0, max_pixel_value);
      // no clamp for float
    }
  }
}

PVideoFrame AddAlphaPlane::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);
  if(vi.IsPlanar())
  {
    int planes_y[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
    int planes_r[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
    int *planes = (vi.IsYUV() || vi.IsYUVA()) ? planes_y : planes_r;
    // copy existing 3 planes
    for (int p = 0; p < 3; ++p) {
      const int plane = planes[p];
      env->BitBlt(dst->GetWritePtr(plane), dst->GetPitch(plane), src->GetReadPtr(plane),
           src->GetPitch(plane), src->GetRowSize(plane), src->GetHeight(plane));
    }
  } else {
    // Packed RGB, already converted to RGB32 or RGB64
    env->BitBlt(dst->GetWritePtr(), dst->GetPitch(), src->GetReadPtr(),
      src->GetPitch(), src->GetRowSize(), src->GetHeight());
  }

  if (vi.IsPlanarRGBA() || vi.IsYUVA()) {
    if (alphaClip) {
      PVideoFrame srcAlpha = alphaClip->GetFrame(n, env);
      env->BitBlt(dst->GetWritePtr(PLANAR_A), dst->GetPitch(PLANAR_A), srcAlpha->GetReadPtr(PLANAR_Y),
        srcAlpha->GetPitch(PLANAR_Y), srcAlpha->GetRowSize(PLANAR_Y), srcAlpha->GetHeight(PLANAR_Y));
    }
    else {
      // default constant
      const int rowsizeA = dst->GetRowSize(PLANAR_A);
      const int dst_pitchA = dst->GetPitch(PLANAR_A);
      BYTE* dstp_a = dst->GetWritePtr(PLANAR_A);
      const int heightA = dst->GetHeight(PLANAR_A);

      switch (vi.ComponentSize())
      {
      case 1:
        fill_plane<BYTE>(dstp_a, heightA, rowsizeA, dst_pitchA, mask);
        break;
      case 2:
        fill_plane<uint16_t>(dstp_a, heightA, rowsizeA, dst_pitchA, mask);
        break;
      case 4:
        fill_plane<float>(dstp_a, heightA, rowsizeA, dst_pitchA, mask_f);
        break;
      }
    }
    return dst;
  }
  // RGB32 and RGB64

  BYTE* pf = dst->GetWritePtr();
  int pitch = dst->GetPitch();
  int rowsize = dst->GetRowSize();
  int height = dst->GetHeight();
  int width = vi.width;

  if (alphaClip) {
    // fill by alpha clip already converted to grey-only
    PVideoFrame srcAlpha = alphaClip->GetFrame(n, env);
    const BYTE* srcp_a = srcAlpha->GetReadPtr(PLANAR_Y);
    size_t pitch_a = srcAlpha->GetPitch(PLANAR_Y);

    pf += pitch * (vi.height - 1); // start from bottom: packed RGB is upside down

    if (vi.IsRGB32()) {
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x ++) {
          pf[x*4+3] = srcp_a[x];
        }
        pf -= pitch; // packed RGB is upside down
        srcp_a += pitch_a;
      }
    }
    else if (vi.IsRGB64()) {
      rowsize /= sizeof(uint16_t);
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x ++) {
          reinterpret_cast<uint16_t *>(pf)[x*4+3] = reinterpret_cast<const uint16_t *>(srcp_a)[x];
        }
        pf -= pitch; // packed RGB is upside down
        srcp_a += pitch_a;
      }
    }
  }
  else {
    // fill with constant
    if (vi.IsRGB32()) {
      for (int y = 0; y < height; y++) {
        for (int x = 3; x < rowsize; x += 4) {
          pf[x] = mask;
        }
        pf += pitch;
      }
    }
    else if (vi.IsRGB64()) {
      rowsize /= sizeof(uint16_t);
      for (int y = 0; y < height; y++) {
        for (int x = 3; x < rowsize; x += 4) {
          reinterpret_cast<uint16_t *>(pf)[x] = mask;
        }
        pf += pitch;
      }
    }
  }

  return dst;
}

AVSValue RemoveAlphaPlane::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  // if videoformat has no Alpha then we return
  const VideoInfo& vi = args[0].AsClip()->GetVideoInfo();
  if(vi.IsPlanar() && (vi.IsYUV() || vi.IsPlanarRGB())) // planar and no alpha
    return args[0].AsClip();
  if (vi.IsYUY2()) // YUY2: no alpha
    return args[0].AsClip();
  if(vi.IsRGB24() || vi.IsRGB48()) // packed RGB and no alpha
    return args[0].AsClip();
  if (vi.IsRGB32()) {
    AVSValue new_args[1] = { args[0].AsClip() };
    return env->Invoke("ConvertToRGB24", AVSValue(new_args, 1)).AsClip();
  }
  if (vi.IsRGB64()) {
    AVSValue new_args[1] = { args[0].AsClip() };
    return env->Invoke("ConvertToRGB48", AVSValue(new_args, 1)).AsClip();
  }
  return new RemoveAlphaPlane(args[0].AsClip(), env);
}

RemoveAlphaPlane::RemoveAlphaPlane(PClip _child, IScriptEnvironment* env)
  : GenericVideoFilter(_child)
{
  if(vi.IsYUY2())
    env->ThrowError("RemoveAlphaPlane: YUY2 is not allowed");
  if(vi.IsY())
    env->ThrowError("RemoveAlphaPlane: greyscale source is not allowed");

  if (vi.IsYUVA()) {
    int pixel_type = vi.pixel_type;
    int new_pixel_type = (pixel_type & ~VideoInfo::CS_YUVA) | VideoInfo::CS_YUV;
    vi.pixel_type = new_pixel_type;
  } else if(vi.IsPlanarRGBA()) {
    int pixel_type = vi.pixel_type;
    int new_pixel_type = (pixel_type & ~VideoInfo::CS_RGBA_TYPE) | VideoInfo::CS_RGB_TYPE;
    vi.pixel_type = new_pixel_type;
  }
}

PVideoFrame RemoveAlphaPlane::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  // Packed RGB: already handled in ::Create through Invoke 32->24 or 64->48 conversion
  // only planar here
  int planes_y[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
  int planes_r[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
  int *planes = (vi.IsYUV() || vi.IsYUVA()) ? planes_y : planes_r;
  // Abuse Subframe to snatch the YUV/GBR planes
  return env->SubframePlanar(src, 0, src->GetPitch(planes[0]), src->GetRowSize(planes[0]), src->GetHeight(planes[0]), 0, 0, src->GetPitch(planes[1]));

#if 0
  // BitBlt version. Kept for reference
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);
  // copy 3 planes w/o alpha
  for (int p = 0; p < 3; ++p) {
    const int plane = planes[p];
    env->BitBlt(dst->GetWritePtr(plane), dst->GetPitch(plane), src->GetReadPtr(plane),
      src->GetPitch(plane), src->GetRowSize(plane), src->GetHeight(plane));
  }
return dst;
#endif
}

