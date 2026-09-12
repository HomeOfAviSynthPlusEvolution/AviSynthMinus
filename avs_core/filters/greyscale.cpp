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


#include "greyscale.h"
#include "video_convert/layout.h"
#include "avs_simd/target_policy.h"
#include "../core/internal.h"
#include <avs/alignment.h>
#include <avs/minmax.h>

#ifdef AVS_WINDOWS
    #include <avs/win.h>
#else
    #include <avs/posix.h>
#endif

#include <stdint.h>
#include "../convert/convert_planar.h"
#include "../convert/convert.h"
#include "../convert/convert_helper.h"


/*************************************
 *******   Convert to Greyscale ******
 ************************************/

extern const AVSFunction Greyscale_filters[] = {
  { "Greyscale", BUILTIN_FUNC_PREFIX, "c[matrix]s", Greyscale::Create },       // matrix can be "rec601", "rec709" or "Average" or "rec2020"
  { "Grayscale", BUILTIN_FUNC_PREFIX, "c[matrix]s", Greyscale::Create },
  { 0 }
};

Greyscale::Greyscale(PClip _child, const char* matrix_name, IScriptEnvironment* env)
 : GenericVideoFilter(_child)
{
  if (matrix_name && !vi.IsRGB())
    env->ThrowError("GreyScale: invalid \"matrix\" parameter (RGB data only)");

  // originally there was no PC range here
  pixelsize = vi.ComponentSize();
  bits_per_pixel = vi.BitsPerComponent();
  if (vi.IsYUY2()) {
    layout = vc_get_layout_functions(avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_layout_supported_targets()));
    if (!layout)
      env->ThrowError("Greyscale: Could not select VideoConvert layout kernels.");
  }

  // Preserve the input range unless the matrix argument selects another range.

  if (vi.IsRGB()) {
    auto frame0 = _child->GetFrame(0, env);
    const AVSMap* props = env->getFramePropsRO(frame0);
    // input _ColorRange frame property can appear for RGB source (studio range limited rgb)
    matrix_parse_merge_with_props(true /*in rgb*/, true /*out rgb*/, matrix_name, props, theMatrix, theColorRange, theOutColorRange, env);
    /*if (theColorRange == ColorRange_e::AVS_RANGE_FULL && theMatrix != Matrix_e::AVS_MATRIX_AVERAGE)
      env->ThrowError("GreyScale: only limited range matrix definition or \"Average\" is allowed.");
    */

    const int shift = 15; // fixed-point coefficient precision

    // input _ColorRange frame property can appear for RGB source (studio range limited rgb)
    double kr, kb;
    if (!GetKrKb(theMatrix, kr, kb))
      env->ThrowError("GreyScale: Unknown matrix.");
    matrix_plan = std::make_unique<avs_video_convert::MatrixPlan>(kr, kb, bits_per_pixel, shift,
      theColorRange == AVS_RANGE_FULL, theOutColorRange == AVS_RANGE_FULL, true, env, true);
  }
  // RGB matrix identity is preserved; RGB output range follows the numeric plan.
}

PVideoFrame Greyscale::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame frame = child->GetFrame(n, env);
  if (vi.NumComponents() == 1)
    return frame;

  env->MakeWritable(&frame);
  BYTE* srcp = frame->GetWritePtr();
  int pitch = frame->GetPitch();
  int height = vi.height;
  int width = vi.width;

  // RGB matrix identity is preserved; RGB output range follows the numeric plan.

  if (vi.IsPlanar() && (vi.IsYUV() || vi.IsYUVA())) {
    // planar YUV, set UV plane to neutral
    BYTE* dstp_u = frame->GetWritePtr(PLANAR_U);
    BYTE* dstp_v = frame->GetWritePtr(PLANAR_V);
    const int height = frame->GetHeight(PLANAR_U);
    const int rowsizeUV = frame->GetRowSize(PLANAR_U);
    const int dst_pitch = frame->GetPitch(PLANAR_U);
    switch (vi.ComponentSize())
    {
    case 1:
      fill_chroma<BYTE>(dstp_u, dstp_v, height, rowsizeUV, dst_pitch, 0x80);
      break;
    case 2:
      fill_chroma<uint16_t>(dstp_u, dstp_v, height, rowsizeUV, dst_pitch, 1 << (vi.BitsPerComponent() - 1));
      break;
    case 4:
      const float shift = 0.0f;
      fill_chroma<float>(dstp_u, dstp_v, height, rowsizeUV, dst_pitch, shift);
      break;
    }
    return frame;
  }

  if (vi.IsYUY2()) {
    if (layout->neutralize_yuy2_chroma({srcp, pitch}, {width, height, 0, height}) != VC_OK)
      env->ThrowError("Greyscale: VideoConvert chroma neutralization failed.");
    return frame;
  }
  if (vi.IsRGB()) {
    matrix_plan->ApplyGreyscale(frame, vi, env);
    update_ColorRange(env->getFramePropsRW(frame), theOutColorRange, env);
  }
  return frame;
}


AVSValue __cdecl Greyscale::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  PClip clip = args[0].AsClip();
  const VideoInfo& vi = clip->GetVideoInfo();

  if (vi.NumComponents() == 1)
    return clip;

  return new Greyscale(clip, args[1].AsString(0), env);
}
