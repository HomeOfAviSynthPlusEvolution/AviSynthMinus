// Avisynth v2.5.  Copyright 2002 Ben Rudiak-Gould et al.
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


// Avisynth filter: YUV merge / Swap planes
// by Klaus Post (kp@interact.dk)
// adapted by Richard Berg (avisynth-dev@richardberg.net)
// iSSE code by Ian Brabham

#include <avisynth.h>
#include <cmath>
#include "merge.h"
#include "avs_composite/adapter.h"
#include "../core/internal.h"
#include "avs/alignment.h"
#include <cstdint>


/********************************************************************
***** Declare index of new filters for Avisynth's filter engine *****
********************************************************************/
extern const AVSFunction Merge_filters[] = {
  { "Merge",       BUILTIN_FUNC_PREFIX, "cc[weight]f", MergeAll::Create },  // src, src2, weight
  { "MergeChroma", BUILTIN_FUNC_PREFIX, "cc[weight]f", MergeChroma::Create },  // src, chroma src, weight
  { "MergeChroma", BUILTIN_FUNC_PREFIX, "cc[chromaweight]f", MergeChroma::Create },  // Legacy!
  { "MergeLuma",   BUILTIN_FUNC_PREFIX, "cc[weight]f", MergeLuma::Create },      // src, luma src, weight
  { "MergeLuma",   BUILTIN_FUNC_PREFIX, "cc[lumaweight]f", MergeLuma::Create },      // Legacy!
  { 0 }
};

static void merge_plane(BYTE* base, const BYTE* source, int base_pitch, int source_pitch,
                        int row_bytes, int height, float weight, int pixelsize, int bits, IScriptEnvironment* env) {
  AVS_UNUSED(pixelsize);
  // Retain the historical half-weight shortcut while sharing all target arithmetic.
  if (weight > 0.4961f && weight < 0.5039f) weight = 0.5f;
  avs_composite::MergePlane(base, source, base_pitch, source_pitch, row_bytes, height, bits, weight, env);
}

/****************************
******   Merge Chroma   *****
****************************/

MergeChroma::MergeChroma(PClip _child, PClip _clip, float _weight, IScriptEnvironment* env)
  : GenericVideoFilter(_child), clip(_clip), weight(_weight)
{
  if (std::isnan(_weight))
    env->ThrowError("MergeChroma: weight cannot be NaN");

  const VideoInfo& vi2 = clip->GetVideoInfo();

  if (!(vi.IsYUV() || vi.IsYUVA()) || !(vi2.IsYUV() || vi2.IsYUVA()))
    env->ThrowError("MergeChroma: YUV data only (no RGB); use ConvertToYUY2, ConvertToYV12/16/24 or ConvertToYUVxxx");

  if (!(vi.IsSameColorspace(vi2)))
    env->ThrowError("MergeChroma: YUV images must have same data type.");

  if (vi.width!=vi2.width || vi.height!=vi2.height)
    env->ThrowError("MergeChroma: Images must have same width and height!");

  if (weight<0.0f) weight=0.0f;
  if (weight>1.0f) weight=1.0f;

  pixelsize = vi.ComponentSize();
  bits_per_pixel = vi.BitsPerComponent();
}


PVideoFrame __stdcall MergeChroma::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);

  if (weight < 0.0039f) return src;

  PVideoFrame chroma = clip->GetFrame(n, env);

  int h = src->GetHeight();
  int w = src->GetRowSize(); // active row bytes

  if (weight < 0.9961f) {
    if (vi.IsYUY2()) {
      env->MakeWritable(&src);
      avs_composite::Mix({src->GetWritePtr() + 1, src->GetPitch(), 2},
                        {chroma->GetReadPtr() + 1, chroma->GetPitch(), 2},
                        {w / 2, h, 0, h}, 8, weight, env);
    }
    else {  // Planar YUV
      env->MakeWritable(&src);
      src->GetWritePtr(PLANAR_Y); //Must be requested

      BYTE* srcpU = (BYTE*)src->GetWritePtr(PLANAR_U);
      BYTE* chromapU = (BYTE*)chroma->GetReadPtr(PLANAR_U);
      BYTE* srcpV = (BYTE*)src->GetWritePtr(PLANAR_V);
      BYTE* chromapV = (BYTE*)chroma->GetReadPtr(PLANAR_V);
      int src_pitch_uv = src->GetPitch(PLANAR_U);
      int chroma_pitch_uv = chroma->GetPitch(PLANAR_U);
      int src_rowsize_u = src->GetRowSize(PLANAR_U);
      int src_rowsize_v = src->GetRowSize(PLANAR_V);
      int src_height_uv = src->GetHeight(PLANAR_U);

      merge_plane(srcpU, chromapU, src_pitch_uv, chroma_pitch_uv, src_rowsize_u, src_height_uv, weight, pixelsize, bits_per_pixel, env);
      merge_plane(srcpV, chromapV, src_pitch_uv, chroma_pitch_uv, src_rowsize_v, src_height_uv, weight, pixelsize, bits_per_pixel, env);

      if (vi.IsYUVA())
        merge_plane(src->GetWritePtr(PLANAR_A), chroma->GetReadPtr(PLANAR_A), src->GetPitch(PLANAR_A), chroma->GetPitch(PLANAR_A),
          src->GetRowSize(PLANAR_A), src->GetHeight(PLANAR_A), weight, pixelsize, bits_per_pixel, env);
    }
  }
  else { // weight == 1.0
    if (vi.IsYUY2()) {
      env->MakeWritable(&chroma);
      avs_composite::Mix({chroma->GetWritePtr(), chroma->GetPitch(), 2},
                        {src->GetReadPtr(), src->GetPitch(), 2}, {w / 2, h, 0, h}, 8, 1.0, env);
      return chroma;
    }
    else {
      if (src->IsWritable()) {
        src->GetWritePtr(PLANAR_Y); //Must be requested
        env->BitBlt(src->GetWritePtr(PLANAR_U), src->GetPitch(PLANAR_U), chroma->GetReadPtr(PLANAR_U), chroma->GetPitch(PLANAR_U), chroma->GetRowSize(PLANAR_U), chroma->GetHeight(PLANAR_U));
        env->BitBlt(src->GetWritePtr(PLANAR_V), src->GetPitch(PLANAR_V), chroma->GetReadPtr(PLANAR_V), chroma->GetPitch(PLANAR_V), chroma->GetRowSize(PLANAR_V), chroma->GetHeight(PLANAR_V));
        if (vi.IsYUVA())
          env->BitBlt(src->GetWritePtr(PLANAR_A), src->GetPitch(PLANAR_A), chroma->GetReadPtr(PLANAR_A), chroma->GetPitch(PLANAR_A), chroma->GetRowSize(PLANAR_A), chroma->GetHeight(PLANAR_A));
      }
      else { // avoid the cost of 2 chroma blits
        PVideoFrame dst = env->NewVideoFrameP(vi, &src);

        env->BitBlt(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y), src->GetReadPtr(PLANAR_Y), src->GetPitch(PLANAR_Y), src->GetRowSize(PLANAR_Y), src->GetHeight(PLANAR_Y));
        env->BitBlt(dst->GetWritePtr(PLANAR_U), dst->GetPitch(PLANAR_U), chroma->GetReadPtr(PLANAR_U), chroma->GetPitch(PLANAR_U), chroma->GetRowSize(PLANAR_U), chroma->GetHeight(PLANAR_U));
        env->BitBlt(dst->GetWritePtr(PLANAR_V), dst->GetPitch(PLANAR_V), chroma->GetReadPtr(PLANAR_V), chroma->GetPitch(PLANAR_V), chroma->GetRowSize(PLANAR_V), chroma->GetHeight(PLANAR_V));
        if (vi.IsYUVA())
          env->BitBlt(dst->GetWritePtr(PLANAR_A), dst->GetPitch(PLANAR_A), chroma->GetReadPtr(PLANAR_A), chroma->GetPitch(PLANAR_A), chroma->GetRowSize(PLANAR_A), chroma->GetHeight(PLANAR_A));

        return dst;
      }
    }
  }
  return src;
}


AVSValue __cdecl MergeChroma::Create(AVSValue args, void* , IScriptEnvironment* env)
{
  return new MergeChroma(args[0].AsClip(), args[1].AsClip(), (float)args[2].AsFloat(1.0f), env);
}


/**************************
******   Merge Luma   *****
**************************/


MergeLuma::MergeLuma(PClip _child, PClip _clip, float _weight, IScriptEnvironment* env)
  : GenericVideoFilter(_child), clip(_clip), weight(_weight)
{
  if (std::isnan(_weight))
    env->ThrowError("MergeLuma: weight cannot be NaN");

  const VideoInfo& vi2 = clip->GetVideoInfo();

  if (!(vi.IsYUV() || vi.IsYUVA()) || !(vi2.IsYUV() || vi2.IsYUVA()))
    env->ThrowError("MergeLuma: YUV data only (no RGB); use ConvertToYUY2, ConvertToYV12/16/24 or ConvertToYUVxxx");

  pixelsize = vi.ComponentSize();
  bits_per_pixel = vi.BitsPerComponent();

  if (!vi.IsSameColorspace(vi2)) {  // Since this is luma we allow all planar formats to be merged.
    if (!(vi.IsPlanar() && vi2.IsPlanar())) {
      env->ThrowError("MergeLuma: YUV data is not same type. YUY2 and planar images doesn't mix.");
    }
    if (pixelsize != vi2.ComponentSize()) {
      env->ThrowError("MergeLuma: YUV data bit depth is not same.");
    }
  }

  if (vi.width!=vi2.width || vi.height!=vi2.height)
    env->ThrowError("MergeLuma: Images must have same width and height!");

  if (weight<0.0f) weight=0.0f;
  if (weight>1.0f) weight=1.0f;

}


PVideoFrame __stdcall MergeLuma::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);

  if (weight < 0.0039f) return src;

  PVideoFrame luma = clip->GetFrame(n, env);

  if (vi.IsYUY2()) {
    env->MakeWritable(&src);
    avs_composite::Mix({src->GetWritePtr(), src->GetPitch(), 2},
                      {luma->GetReadPtr(), luma->GetPitch(), 2},
                      {vi.width, vi.height, 0, vi.height}, 8, weight < 0.9961f ? weight : 1.0, env);
    return src;
  }  // Planar
  if (weight > 0.9961f) {
    // 2nd clip weight is almost 100%: no merge, just copy
    const VideoInfo& vi2 = clip->GetVideoInfo();
    if (luma->IsWritable() && vi.IsSameColorspace(vi2)) {
      if (luma->GetRowSize(PLANAR_U)) {
        luma->GetWritePtr(PLANAR_Y); //Must be requested BUT only if we actually do something
        env->BitBlt(luma->GetWritePtr(PLANAR_U), luma->GetPitch(PLANAR_U), src->GetReadPtr(PLANAR_U), src->GetPitch(PLANAR_U), src->GetRowSize(PLANAR_U), src->GetHeight(PLANAR_U));
        env->BitBlt(luma->GetWritePtr(PLANAR_V), luma->GetPitch(PLANAR_V), src->GetReadPtr(PLANAR_V), src->GetPitch(PLANAR_V), src->GetRowSize(PLANAR_V), src->GetHeight(PLANAR_V));
      }
      if (luma->GetPitch(PLANAR_A)) // copy Alpha if exists
        env->BitBlt(luma->GetWritePtr(PLANAR_A), luma->GetPitch(PLANAR_A), src->GetReadPtr(PLANAR_A), src->GetPitch(PLANAR_A), src->GetRowSize(PLANAR_A), src->GetHeight(PLANAR_A));

      return luma;
    }
    else { // avoid the cost of 2 chroma blits
      PVideoFrame dst = env->NewVideoFrameP(vi, &luma);

      env->BitBlt(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y), luma->GetReadPtr(PLANAR_Y), luma->GetPitch(PLANAR_Y), luma->GetRowSize(PLANAR_Y), luma->GetHeight(PLANAR_Y));
      if (src->GetRowSize(PLANAR_U) && dst->GetRowSize(PLANAR_U)) {
        env->BitBlt(dst->GetWritePtr(PLANAR_U), dst->GetPitch(PLANAR_U), src->GetReadPtr(PLANAR_U), src->GetPitch(PLANAR_U), src->GetRowSize(PLANAR_U), src->GetHeight(PLANAR_U));
        env->BitBlt(dst->GetWritePtr(PLANAR_V), dst->GetPitch(PLANAR_V), src->GetReadPtr(PLANAR_V), src->GetPitch(PLANAR_V), src->GetRowSize(PLANAR_V), src->GetHeight(PLANAR_V));
      }
      if (dst->GetPitch(PLANAR_A) && src->GetPitch(PLANAR_A)) // copy Alpha if in both clip exists
        env->BitBlt(dst->GetWritePtr(PLANAR_A), dst->GetPitch(PLANAR_A), src->GetReadPtr(PLANAR_A), src->GetPitch(PLANAR_A), src->GetRowSize(PLANAR_A), src->GetHeight(PLANAR_A));

      return dst;
    }
  }
  else { // weight <= 0.9961f
    env->MakeWritable(&src);
    BYTE* srcpY = (BYTE*)src->GetWritePtr(PLANAR_Y);
    BYTE* lumapY = (BYTE*)luma->GetReadPtr(PLANAR_Y);
    int src_pitch = src->GetPitch(PLANAR_Y);
    int luma_pitch = luma->GetPitch(PLANAR_Y);
    int src_rowsize = src->GetRowSize(PLANAR_Y);
    int src_height = src->GetHeight(PLANAR_Y);

    merge_plane(srcpY, lumapY, src_pitch, luma_pitch, src_rowsize, src_height, weight, pixelsize, bits_per_pixel, env);
  }

  return src;
}


AVSValue __cdecl MergeLuma::Create(AVSValue args, void* , IScriptEnvironment* env)
{
  return new MergeLuma(args[0].AsClip(), args[1].AsClip(), (float)args[2].AsFloat(1.0f), env);
}


/*************************
******   Merge All   *****
*************************/


MergeAll::MergeAll(PClip _child, PClip _clip, float _weight, IScriptEnvironment* env)
  : GenericVideoFilter(_child), clip(_clip), weight(_weight)
{
  if (std::isnan(_weight))
    env->ThrowError("Merge: weight cannot be NaN");

  const VideoInfo& vi2 = clip->GetVideoInfo();

  if (!vi.IsSameColorspace(vi2))
    env->ThrowError("Merge: Pixel types are not the same. Both must be the same.");

  if (vi.width!=vi2.width || vi.height!=vi2.height)
    env->ThrowError("Merge: Images must have same width and height!");

  pixelsize = vi.ComponentSize();
  bits_per_pixel = vi.BitsPerComponent();

  if (weight<0.0f) weight=0.0f;
  if (weight>1.0f) weight=1.0f;
}


PVideoFrame __stdcall MergeAll::GetFrame(int n, IScriptEnvironment* env)
{
  if (weight<0.0039f) return child->GetFrame(n, env);
  if (weight>0.9961f) return clip->GetFrame(n, env);

  PVideoFrame src  = child->GetFrame(n, env);
  PVideoFrame src2 =  clip->GetFrame(n, env);

  env->MakeWritable(&src);
  BYTE* srcp  = src->GetWritePtr();
  const BYTE* srcp2 = src2->GetReadPtr();

  const int src_pitch = src->GetPitch();
  const int src_rowsize = src->GetRowSize();

  merge_plane(srcp, srcp2, src_pitch, src2->GetPitch(), src_rowsize, src->GetHeight(), weight, pixelsize, bits_per_pixel, env);

  if (vi.IsPlanar()) {
    const int planesYUV[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A};
    const int planesRGB[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A};
    const int *planes = (vi.IsYUV() || vi.IsYUVA()) ? planesYUV : planesRGB;
    // first plane is already processed
    for (int p = 1; p < vi.NumComponents(); p++) {
      const int plane = planes[p];
      merge_plane(src->GetWritePtr(plane), src2->GetReadPtr(plane), src->GetPitch(plane), src2->GetPitch(plane), src->GetRowSize(plane), src->GetHeight(plane), weight, pixelsize, bits_per_pixel, env);
    }
  }

  return src;
}


AVSValue __cdecl MergeAll::Create(AVSValue args, void* , IScriptEnvironment* env)
{
  return new MergeAll(args[0].AsClip(), args[1].AsClip(), (float)args[2].AsFloat(0.5f), env);
}
