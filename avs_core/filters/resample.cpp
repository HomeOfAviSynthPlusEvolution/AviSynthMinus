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

#include "resample.h"
#include "avs_video_convert/resize.h"
#include "../convert/convert_planar.h"
#include "../convert/convert_helper.h"
#include <algorithm>
#include <cassert>
#include <cmath>

/********************************************************************
***** Declare index of new filters for Avisynth's filter engine *****
********************************************************************/

extern const AVSFunction Resample_filters[] = {
  { "PointResize",    BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[force]i[keep_center]b[placement]s", FilteredResize::Create_PointResize },
  { "BilinearResize", BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[force]i[keep_center]b[placement]s", FilteredResize::Create_BilinearResize },
  { "BicubicResize",  BUILTIN_FUNC_PREFIX, "cii[b]f[c]f[src_left]f[src_top]f[src_width]f[src_height]f[force]i[keep_center]b[placement]s", FilteredResize::Create_BicubicResize },
  { "LanczosResize",  BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[taps]i[force]i[keep_center]b[placement]s", FilteredResize::Create_LanczosResize},
  { "Lanczos4Resize", BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[force]i[keep_center]b[placement]s", FilteredResize::Create_Lanczos4Resize},
  { "BlackmanResize", BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[taps]i[force]i[keep_center]b[placement]s", FilteredResize::Create_BlackmanResize},
  { "Spline16Resize", BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[force]i[keep_center]b[placement]s", FilteredResize::Create_Spline16Resize},
  { "Spline36Resize", BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[force]i[keep_center]b[placement]s", FilteredResize::Create_Spline36Resize},
  { "Spline64Resize", BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[force]i[keep_center]b[placement]s", FilteredResize::Create_Spline64Resize},
  { "GaussResize",    BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[p]f[b]f[s]f[force]i[keep_center]b[placement]s", FilteredResize::Create_GaussianResize},
  { "SincResize",     BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[taps]i[force]i[keep_center]b[placement]s", FilteredResize::Create_SincResize},
  { "SinPowerResize", BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[p]f[force]i[keep_center]b[placement]s", FilteredResize::Create_SinPowerResize},
  { "SincLin2Resize", BUILTIN_FUNC_PREFIX, "cii[src_left]f[src_top]f[src_width]f[src_height]f[taps]i[force]i[keep_center]b[placement]s", FilteredResize::Create_SincLin2Resize},
  { "UserDefined2Resize", BUILTIN_FUNC_PREFIX, "cii[b]f[c]f[s]f[src_left]f[src_top]f[src_width]f[src_height]f[force]i[keep_center]b[placement]s", FilteredResize::Create_UserDefined2Resize},
  /**
    * Resize(PClip clip, dst_width, dst_height [src_left, src_top, src_width, int src_height,] )
    *
    * src_left et al.   =  when these optional arguments are given, the filter acts just like
    *                      a Crop was performed with those parameters before resizing, only faster
   **/

  { 0 }
};

// Borrowed from fmtconv
// ChromaPlacement.cpp
// Author : Laurent de Soras, 2015

// Fixes the vertical chroma placement when the picture is interlaced.
// ofs = ordinate to skip between TFF and BFF, relative to the chroma grid. A
// single line of full-res picture is 0.25.
static inline void ChromaPlacement_fix_itl(double& cp_v, bool interlaced_flag, bool top_flag, double ofs = 0.5)
{
  assert(cp_v >= 0);

  if (interlaced_flag)
  {
    cp_v *= 0.5;
    if (!top_flag)
    {
      cp_v += ofs;
    }
  }
}
/*
ss_h and ss_v are log2(subsampling)
rgb_flag actually means that chroma subsampling doesn't apply.

http://www.mir.com/DMG/chroma.html

cp_* is the position of the sampling point relative to the frame
top/left border, in the plane coordinates. For reference, the border
of the frame is at 0.5 units of luma from the first luma sampling point.
I. e., the luma sampling point is at the pixel's center.
*/

// PF added BOTTOM, BOTTOM_LEFT, TOP
// Pass ChromaLocation_e::AVS_CHROMA_UNUSED for defaults
// plane index 0:Y, 1:U, 2:V
// cplace is a ChromaLocation_e constant
static void ChromaPlacement_compute_cplace(double& cp_h, double& cp_v, int cplace, int plane_index, int ss_h, int ss_v, bool rgb_flag, bool interlaced_flag, bool top_flag)
{
  assert(cplace >= 0 || cplace == ChromaLocation_e::AVS_CHROMA_UNUSED);
  assert(cplace < ChromaLocation_e::AVS_CHROMA_DV);
  assert(ss_h >= 0);
  assert(ss_v >= 0);
  assert(plane_index >= 0);

  // Generic case for luma, non-subsampled chroma and center (MPEG-1) chroma.
  cp_h = 0.5;
  cp_v = 0.5;
  ChromaPlacement_fix_itl(cp_v, interlaced_flag, top_flag);

  // Subsampled chroma
  if (!rgb_flag && plane_index > 0)
  {
    if (ss_h > 0) // horizontal subsampling 420 411
    {
      if (cplace == ChromaLocation_e::AVS_CHROMA_LEFT // mpeg2
        || cplace == ChromaLocation_e::AVS_CHROMA_DV
        || cplace == ChromaLocation_e::AVS_CHROMA_TOP_LEFT
        || cplace == ChromaLocation_e::AVS_CHROMA_BOTTOM_LEFT
        )
      {
        cp_h = 0.5 / (1 << ss_h);
      }
    }

    if (ss_v == 1) // vertical subsampling 420, 422
    {
      if (cplace == ChromaLocation_e::AVS_CHROMA_LEFT)
      {
        cp_v = 0.5;
        ChromaPlacement_fix_itl(cp_v, interlaced_flag, top_flag);
      }
      else if (cplace == ChromaLocation_e::AVS_CHROMA_DV
        || cplace == ChromaLocation_e::AVS_CHROMA_TOP_LEFT
        || cplace == ChromaLocation_e::AVS_CHROMA_TOP
        )
      {
        cp_v = 0.25;
        ChromaPlacement_fix_itl(cp_v, interlaced_flag, top_flag, 0.25);

        if (cplace == ChromaLocation_e::AVS_CHROMA_DV && plane_index == 2) // V
        {
          cp_v += 0.5;
        }
      }
      else if (cplace == ChromaLocation_e::AVS_CHROMA_BOTTOM_LEFT
        || cplace == ChromaLocation_e::AVS_CHROMA_BOTTOM
        )
      {
        cp_v = 0.75;
        ChromaPlacement_fix_itl(cp_v, interlaced_flag, top_flag, 0.25);
      }
    }  // ss_v == 1
  }
}


// returns the requested horizontal or vertical pixel center position
static void GetCenterShiftForResizers(double& center_pos_luma, double& center_pos_chroma, bool preserve_center, int chroma_placement, VideoInfo &vi, bool for_horizontal) {
  double center_pos_h_luma = 0.0;
  double center_pos_v_luma = 0.0;
  // if not needed, these won't be used
  double center_pos_h_chroma = 0.0;
  double center_pos_v_chroma = 0.0;

  // chroma, only if applicable
  if (vi.IsPlanar() && vi.NumComponents() > 1 && !vi.IsRGB()) {
    double cp_s_h = 0;
    double cp_s_v = 0;

    if (preserve_center) {
      // same for source and destination
      int plane_index = 1; // U
      int src_ss_h = vi.GetPlaneWidthSubsampling(PLANAR_U);
      int src_ss_v = vi.GetPlaneHeightSubsampling(PLANAR_U);

      int chromaplace = ChromaLocation_e::AVS_CHROMA_CENTER; // MPEG1

      ChromaPlacement_compute_cplace(
        cp_s_h, cp_s_v, chroma_placement, plane_index, src_ss_h, src_ss_v,
        vi.IsRGB(),
        false, // interlacing flag, we don't handle it here
        false  // top_flag, we don't handle it here
      );
    }

    center_pos_h_chroma = cp_s_h;
    center_pos_v_chroma = cp_s_v;
  }

  // luma/rgb planes
  if (preserve_center) {
    center_pos_h_luma = 0.5;
    center_pos_v_luma = 0.5;
  }
  else {
    center_pos_h_luma = 0.0;
    center_pos_v_luma = 0.0;
  }

  // fill return ref values
  if (for_horizontal) {
    center_pos_luma = center_pos_h_luma;
    center_pos_chroma = center_pos_h_chroma;
  }
  else {
    // vertical
    center_pos_luma = center_pos_v_luma;
    center_pos_chroma = center_pos_v_chroma;
  }

}

PClip FilteredResize::CreateResizeH(PClip clip, double subrange_left, double subrange_width, int target_width, bool force,
  const vc_filter_spec& func, bool preserve_center, int chroma_placement, IScriptEnvironment* env)
{
  const VideoInfo& vi = clip->GetVideoInfo();
  if (!force && subrange_left == 0 && subrange_width == target_width && subrange_width == vi.width) {
    return clip;
  }
  /*
  // intentionally left here: don't use crop at special edge cases to avoid inconsistent results across params/color spaces
  if (subrange_left == int(subrange_left) && subrange_width == target_width
   && subrange_left >= 0 && subrange_left + subrange_width <= vi.width) {
    const int mask = ((vi.IsYUV() || vi.IsYUVA()) && !vi.IsY()) ? (1 << vi.GetPlaneWidthSubsampling(PLANAR_U)) - 1 : 0;

    if (((int(subrange_left) | int(subrange_width)) & mask) == 0)
      return new Crop(int(subrange_left), 0, int(subrange_width), vi.height, 0, clip, env);
  }
  */
  double luma_center, chroma_center;
  VideoInfo source_vi = vi;
  // YUY2 uses the same chroma grid as the former planar YV16 wrappers.
  if (vi.IsYUY2())
    source_vi.pixel_type = VideoInfo::CS_YV16;
  GetCenterShiftForResizers(luma_center, chroma_center, preserve_center, chroma_placement, source_vi, true);
  return avs_video_convert::CreateResizeAxis(clip, VC_HORIZONTAL, subrange_left, subrange_width, target_width,
                                             func, luma_center, chroma_center, env);
}


PClip FilteredResize::CreateResizeV(PClip clip, double subrange_top, double subrange_height, int target_height, bool force,
  const vc_filter_spec& func, bool preserve_center, int chroma_placement, IScriptEnvironment* env)
{
  const VideoInfo& vi = clip->GetVideoInfo();
  if (!force && subrange_top == 0 && subrange_height == target_height && subrange_height == vi.height) {
    return clip;
  }
  /*
  // intentionally left here: don't use crop at special edge cases to avoid inconsistent results across params/color spaces
  if (subrange_top == int(subrange_top) && subrange_height == target_height
   && subrange_top >= 0 && subrange_top + subrange_height <= vi.height) {
    const int mask = ((vi.IsYUV() || vi.IsYUVA()) && !vi.IsY()) ? (1 << vi.GetPlaneHeightSubsampling(PLANAR_U)) - 1 : 0;

    if (((int(subrange_top) | int(subrange_height)) & mask) == 0)
      return new Crop(0, int(subrange_top), vi.width, int(subrange_height), 0, clip, env);
  }
  */
  double luma_center, chroma_center;
  VideoInfo source_vi = vi;
  GetCenterShiftForResizers(luma_center, chroma_center, preserve_center, chroma_placement, source_vi, false);
  return avs_video_convert::CreateResizeAxis(clip, VC_VERTICAL, subrange_top, subrange_height, target_height,
                                             func, luma_center, chroma_center, env);
}


PClip FilteredResize::CreateResize(PClip clip, int target_width, int target_height, const AVSValue* args, int force,
  const vc_filter_spec& f,
  bool preserve_center, const char* placement_name, const int forced_chroma_placement,
  IScriptEnvironment* env)
{
  // args 0-1-2-3: left-top-width-height
  VideoInfo vi = clip->GetVideoInfo();
  const double subrange_left = args[0].AsFloat(0), subrange_top = args[1].AsFloat(0);

  double subrange_width = args[2].AsDblDef(vi.width), subrange_height = args[3].AsDblDef(vi.height);

  if (std::isnan(subrange_left) || std::isnan(subrange_top) || std::isnan(subrange_width) || std::isnan(subrange_height))
    env->ThrowError("Resize: crop arguments cannot be NaN");
  // Crop style syntax
  if (subrange_width <= 0.0) subrange_width = vi.width - subrange_left + subrange_width;
  if (subrange_height <= 0.0) subrange_height = vi.height - subrange_top + subrange_height;

  PClip result;
  // ensure that the intermediate area is maximal

  const double area_FirstH = subrange_height * target_width;
  const double area_FirstV = subrange_width * target_height;

  // "minimal area" logic is not necessarily faster because H and V resizers are not the same speed.
  // so we keep the traditional max area logic, which is for quality

  // use forced_chroma_placement >= 0 and placement_name == nullptr together
  int chroma_placement = forced_chroma_placement >= 0 ? forced_chroma_placement : ChromaLocation_e::AVS_CHROMA_UNUSED;
  if (placement_name) {
    // no format-oriented defaults
    if (vi.IsYV411() || vi.Is420() || vi.Is422()) {
      // placement explicite parameter like in ConvertToXXX or Text
      // input frame properties, if "auto"
      // When called from ConvertToXXX, chroma is not involved.
      auto frame0 = clip->GetFrame(0, env);
      const AVSMap* props = env->getFramePropsRO(frame0);
      chromaloc_parse_merge_with_props(vi, placement_name, props, /* ref*/chroma_placement, ChromaLocation_e::AVS_CHROMA_UNUSED /*default*/, env);
    }

  }

  // 0 - return unchanged if no resize needed
  // 1 - force H
  // 2 - force V
  // 3 - force H and V
  const bool force_H = force == 1 || force == 3;
  const bool force_V = force == 2 || force == 3;
  if (area_FirstH < area_FirstV)
  {
    result = CreateResizeV(clip, subrange_top, subrange_height, target_height, force_V, f, preserve_center, chroma_placement, env);
    result = CreateResizeH(result, subrange_left, subrange_width, target_width, force_H, f, preserve_center, chroma_placement, env);
  }
  else
  {
    result = CreateResizeH(clip, subrange_left, subrange_width, target_width, force_H, f, preserve_center, chroma_placement, env);
    result = CreateResizeV(result, subrange_top, subrange_height, target_height, force_V, f, preserve_center, chroma_placement, env);
  }
  return result;
}

AVSValue __cdecl FilteredResize::Create_PointResize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_POINT, {}};
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}


AVSValue __cdecl FilteredResize::Create_BilinearResize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_TRIANGLE, {}};
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}


AVSValue __cdecl FilteredResize::Create_BicubicResize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_BICUBIC, {args[3].AsDblDef(1. / 3.), args[4].AsDblDef(1. / 3.)}};
  const int force = args[9].AsInt(0);

  bool preserve_center = args[10].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[11].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[5], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_LanczosResize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_LANCZOS, {double(std::clamp(args[7].AsInt(3), 1, 150))}};
  const int force = args[8].AsInt(0);

  bool preserve_center = args[9].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[10].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_Lanczos4Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_LANCZOS, {double(4)}};
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_BlackmanResize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_BLACKMAN, {double(std::clamp(args[7].AsInt(4), 1, 150))}};
  const int force = args[8].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_Spline16Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_SPLINE16, {}};
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_Spline36Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_SPLINE36, {}};
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_Spline64Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_SPLINE64, {}};
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_GaussianResize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_GAUSSIAN, {args[7].AsFloat(30.0f), args[8].AsFloat(2.0f), args[9].AsFloat(4.0f)}}; // defaults at two more places
  const int force = args[10].AsInt(0);

  bool preserve_center = args[11].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[12].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_SincResize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_SINC, {double(std::clamp(args[7].AsInt(4), 1, 150))}};
  const int force = args[8].AsInt(0);
 
  bool preserve_center = args[9].AsBool(true); // [keep_center] default Avisynth
  const char * placement_name = args[10].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

// like GaussianFilter(); optional P
AVSValue __cdecl FilteredResize::Create_SinPowerResize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_SINPOWER, {args[7].AsFloat(2.5f)}};
  const int force = args[8].AsInt(0);

  bool preserve_center = args[9].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[10].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

// like SincFilter or LanczosFilter: optional Taps
AVSValue __cdecl FilteredResize::Create_SincLin2Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_SINCLIN2, {double(std::clamp(args[7].AsInt(15), 1, 150))}};
  const int force = args[8].AsInt(0);

  bool preserve_center = args[9].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[10].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

// like bicubic, plus 's'upport: optional B and C and S
AVSValue __cdecl FilteredResize::Create_UserDefined2Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  const auto f = vc_filter_spec{VC_USER_DEFINED2, {args[3].AsFloat(121.0f), args[4].AsFloat(19.0f), args[5].AsFloat(2.3f)}};
  const int force = args[10].AsInt(0);

  bool preserve_center = args[11].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[12].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[6], force, f, preserve_center, placement_name, forced_chroma_placement, env);
}

