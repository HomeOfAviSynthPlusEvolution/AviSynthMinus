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

// ConvertPlanar (c) 2005 by Klaus Post


#include "video_convert/layout.h"
#include "avs_simd/target_policy.h"
#include "convert.h"
#include "convert_matrix.h"
#include "convert_helper.h"
#include "convert_planar.h"
#include "convert_bits.h"
#include "../filters/resample.h"
#include "../filters/planeswap.h"
#include "../filters/field.h"

#ifdef AVS_WINDOWS
    #include <avs/win.h>
#else
    #include <avs/posix.h>
#endif

#include <avs/alignment.h>
#include <algorithm>
#include <string>

template <typename pixel_t>
void fill_chroma(uint8_t * dstp_u, uint8_t * dstp_v, int height, int row_size, int pitch, pixel_t val)
{
  if (pitch == row_size) {
    size_t size = height * pitch / sizeof(pixel_t);
    std::fill_n(reinterpret_cast<pixel_t*>(dstp_u), size, val);
    std::fill_n(reinterpret_cast<pixel_t*>(dstp_v), size, val);
  }
  else {
    size_t size = row_size / sizeof(pixel_t);
    for (int i = 0; i < height; i++) {
      std::fill_n(reinterpret_cast<pixel_t*>(dstp_u), size, val);
      std::fill_n(reinterpret_cast<pixel_t*>(dstp_v), size, val);
      dstp_u += pitch;
      dstp_v += pitch;
    }
  }
}

template <typename pixel_t>
void fill_plane(uint8_t * dstp, int height, int row_size, int pitch, pixel_t val)
{
  if (pitch == row_size) {
    size_t size = height * pitch / sizeof(pixel_t);
    std::fill_n(reinterpret_cast<pixel_t*>(dstp), size, val);
  }
  else {
    size_t size = row_size / sizeof(pixel_t);
    for (int i = 0; i < height; i++) {
      std::fill_n(reinterpret_cast<pixel_t*>(dstp), size, val);
      dstp += pitch;
    }
  }
}

// specialize it
template void fill_plane<uint8_t>(uint8_t * dstp, int height, int row_size, int pitch, uint8_t val);
template void fill_plane<uint16_t>(uint8_t * dstp, int height, int row_size, int pitch, uint16_t val);
template void fill_plane<float>(uint8_t * dstp, int height, int row_size, int pitch, float val);
template void fill_chroma<uint8_t>(uint8_t * dstp_u, uint8_t * dstp_v, int height, int row_size, int pitch, uint8_t val);
template void fill_chroma<uint16_t>(uint8_t * dstp_u, uint8_t * dstp_v, int height, int row_size, int pitch, uint16_t val);
template void fill_chroma<float>(uint8_t * dstp_u, uint8_t * dstp_v, int height, int row_size, int pitch, float val);

ConvertToY::ConvertToY(PClip src, const char *matrix_name, IScriptEnvironment* env) : GenericVideoFilter(src) {
  yuy2_input = blit_luma_only = packed_rgb_input = planar_rgb_input = false;

  int target_pixel_type;
  int bits_per_pixel = vi.BitsPerComponent();
  switch (bits_per_pixel)
  {
  case 8: target_pixel_type = VideoInfo::CS_Y8; break;
  case 10: target_pixel_type = VideoInfo::CS_Y10; break;
  case 12: target_pixel_type = VideoInfo::CS_Y12; break;
  case 14: target_pixel_type = VideoInfo::CS_Y14; break;
  case 16: target_pixel_type = VideoInfo::CS_Y16; break;
  case 32: target_pixel_type = VideoInfo::CS_Y32; break;
  default:
    env->ThrowError("ConvertToY does not support %d-bit formats.", vi.BitsPerComponent());
  }

  pixelsize = vi.ComponentSize();

  if (vi.IsPlanar() && (vi.IsYUV() || vi.IsYUVA())) { // not for Planar RGB
    blit_luma_only = true;
    vi.pixel_type = target_pixel_type;
    return;
  }

  if (vi.IsYUY2()) {
    yuy2_input = true;
    layout = vc_get_layout_functions(avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_layout_supported_targets()));
    if (!layout)
      env->ThrowError("ConvertToY: Could not select VideoConvert layout kernels.");
    vi.pixel_type = target_pixel_type;
    return;
  }

  if (vi.IsRGB()) { // also Planar RGB
    if (vi.IsPlanarRGB() || vi.IsPlanarRGBA())
      planar_rgb_input = true;
    else
      packed_rgb_input = true;
    pixel_step = vi.BytesFromPixels(1); // for packed RGB 3,4,6,8

    const int shift = 15; // internally 15 bits precision, still no overflow in calculations

    // input _ColorRange frame property can appear for RGB source (studio range limited rgb)
    auto frame0 = src->GetFrame(0, env);
    const AVSMap* props = env->getFramePropsRO(frame0);
    matrix_parse_merge_with_props(vi.IsRGB(), vi.IsRGB(), matrix_name, props, theMatrix, theColorRange, theOutColorRange, env);

    double kr, kb;
    if (!GetKrKb(theMatrix, kr, kb))
      env->ThrowError("ConvertToY: Unknown matrix.");
    matrix_plan = std::make_unique<avs_video_convert::MatrixPlan>(kr, kb, bits_per_pixel, shift,
      theColorRange == AVS_RANGE_FULL, theOutColorRange == AVS_RANGE_FULL, true, env, true);

    theColorRange = theOutColorRange; // final frame property

    vi.pixel_type = target_pixel_type;

    return;
  }

  env->ThrowError("ConvertToY: Unknown input format");
}


PVideoFrame __stdcall ConvertToY::GetFrame(int n, IScriptEnvironment* env) {
  PVideoFrame src = child->GetFrame(n, env);

  if (blit_luma_only) {
    // Abuse Subframe to snatch the Y plane
    PVideoFrame dst = env->Subframe(src, 0, src->GetPitch(PLANAR_Y), src->GetRowSize(PLANAR_Y), src->GetHeight(PLANAR_Y));
    // A grayscale frame has no chroma location. Subframe owns its property map;
    // deleting this property must not change the source frame's metadata.
    env->propDeleteKey(env->getFramePropsRW(dst), "_ChromaLocation");
    return dst;
  }

  const BYTE* srcp = src->GetReadPtr();
  const int src_pitch = src->GetPitch();

  PVideoFrame dst = env->NewVideoFrameP(vi, &src);
  env->propDeleteKey(env->getFramePropsRW(dst), "_ChromaLocation");
  BYTE* dstp = dst->GetWritePtr(PLANAR_Y);
  const int dst_pitch = dst->GetPitch(PLANAR_Y);
  int rowsize = dst->GetRowSize(PLANAR_Y);
  int width = rowsize / pixelsize;
  int height = dst->GetHeight(PLANAR_Y);

  if (packed_rgb_input || planar_rgb_input) {
    auto props = env->getFramePropsRW(dst);
    update_Matrix_and_ColorRange(props, theMatrix, theColorRange, env);
  }

  if (yuy2_input) {
    if (layout->extract_yuy2_luma({srcp, src_pitch}, {dstp, dst_pitch}, {width, height, 0, height}) != VC_OK)
      env->ThrowError("ConvertToY: VideoConvert luma extraction failed.");
    return dst;
  }

  if (packed_rgb_input)
    matrix_plan->ConvertPacked(src, dst, width, height, pixel_step / pixelsize, false, env);
  else
    matrix_plan->Convert(src, dst, width, height, env);
  return dst;
}

AVSValue __cdecl ConvertToY::Create(AVSValue args, void* user_data, IScriptEnvironment* env) {
  PClip clip = args[0].AsClip();
  bool only_8bit = reinterpret_cast<intptr_t>(user_data) == 0;
  if (only_8bit && clip->GetVideoInfo().BitsPerComponent() != 8)
    env->ThrowError("ConvertToY8: only 8 bit sources allowed");

  if (clip->GetVideoInfo().NumComponents() == 1)
    return clip;

  return new ConvertToY(clip, args[1].AsString(0), env);
}

/*****************************************************
 * ConvertRGBToYUV444
 ******************************************************/

ConvertRGBToYUV444::ConvertRGBToYUV444(PClip src, const char *matrix_name, bool keep_packedrgb_alpha, IScriptEnvironment* env)
  : GenericVideoFilter(src)
{
  if (!vi.IsRGB())
    env->ThrowError("ConvertRGBToYV24/YUV444: Only RGB data input accepted");

  // ChromaInPlacement parameter exists, (default none/-1) + input frame properties; 'left'-ish _ChromaLocation is allowed, checked later
  auto frame0 = src->GetFrame(0, env);
  const AVSMap* props = env->getFramePropsRO(frame0);

  // input _ColorRange frame property can appear for RGB source (studio range limited rgb)
  matrix_parse_merge_with_props(true /*in rgb*/, false /*out yuv*/, matrix_name, props, theMatrix, theColorRange, theOutColorRange, env);

  const int shift = 15; // internally 15 bits precision, still no overflow in calculations
  int bits_per_pixel = vi.BitsPerComponent();

  double kr, kb;
  if (!GetKrKb(theMatrix, kr, kb))
    env->ThrowError("ConvertRGBToYV24/YUV444: Unknown matrix.");
  {
    matrix_plan = std::make_unique<avs_video_convert::MatrixPlan>(kr, kb, bits_per_pixel, shift,
      theColorRange == AVS_RANGE_FULL, theOutColorRange == AVS_RANGE_FULL, true, env);
  }

  theColorRange = theOutColorRange; // final frame property

  isPlanarRGBfamily = vi.IsPlanarRGB() || vi.IsPlanarRGBA();
  hasAlpha = vi.IsPlanarRGBA() || (keep_packedrgb_alpha && (vi.IsRGB32() || vi.IsRGB64()));
  // for packed RGB it depends: ConvertToYUVAxxx() can force YUVA target option
  if (isPlanarRGBfamily)
  {
    pixel_step = hasAlpha ? -2 : -1;
    switch (vi.BitsPerComponent())
    {
    case 8: vi.pixel_type  = hasAlpha ? VideoInfo::CS_YUVA444    : VideoInfo::CS_YV24; break;
    case 10: vi.pixel_type = hasAlpha ? VideoInfo::CS_YUVA444P10 : VideoInfo::CS_YUV444P10; break;
    case 12: vi.pixel_type = hasAlpha ? VideoInfo::CS_YUVA444P12 : VideoInfo::CS_YUV444P12; break;
    case 14: vi.pixel_type = hasAlpha ? VideoInfo::CS_YUVA444P14 : VideoInfo::CS_YUV444P14; break;
    case 16: vi.pixel_type = hasAlpha ? VideoInfo::CS_YUVA444P16 : VideoInfo::CS_YUV444P16; break;
    case 32: vi.pixel_type = hasAlpha ? VideoInfo::CS_YUVA444PS  : VideoInfo::CS_YUV444PS; break;
    }
  } else { // packed RGB24/32/48/64
    // for compatibility reasons ConvertToYUVxxx target is not YUVA even 
    // if original has alpha, such as RGB32.
    // Unlike ConvertToYUVA which can force keeping RGB32's or RGB64's alpha
    pixel_step = vi.BytesFromPixels(1); // 3,4 for packed 8 bit, 6,8 for
    switch(vi.ComponentSize())
    {
    case 1: vi.pixel_type = hasAlpha ? VideoInfo::CS_YUVA444 : VideoInfo::CS_YV24; break;
    case 2: vi.pixel_type = hasAlpha ? VideoInfo::CS_YUVA444P16 : VideoInfo::CS_YUV444P16; break;
    }
  }

}



PVideoFrame __stdcall ConvertRGBToYUV444::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);

  auto props = env->getFramePropsRW(dst);
  update_Matrix_and_ColorRange(props, theMatrix, theColorRange, env);

  if (pixel_step > 0) {
    matrix_plan->ConvertPacked(src, dst, vi.width, vi.height, pixel_step / vi.ComponentSize(), hasAlpha, env);
    return dst;
  }

  // isPlanarRGBfamily
  if(hasAlpha) {
    // simple copy
    BYTE* dstA = dst->GetWritePtr(PLANAR_A);
    const int Apitch = dst->GetPitch(PLANAR_A);
    env->BitBlt(dstA, Apitch, src->GetReadPtr(PLANAR_A), src->GetPitch(PLANAR_A), src->GetRowSize(PLANAR_A_ALIGNED), src->GetHeight(PLANAR_A));
  }
  matrix_plan->Convert(src, dst, vi.width, vi.height, env);
  return dst;
}



/*****************************************************
 * ConvertYV24ToRGB
 *
 * (c) Klaus Post, 2005
 * Generic 4:4:4(:4), 16 bit and Planar RGB(A) support 2016 by PF
 ******************************************************/


ConvertYUV444ToRGB::ConvertYUV444ToRGB(PClip src, const char *matrix_name, int _pixel_step, IScriptEnvironment* env)
 : GenericVideoFilter(src), pixel_step(_pixel_step)
{

  if (!vi.Is444())
    env->ThrowError("ConvertYUV444ToRGB: Only 4:4:4 data input accepted");

  auto frame0 = child->GetFrame(0, env);
  const AVSMap* props = env->getFramePropsRO(frame0);
  matrix_parse_merge_with_props(false /*in yuv*/, true /*out rgb*/, matrix_name, props, theMatrix, theColorRange, theOutColorRange, env);

  const int shift = 13; // for integer arithmetic, over 13 bits would overflow the internal calculation
  const int bits_per_pixel = vi.BitsPerComponent();

  double kr, kb;
  if (!GetKrKb(theMatrix, kr, kb))
    env->ThrowError("ConvertYV24ToRGB: Unknown matrix.");
  {
    matrix_plan = std::make_unique<avs_video_convert::MatrixPlan>(kr, kb, bits_per_pixel, shift,
      theColorRange == AVS_RANGE_FULL, theOutColorRange == AVS_RANGE_FULL, false, env);
  }

  theOutMatrix = Matrix_e::AVS_MATRIX_RGB;
  //theOutColorRange = ColorRange_e::AVS_RANGE_FULL; // PC709 must keep the input one!

  switch (pixel_step)
  {
  case -1: case -2:
    switch (vi.BitsPerComponent())
    {
    case 8:  vi.pixel_type = pixel_step == -2 ? VideoInfo::CS_RGBAP : VideoInfo::CS_RGBP; break;
    case 10: vi.pixel_type = pixel_step == -2 ? VideoInfo::CS_RGBAP10 : VideoInfo::CS_RGBP10; break;
    case 12: vi.pixel_type = pixel_step == -2 ? VideoInfo::CS_RGBAP12 : VideoInfo::CS_RGBP12; break;
    case 14: vi.pixel_type = pixel_step == -2 ? VideoInfo::CS_RGBAP14 : VideoInfo::CS_RGBP14; break;
    case 16: vi.pixel_type = pixel_step == -2 ? VideoInfo::CS_RGBAP16 : VideoInfo::CS_RGBP16; break;
    case 32: vi.pixel_type = pixel_step == -2 ? VideoInfo::CS_RGBAPS : VideoInfo::CS_RGBPS; break;
    default:
      env->ThrowError("ConvertYUV444ToRGB: invalid vi.BitsPerComponent(): %d", vi.BitsPerComponent());
    }
    break;
  case 3: vi.pixel_type = VideoInfo::CS_BGR24; break;
  case 4: vi.pixel_type = VideoInfo::CS_BGR32; break;
  case 6: vi.pixel_type = VideoInfo::CS_BGR48; break;
  case 8: vi.pixel_type = VideoInfo::CS_BGR64; break;
  default:
    env->ThrowError("ConvertYUV444ToRGB: invalid pixel step: %d", pixel_step);
  }

}


PVideoFrame __stdcall ConvertYUV444ToRGB::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);

  auto props = env->getFramePropsRW(dst);
  update_Matrix_and_ColorRange(props, theOutMatrix, theOutColorRange, env);
  update_ChromaLocation(props, -1, env); // RGB target: delete _ChromaLocation

  if (pixel_step > 0) {
    matrix_plan->ConvertPacked(src, dst, vi.width, vi.height, pixel_step / vi.ComponentSize(),
      src->GetRowSize(PLANAR_A) != 0, env);
    return dst;
  }
  {
      // YUV444 -> PlanarRGB
      // YUVA444 -> PlanarRGBA
    bool targetHasAlpha = pixel_step == -2;

    // copy or fill alpha
    BYTE *dstpA;
    if (targetHasAlpha) {
      dstpA = dst->GetWritePtr(PLANAR_A);
      int heightA = dst->GetHeight(PLANAR_A);
      int rowsizeA = dst->GetRowSize(PLANAR_A);
      int dst_pitchA = dst->GetPitch(PLANAR_A);
        // simple copy
      if(src->GetRowSize(PLANAR_A)) // vi.IsYUVA() no-no! vi is already the target video type
        env->BitBlt(dstpA, dst_pitchA, src->GetReadPtr(PLANAR_A), src->GetPitch(PLANAR_A), src->GetRowSize(PLANAR_A_ALIGNED), src->GetHeight(PLANAR_A));
      else {
        // fill default transparency
        switch (vi.ComponentSize())
        {
        case 1:
          fill_plane<BYTE>(dstpA, heightA, rowsizeA, dst_pitchA, 255);
          break;
        case 2:
          fill_plane<uint16_t>(dstpA, heightA, rowsizeA, dst_pitchA, (1 << vi.BitsPerComponent()) - 1);
          break;
        case 4:
          fill_plane<float>(dstpA, heightA, rowsizeA, dst_pitchA, 1.0f);
          break;
        }
      }
    }

    matrix_plan->Convert(src, dst, vi.width, vi.height, env);
  }
  return dst;
}

/************************************
 * YUY2 to YV16
 ************************************/

ConvertYUY2ToYV16::ConvertYUY2ToYV16(PClip src, IScriptEnvironment* env) : GenericVideoFilter(src) {
  layout = vc_get_layout_functions(avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_layout_supported_targets()));
  if (!layout)
    env->ThrowError("YUY2: cannot select layout kernel");


  if (!vi.IsYUY2())
    env->ThrowError("ConvertYUY2ToYV16: Only YUY2 is allowed as input");

  vi.pixel_type = VideoInfo::CS_YV16;

}


PVideoFrame __stdcall ConvertYUY2ToYV16::GetFrame(int n, IScriptEnvironment* env) {
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);
  const vc_yuv_planes output{{dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y)},
    {dst->GetWritePtr(PLANAR_U), dst->GetPitch(PLANAR_U)}, {dst->GetWritePtr(PLANAR_V), dst->GetPitch(PLANAR_V)}};
  const int status = layout->unpack_yuy2({src->GetReadPtr(), src->GetPitch()}, output,
    {vi.width, vi.height, 0, vi.height});
  if (status != VC_OK)
    env->ThrowError("YUY2 unpack failed (%d)", status);
  return dst;
}

AVSValue __cdecl ConvertYUY2ToYV16::Create(AVSValue args, void*, IScriptEnvironment* env) {
  PClip clip = args[0].AsClip();
  if (clip->GetVideoInfo().IsYV16())
    return clip;
  return new ConvertYUY2ToYV16(clip, env);
}

/************************************
 * YV16 to YUY2
 ************************************/

ConvertYV16ToYUY2::ConvertYV16ToYUY2(PClip src, IScriptEnvironment* env) : GenericVideoFilter(src) {
  layout = vc_get_layout_functions(avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_layout_supported_targets()));
  if (!layout)
    env->ThrowError("YUY2: cannot select layout kernel");


  if (!vi.IsYV16())
    env->ThrowError("ConvertYV16ToYUY2: Only YV16 is allowed as input");

  vi.pixel_type = VideoInfo::CS_YUY2;
}

PVideoFrame __stdcall ConvertYV16ToYUY2::GetFrame(int n, IScriptEnvironment* env) {
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);
  const vc_const_yuv_planes input{{src->GetReadPtr(PLANAR_Y), src->GetPitch(PLANAR_Y)},
    {src->GetReadPtr(PLANAR_U), src->GetPitch(PLANAR_U)}, {src->GetReadPtr(PLANAR_V), src->GetPitch(PLANAR_V)}};
  const int status = layout->pack_yuy2(input, {dst->GetWritePtr(), dst->GetPitch()},
    {vi.width, vi.height, 0, vi.height});
  if (status != VC_OK)
    env->ThrowError("YUY2 pack failed (%d)", status);
  return dst;
}

AVSValue __cdecl ConvertYV16ToYUY2::Create(AVSValue args, void*, IScriptEnvironment* env) {
  PClip clip = args[0].AsClip();
  if (clip->GetVideoInfo().IsYUY2())
    return clip;
  return new ConvertYV16ToYUY2(clip, env);
}

/**********************************************
 * Converter between arbitrary planar formats
 *
 * This uses plane copy for luma, and a custom
 * resizer for chroma
 *
 * (c) Klaus Post, 2005
 * (c) Ian Brabham, 2011
 **********************************************/

ConvertToPlanarGeneric::ConvertToPlanarGeneric(
  PClip src, int dst_space, bool interlaced,
  int _ChromaLocation_In, 
  const AVSValue& chromaResampler, const AVSValue& param1, const AVSValue& param2, const AVSValue& param3,
  int _ChromaLocation_Out,
  IScriptEnvironment* env) : 
  GenericVideoFilter(src), ChromaLocation_In(_ChromaLocation_In), ChromaLocation_Out(_ChromaLocation_Out)
{
  Yinput = vi.NumComponents() == 1;
  pixelsize = vi.ComponentSize();

  if (Yinput) {
    vi.pixel_type = dst_space;
    if (vi.ComponentSize() != pixelsize)
      env->ThrowError("Convert: Conversion from %d to %d-byte format not supported.", pixelsize, vi.ComponentSize());
    return;
  }

  auto Is420 = [](int pix_type) {
    return pix_type == VideoInfo::CS_YV12 || pix_type == VideoInfo::CS_I420 ||
      pix_type == VideoInfo::CS_YUV420P10 || pix_type == VideoInfo::CS_YUV420P12 ||
      pix_type == VideoInfo::CS_YUV420P14 || pix_type == VideoInfo::CS_YUV420P16 ||
      pix_type == VideoInfo::CS_YUV420PS ||
        pix_type == VideoInfo::CS_YUVA420 ||
      pix_type == VideoInfo::CS_YUVA420P10 || pix_type == VideoInfo::CS_YUVA420P12 ||
      pix_type == VideoInfo::CS_YUVA420P14 || pix_type == VideoInfo::CS_YUVA420P16 ||
      pix_type == VideoInfo::CS_YUVA420PS;
  };

  if (!Is420(vi.pixel_type) && !Is420(dst_space))
    interlaced = false;  // Ignore, if YV12 is not involved.
  //if (interlaced) env->ThrowError("Convert: Interlaced only available with 4:2:0 color spaces.");

  // Describe input pixel positioning
  float xdInU = 0.0f, txdInU = 0.0f, bxdInU = 0.0f;
  float ydInU = 0.0f, tydInU = 0.0f, bydInU = 0.0f;
  float xdInV = 0.0f, txdInV = 0.0f, bxdInV = 0.0f;
  float ydInV = 0.0f, tydInV = 0.0f, bydInV = 0.0f;

  /*
    "mpeg1", "center"
      - 1-1 averaging kernel (4:2:0, 4:2:2 (horizontal only))
    "mpeg2", "left" (4:2:0, 4:2:2)
      - top-field samples are sited 1/4 sample below the luma samples (4:2:0)
      - bottom-field samples are sited 1/4 sample above the luma samples (4:2:0)
      - horizontal 1-2-1 kernel 
    "dv": (4:2:0)
      Chroma samples are sited on top of luma samples, but CB and CR samples are sited on alternate lines.
      - top: V
      - bottom: U
    "top_left" (4:2:0)
      - horizontal 1-2-1 vertical 1-2-1
  */

  if (Is420(vi.pixel_type)) {
    switch (ChromaLocation_In) {
      case ChromaLocation_e::AVS_CHROMA_DV: // spec. avisynth
        xdInU = 0.0f; ydInU = 1.0f; txdInU = 0.0f; tydInU = 1.0f; bxdInU = 0.0f; bydInU = 1.0f; // Cb
        xdInV = 0.0f; ydInV = 0.0f; txdInV = 0.0f; tydInV = 0.0f; bxdInV = 0.0f; bydInV = 0.0f; // Cr
        break;
      case ChromaLocation_e::AVS_CHROMA_TOP:
        xdInU = 0.5f, ydInU = 0.0f; txdInU = 0.5f; tydInU = 0.0f; bxdInU = 0.5f; bydInU = 0.5f;
        xdInV = 0.5f, ydInV = 0.0f; txdInV = 0.5f; tydInV = 0.0f; bxdInV = 0.5f; bydInV = 0.5f;
        break;
      case ChromaLocation_e::AVS_CHROMA_CENTER: // mpeg1, center
        xdInU = 0.5f, ydInU = 0.5f; txdInU = 0.5f; tydInU = 0.25f; bxdInU = 0.5f; bydInU = 0.75f;
        xdInV = 0.5f, ydInV = 0.5f; txdInV = 0.5f; tydInV = 0.25f; bxdInV = 0.5f; bydInV = 0.75f;
        break;
      case ChromaLocation_e::AVS_CHROMA_BOTTOM:
        xdInU = 0.5f, ydInU = 1.0f; txdInU = 0.5f; tydInU = 0.5f; bxdInU = 0.5f; bydInU = 1.0f;
        xdInV = 0.5f, ydInV = 1.0f; txdInV = 0.5f; tydInV = 0.5f; bxdInV = 0.5f; bydInV = 1.0f;
        break;
      case ChromaLocation_e::AVS_CHROMA_TOP_LEFT:
        xdInU = 0.0f; ydInU = 0.0f; txdInU = 0.0f; tydInU = 0.0f; bxdInU = 0.0f; bydInU = 0.5f;
        xdInV = 0.0f; ydInV = 0.0f; txdInV = 0.0f; tydInV = 0.0f; bxdInV = 0.0f; bydInV = 0.5f;
        break;
      case ChromaLocation_e::AVS_CHROMA_LEFT: // left, mpeg2
        xdInU = 0.0f; ydInU = 0.5f; txdInU = 0.0f; tydInU = 0.25f; bxdInU = 0.0f; bydInU = 0.75f;
        xdInV = 0.0f; ydInV = 0.5f; txdInV = 0.0f; tydInV = 0.25f; bxdInV = 0.0f; bydInV = 0.75f;
        break;
      case ChromaLocation_e::AVS_CHROMA_BOTTOM_LEFT:
        xdInU = 0.0f; ydInU = 1.0f; txdInU = 0.0f; tydInU = 0.5f; bxdInU = 0.0f; bydInU = 1.0f;
        xdInV = 0.0f; ydInV = 1.0f; txdInV = 0.0f; tydInV = 0.5f; bxdInV = 0.0f; bydInV = 1.0f;
        break;
      default:
        env->ThrowError("Convert: not supported ChromaPlacement for 4:2:0 input.");
    }
  }
  else if (vi.Is422()) {
    switch (ChromaLocation_In) {
    case ChromaLocation_e::AVS_CHROMA_CENTER: // center
      xdInU = 0.5f, ydInU = 0.0f; txdInU = 0.5f; tydInU = 0.0f; bxdInU = 0.5f; bydInU = 0.0f;
      xdInV = 0.5f, ydInV = 0.0f; txdInV = 0.5f; tydInV = 0.0f; bxdInV = 0.5f; bydInV = 0.0f;
      break;
    case ChromaLocation_e::AVS_CHROMA_TOP_LEFT: // treated as left
    case ChromaLocation_e::AVS_CHROMA_LEFT: // left, mpeg2
    case ChromaLocation_e::AVS_CHROMA_BOTTOM_LEFT: // treated as left
      xdInU = 0.0f; ydInU = 0.0f; txdInU = 0.0f; tydInU = 0.0f; bxdInU = 0.0f; bydInU = 0.0f;
      xdInV = 0.0f; ydInV = 0.0f; txdInV = 0.0f; tydInV = 0.0f; bxdInV = 0.0f; bydInV = 0.0f;
      break;
    default:
      env->ThrowError("Convert: not supported ChromaPlacement for 4:2:2 input.");
    }
  }
  else if (vi.IsYV411()) {
    if (ChromaLocation_In >= 0
      && ChromaLocation_In != ChromaLocation_e::AVS_CHROMA_TOP_LEFT
      && ChromaLocation_In != ChromaLocation_e::AVS_CHROMA_LEFT
      && ChromaLocation_In != ChromaLocation_e::AVS_CHROMA_BOTTOM_LEFT
      )
    {
      // if given, only the 'left'-ish versions are accepted, this is how it is treated
      env->ThrowError("Convert: not supported ChromaPlacement for 4:1:1 input. Only 'left'-style is allowed if given.");
    }
  }
  else if (ChromaLocation_In >= 0)
    env->ThrowError("Convert: Input ChromaPlacement is invalid for this format.");

  const int xsIn = 1 << vi.GetPlaneWidthSubsampling(PLANAR_U);
  const int ysIn = 1 << vi.GetPlaneHeightSubsampling(PLANAR_U);

  // change vi to the output format
  vi.pixel_type = dst_space;

  if (vi.ComponentSize() != pixelsize)
    env->ThrowError("Convert: Conversion from %d to %d-byte format not supported.", pixelsize, vi.ComponentSize());

  // Describe output pixel positioning
  float xdOutU = 0.0f, txdOutU = 0.0f, bxdOutU = 0.0f;
  float ydOutU = 0.0f, tydOutU = 0.0f, bydOutU = 0.0f;
  float xdOutV = 0.0f, txdOutV = 0.0f, bxdOutV = 0.0f;
  float ydOutV = 0.0f, tydOutV = 0.0f, bydOutV = 0.0f;

  if (Is420(vi.pixel_type)) {
    switch (ChromaLocation_Out) {
    case ChromaLocation_e::AVS_CHROMA_DV:
      xdOutU = 0.0f; ydOutU = 1.0f; txdOutU = 0.0f; tydOutU = 1.0f; bxdOutU = 0.0f; bydOutU = 1.0f; // Cb
      xdOutV = 0.0f; ydOutV = 0.0f; txdOutV = 0.0f; tydOutV = 0.0f; bxdOutV = 0.0f; bydOutV = 0.0f; // Cr
      break;
    case ChromaLocation_e::AVS_CHROMA_TOP:
      xdOutU = 0.5f, ydOutU = 0.0f; txdOutU = 0.5f; tydOutU = 0.0f; bxdOutU = 0.5f; bydOutU = 0.5f;
      xdOutV = 0.5f, ydOutV = 0.0f; txdOutV = 0.5f; tydOutV = 0.0f; bxdOutV = 0.5f; bydOutV = 0.5f;
      break;
    case ChromaLocation_e::AVS_CHROMA_CENTER: // mpeg1, center
      xdOutU = 0.5f, ydOutU = 0.5f; txdOutU = 0.5f; tydOutU = 0.25f; bxdOutU = 0.5f; bydOutU = 0.75f;
      xdOutV = 0.5f, ydOutV = 0.5f; txdOutV = 0.5f; tydOutV = 0.25f; bxdOutV = 0.5f; bydOutV = 0.75f;
      break;
    case ChromaLocation_e::AVS_CHROMA_BOTTOM:
      xdOutU = 0.5f, ydOutU = 1.0f; txdOutU = 0.5f; tydOutU = 0.5f; bxdOutU = 0.5f; bydOutU = 1.0f;
      xdOutV = 0.5f, ydOutV = 1.0f; txdOutV = 0.5f; tydOutV = 0.5f; bxdOutV = 0.5f; bydOutV = 1.0f;
      break;
    case ChromaLocation_e::AVS_CHROMA_TOP_LEFT:
      xdOutU = 0.0f; ydOutU = 0.0f; txdOutU = 0.0f; tydOutU = 0.0f; bxdOutU = 0.0f; bydOutU = 0.5f;
      xdOutV = 0.0f; ydOutV = 0.0f; txdOutV = 0.0f; tydOutV = 0.0f; bxdOutV = 0.0f; bydOutV = 0.5f;
      break;
    case ChromaLocation_e::AVS_CHROMA_LEFT: // left, mpeg2
      xdOutU = 0.0f; ydOutU = 0.5f; txdOutU = 0.0f; tydOutU = 0.25f; bxdOutU = 0.0f; bydOutU = 0.75f;
      xdOutV = 0.0f; ydOutV = 0.5f; txdOutV = 0.0f; tydOutV = 0.25f; bxdOutV = 0.0f; bydOutV = 0.75f;
      break;
    case ChromaLocation_e::AVS_CHROMA_BOTTOM_LEFT:
      xdOutU = 0.0f; ydOutU = 1.0f; txdOutU = 0.0f; tydOutU = 0.5f; bxdOutU = 0.0f; bydOutU = 1.0f;
      xdOutV = 0.0f; ydOutV = 1.0f; txdOutV = 0.0f; tydOutV = 0.5f; bxdOutV = 0.0f; bydOutV = 1.0f;
      break;
    default:
      env->ThrowError("Convert: not supported ChromaPlacement for 4:2:0 output.");
    }
  }
  else if (vi.Is422()) {
    switch (ChromaLocation_Out) {
    case ChromaLocation_e::AVS_CHROMA_CENTER: // mpeg1, center
      xdOutU = 0.5f, ydOutU = 0.0f; txdOutU = 0.5f; tydOutU = 0.0f; bxdOutU = 0.5f; bydOutU = 0.0f;
      xdOutV = 0.5f, ydOutV = 0.0f; txdOutV = 0.5f; tydOutV = 0.0f; bxdOutV = 0.5f; bydOutV = 0.0f;
      break;
    case ChromaLocation_e::AVS_CHROMA_TOP_LEFT: // treated as left
    case ChromaLocation_e::AVS_CHROMA_LEFT: // left, mpeg2
    case ChromaLocation_e::AVS_CHROMA_BOTTOM_LEFT: // treated as left
      xdOutU = 0.0f; ydOutU = 0.0f; txdOutU = 0.0f; tydOutU = 0.0f; bxdOutU = 0.0f; bydOutU = 0.0f;
      xdOutV = 0.0f; ydOutV = 0.0f; txdOutV = 0.0f; tydOutV = 0.0f; bxdOutV = 0.0f; bydOutV = 0.0f;
      break;
    default:
      env->ThrowError("Convert: not supported ChromaPlacement for 4:2:2 output.");
    }
  }
  else if (ChromaLocation_Out >= 0)
    env->ThrowError("Convert: Output ChromaPlacement only available with 4:2:0 or 4:2:2 output.");

  const int xsOut = 1 << vi.GetPlaneWidthSubsampling(PLANAR_U);
  const int xmask = xsOut - 1;
  if (vi.width & xmask)
    env->ThrowError("Convert: Cannot convert if width isn't mod%d!", xsOut);

  const int ysOut = 1 << vi.GetPlaneHeightSubsampling(PLANAR_U);
  const int ymask = ysOut - 1;
  if (vi.height & ymask)
    env->ThrowError("Convert: Cannot convert if height isn't mod%d!", ysOut);
  if (interlaced && ysOut == 2 && (vi.height & 3))
    env->ThrowError("Convert: interlaced 4:2:0 conversion requires height to be mod4!");

  int uv_width  = vi.width  >> vi.GetPlaneWidthSubsampling(PLANAR_U);
  int uv_height = vi.height >> vi.GetPlaneHeightSubsampling(PLANAR_U);

  const vc_filter_spec filter = getResampler(chromaResampler.AsString("bicubic"), param1, param2, param3, true, env);

  bool P = !lstrcmpi(chromaResampler.AsString(""), "point");

  auto ChrOffset = [P](int sIn, float dIn, int sOut, float dOut) {
    //     (1 - sOut/sIn)/2 + (dOut-dIn)/sIn; // Gavino Jan 2011
    return P ? (dOut - dIn) / sIn : 0.5f + (dOut - dIn - 0.5f*sOut) / sIn;
  };

  const int force = 0;
  const bool preserve_center = true;
  const char *placement_name_notused = nullptr; // n/a
  const int forced_chroma_placement = -1; // no force
  // chroma planes are extracted, behave like Y when resized, no chroma planes involved

  if (interlaced) {
    uv_height /=  2;

    AVSValue tUsubSampling[4] = { ChrOffset(xsIn, txdInU, xsOut, txdOutU), ChrOffset(ysIn, tydInU, ysOut, tydOutU), AVSValue(), AVSValue() };
    AVSValue bUsubSampling[4] = { ChrOffset(xsIn, bxdInU, xsOut, bxdOutU), ChrOffset(ysIn, bydInU, ysOut, bydOutU), AVSValue(), AVSValue() };
    AVSValue tVsubSampling[4] = { ChrOffset(xsIn, txdInV, xsOut, txdOutV), ChrOffset(ysIn, tydInV, ysOut, tydOutV), AVSValue(), AVSValue() };
    AVSValue bVsubSampling[4] = { ChrOffset(xsIn, bxdInV, xsOut, bxdOutV), ChrOffset(ysIn, bydInV, ysOut, bydOutV), AVSValue(), AVSValue() };

    Usource = new SeparateFields(new AssumeParity(new SwapUVToY(child, SwapUVToY::UToY8, env), true), env); // also works for Y16/Y32
    Vsource = new SeparateFields(new AssumeParity(new SwapUVToY(child, SwapUVToY::VToY8, env), true), env); // also works for Y16/Y32

    std::vector<PClip> tbUsource(2); // Interleave() will take ownership of these
    std::vector<PClip> tbVsource(2);

    tbUsource[0] = FilteredResize::CreateResize(new SelectEvery(Usource, 2, 0, env), uv_width, uv_height, tUsubSampling, force, filter, preserve_center, placement_name_notused, forced_chroma_placement, env);
    tbUsource[1] = FilteredResize::CreateResize(new SelectEvery(Usource, 2, 1, env), uv_width, uv_height, bUsubSampling, force, filter, preserve_center, placement_name_notused, forced_chroma_placement, env);
    tbVsource[0] = FilteredResize::CreateResize(new SelectEvery(Vsource, 2, 0, env), uv_width, uv_height, tVsubSampling, force, filter, preserve_center, placement_name_notused, forced_chroma_placement, env);
    tbVsource[1] = FilteredResize::CreateResize(new SelectEvery(Vsource, 2, 1, env), uv_width, uv_height, bVsubSampling, force, filter, preserve_center, placement_name_notused, forced_chroma_placement, env);

    Usource = new SelectEvery(new DoubleWeaveFields(new Interleave(std::move(tbUsource), env)), 2, 0, env);
    Vsource = new SelectEvery(new DoubleWeaveFields(new Interleave(std::move(tbVsource), env)), 2, 0, env);
  }
  else {
    AVSValue UsubSampling[4] = { ChrOffset(xsIn, xdInU, xsOut, xdOutU), ChrOffset(ysIn, ydInU, ysOut, ydOutU), AVSValue(), AVSValue() };
    AVSValue VsubSampling[4] = { ChrOffset(xsIn, xdInV, xsOut, xdOutV), ChrOffset(ysIn, ydInV, ysOut, ydOutV), AVSValue(), AVSValue() };

    Usource = FilteredResize::CreateResize(new SwapUVToY(child, SwapUVToY::UToY8, env), uv_width, uv_height, UsubSampling, force, filter, preserve_center, placement_name_notused, forced_chroma_placement, env);
    Vsource = FilteredResize::CreateResize(new SwapUVToY(child, SwapUVToY::VToY8, env), uv_width, uv_height, VsubSampling, force, filter, preserve_center, placement_name_notused, forced_chroma_placement, env);
  }

}

PVideoFrame __stdcall ConvertToPlanarGeneric::GetFrame(int n, IScriptEnvironment* env) {
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);

  auto props = env->getFramePropsRW(dst);
  update_ChromaLocation(props, ChromaLocation_Out, env);

  env->BitBlt(dst->GetWritePtr(PLANAR_Y), dst->GetPitch(PLANAR_Y), src->GetReadPtr(PLANAR_Y), src->GetPitch(PLANAR_Y),
              src->GetRowSize(PLANAR_Y_ALIGNED), src->GetHeight(PLANAR_Y));

  // alpha. if pitch is zero -> no alpha channel
  const int rowsizeA = dst->GetRowSize(PLANAR_A);
  const int dst_pitchA = dst->GetPitch(PLANAR_A);
  BYTE* dstp_a = (dst_pitchA == 0) ? nullptr : dst->GetWritePtr(PLANAR_A);
  const int heightA = dst->GetHeight(PLANAR_A);

  if (dst_pitchA != 0)
  {
    if (src->GetPitch(PLANAR_A) != 0)
      env->BitBlt(dstp_a, dst_pitchA, src->GetReadPtr(PLANAR_A), src->GetPitch(PLANAR_A),
        src->GetRowSize(PLANAR_A_ALIGNED), src->GetHeight(PLANAR_A));
    else {
      // e.g. ConvertToYUVA() case from Alpha-less formats
      switch (vi.ComponentSize())
      {
      case 1:
        fill_plane<BYTE>(dstp_a, heightA, rowsizeA, dst_pitchA, 255);
        break;
      case 2:
        fill_plane<uint16_t>(dstp_a, heightA, rowsizeA, dst_pitchA, (1 << vi.BitsPerComponent()) - 1);
        break;
      case 4:
        fill_plane<float>(dstp_a, heightA, rowsizeA, dst_pitchA, 1.0f);
        break;
      }
    }
  }

  BYTE* dstp_u = dst->GetWritePtr(PLANAR_U);
  BYTE* dstp_v = dst->GetWritePtr(PLANAR_V);
  const int height = dst->GetHeight(PLANAR_U);
  const int rowsizeUV = dst->GetRowSize(PLANAR_U);
  const int dst_pitch = dst->GetPitch(PLANAR_U);

  if (Yinput) {
    switch (vi.ComponentSize())
    {
      case 1:
        fill_chroma<BYTE>(dstp_u, dstp_v, height, rowsizeUV, dst_pitch, 0x80);
        break;
      case 2:
        fill_chroma<uint16_t>(dstp_u, dstp_v, height, rowsizeUV, dst_pitch, 1 << (vi.BitsPerComponent() - 1));
        break;
      case 4:
        const float half = 0.0f;
        fill_chroma<float>(dstp_u, dstp_v, height, rowsizeUV, dst_pitch, half);
        break;
    }
  } else {
    src = Usource->GetFrame(n, env);
    env->BitBlt(dstp_u, dst_pitch, src->GetReadPtr(PLANAR_Y), src->GetPitch(PLANAR_Y), src->GetRowSize(PLANAR_Y_ALIGNED), height);
    src = Vsource->GetFrame(n, env);
    env->BitBlt(dstp_v, dst_pitch, src->GetReadPtr(PLANAR_Y), src->GetPitch(PLANAR_Y), src->GetRowSize(PLANAR_Y_ALIGNED), height);
  }

  return dst;
}

AVSValue ConvertToPlanarGeneric::Create(AVSValue& args, const char* filter, bool strip_alpha_legacy_8bit, bool to_yuva, IScriptEnvironment* env) {
  bool converted = false;

  PClip clip = args[0].AsClip();
  VideoInfo vi = clip->GetVideoInfo();

  if (vi.IsRGB()) { // packed or planar
    const bool keep_packedrgb_alpha = to_yuva && (vi.IsRGB32() || vi.IsRGB64());
    if (vi.IsRGB48() || vi.IsRGB64()) {
      // we convert to intermediate PlanarRGB, RGB48/64->YUV444 is slow C, planarRGB  is fast
      AVSValue new_args[8] = { clip, AVSValue(), AVSValue(), AVSValue(), AVSValue(), AVSValue(), AVSValue(), AVSValue() };
      // clip, matrix,  interlaced, chromainplacement, chromaresample, param1, param2, param3
      // convert to planar RGBA only if RGB64 and target is YUVA (need to keep alpha)
      const intptr_t planar_rgb_type = keep_packedrgb_alpha ? -2 : -1;
      clip = ConvertToRGB::Create(AVSValue(new_args, 8), (void *)planar_rgb_type, env).AsClip();
      vi = clip->GetVideoInfo();
    }

    clip = new ConvertRGBToYUV444(clip, args[2].AsString(0) /* matrix_name */, keep_packedrgb_alpha, env);
    vi = clip->GetVideoInfo();
    converted = true;
  }
  else if (vi.IsYUY2()) { // 8 bit only
    clip = new ConvertYUY2ToYV16(clip, env);
    vi = clip->GetVideoInfo();
    converted = true;
  }
  else if (!vi.IsPlanar())
    env->ThrowError("%s: Can only convert from Planar YUV.", filter);

  int pixel_type = VideoInfo::CS_UNKNOWN;
  AVSValue outplacement = AVSValue(); // only for ConvertToYUV420 and ConvertToYUV422

  bool hasAlpha = vi.NumComponents() == 4 && !strip_alpha_legacy_8bit;
  bool shouldStripAlpha = vi.NumComponents() == 4 && strip_alpha_legacy_8bit;
  bool shouldAddAlpha = vi.NumComponents() != 4 && to_yuva;
  bool targethasAlpha = hasAlpha || shouldAddAlpha;

  int ChromaLocation_In = -1; // invalid. Chromalocation_e::AVS_CHROMALOCATION_UNUSED
  int ChromaLocation_Out = -1;

  const bool to_420 = strcmp(filter, "ConvertToYUV420") == 0;
  const bool to_422 = strcmp(filter, "ConvertToYUV422") == 0;
  const bool to_411 = strcmp(filter, "ConvertToYV411") == 0;
  const bool to_444 = strcmp(filter, "ConvertToYUV444") == 0;

  if (vi.IsYV411()) {
    // ChromaInPlacement parameter exists, (default none/-1) + input frame properties; 'left'-ish _ChromaLocation is allowed, checked later
    auto frame0 = clip->GetFrame(0, env);
    const AVSMap* props = env->getFramePropsRO(frame0);
    chromaloc_parse_merge_with_props(vi, args[3].AsString(nullptr), props, /* ref*/ChromaLocation_In, -1 /*default none chromaloc */, env);
  }
  else if (vi.Is420() || vi.Is422()) {
    // ChromaInPlacement parameter is valid + input frame properties
    auto frame0 = clip->GetFrame(0, env);
    const AVSMap* props = env->getFramePropsRO(frame0);
    chromaloc_parse_merge_with_props(vi, args[3].AsString(nullptr), props, /* ref*/ChromaLocation_In, ChromaLocation_e::AVS_CHROMA_LEFT /*default*/, env);
  }

  AVSValue param1;
  AVSValue param2;
  AVSValue param3;
  if (to_420 || to_422) {
    // ChromaOutPlacement parameter is valid
    chromaloc_parse_merge_with_props(vi, args[5].AsString(nullptr), nullptr, /* ref*/ChromaLocation_Out, ChromaLocation_e::AVS_CHROMA_LEFT /*default*/, env);
    param1 = args[6];
    param2 = args[7];
    param3 = args[8];
  }
  else {
    param1 = args[5];
    param2 = args[6];
    param3 = args[7];
  }


  if (to_420) {
    if (vi.Is420()) {
      // possible shortcut
      if (ChromaLocation_In == ChromaLocation_Out)
      {
        if (shouldStripAlpha)
          return new RemoveAlphaPlane(clip, env);
        if (shouldAddAlpha) {
          // create with default alpha
          clip = new AddAlphaPlane(clip, nullptr, 0.0f, false, env);
          vi = clip->GetVideoInfo();
        }
        return clip;
      }
    }

    outplacement = args[5];
    switch (vi.BitsPerComponent())
    {
    case 8 : pixel_type = targethasAlpha ? VideoInfo::CS_YUVA420 : VideoInfo::CS_YV12; break;
    case 10: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA420P10 : VideoInfo::CS_YUV420P10; break;
    case 12: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA420P12 : VideoInfo::CS_YUV420P12; break;
    case 14: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA420P14 : VideoInfo::CS_YUV420P14; break;
    case 16: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA420P16 : VideoInfo::CS_YUV420P16; break;
    case 32: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA420PS  : VideoInfo::CS_YUV420PS; break;
    }
  }
  else if (to_422) {
    if (vi.Is422()) {
      // possible shortcut
      if (ChromaLocation_In == ChromaLocation_Out)
      {
        if (shouldStripAlpha)
          return new RemoveAlphaPlane(clip, env);
        if (shouldAddAlpha) {
          // create with default alpha
          clip = new AddAlphaPlane(clip, nullptr, 0.0f, false, env);
          vi = clip->GetVideoInfo();
        }
        return clip;
      }
    }

    outplacement = args[5];
    switch (vi.BitsPerComponent())
    {
    case 8 : pixel_type = targethasAlpha ? VideoInfo::CS_YUVA422 : VideoInfo::CS_YV16; break;
    case 10: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA422P10 : VideoInfo::CS_YUV422P10; break;
    case 12: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA422P12 : VideoInfo::CS_YUV422P12; break;
    case 14: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA422P14 : VideoInfo::CS_YUV422P14; break;
    case 16: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA422P16 : VideoInfo::CS_YUV422P16; break;
    case 32: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA422PS  : VideoInfo::CS_YUV422PS; break;
    }
  }
  else if (to_444) {
    if (vi.Is444()) {
      if (shouldStripAlpha)
        return new RemoveAlphaPlane(clip, env);
      if (shouldAddAlpha) {
        // create with default alpha
        clip = new AddAlphaPlane(clip, nullptr, 0.0f, false, env);
        vi = clip->GetVideoInfo();
      }
      return clip;
    }

    switch (vi.BitsPerComponent())
    {
    case 8 : pixel_type = targethasAlpha ? VideoInfo::CS_YUVA444 : VideoInfo::CS_YV24; break;
    case 10: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA444P10 : VideoInfo::CS_YUV444P10; break;
    case 12: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA444P12 : VideoInfo::CS_YUV444P12; break;
    case 14: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA444P14 : VideoInfo::CS_YUV444P14; break;
    case 16: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA444P16 : VideoInfo::CS_YUV444P16; break;
    case 32: pixel_type = targethasAlpha ? VideoInfo::CS_YUVA444PS  : VideoInfo::CS_YUV444PS; break;
    }
  }
  else if (to_411) {
    if (vi.IsYV411()) return clip;
    if(vi.ComponentSize()!=1)
      env->ThrowError("%s: 8 bit input only", filter);

    pixel_type = VideoInfo::CS_YV411;
  }
  else env->ThrowError("Convert: unknown filter '%s'.", filter);

  if (pixel_type == VideoInfo::CS_UNKNOWN)
    env->ThrowError("%s: unsupported bit depth", filter);

  if (converted)
    clip = env->Invoke("Cache", AVSValue(clip)).AsClip();

  // ConvertToPlanarGeneric's GetFrame will recognize if alpha copy or fill-with-defaults needed
  return new ConvertToPlanarGeneric(clip, pixel_type, args[1].AsBool(false), ChromaLocation_In, 
    args[4], // chromaresample
    param1, param2, param3,
    ChromaLocation_Out, env);
}

AVSValue __cdecl ConvertToPlanarGeneric::CreateYUV420(AVSValue args, void* user_data, IScriptEnvironment* env) {
  PClip clip = args[0].AsClip();
  const VideoInfo& vi = clip->GetVideoInfo();
  bool only_8bit = reinterpret_cast<intptr_t>(user_data) == 0;
  bool to_yuva = reinterpret_cast<intptr_t>(user_data) == 2;
  if (only_8bit && vi.BitsPerComponent() != 8)
    env->ThrowError("ConvertToYV12: only 8 bit sources allowed");
  return Create(args, "ConvertToYUV420", only_8bit, to_yuva, env);
}

AVSValue __cdecl ConvertToPlanarGeneric::CreateYUV422(AVSValue args, void* user_data, IScriptEnvironment* env) {
  PClip clip = args[0].AsClip();
  const VideoInfo& vi = clip->GetVideoInfo();
  bool only_8bit = reinterpret_cast<intptr_t>(user_data) == 0;
  bool to_yuva = reinterpret_cast<intptr_t>(user_data) == 2;
  if (only_8bit && vi.BitsPerComponent() != 8)
    env->ThrowError("ConvertToYV16: only 8 bit sources allowed");
  return Create(args, "ConvertToYUV422", only_8bit, to_yuva, env);
}

AVSValue __cdecl ConvertToPlanarGeneric::CreateYUV444(AVSValue args, void* user_data, IScriptEnvironment* env) {
  PClip clip = args[0].AsClip();
  const VideoInfo& vi = clip->GetVideoInfo();
  bool only_8bit = reinterpret_cast<intptr_t>(user_data) == 0;
  bool to_yuva = reinterpret_cast<intptr_t>(user_data) == 2;
  if (only_8bit && vi.BitsPerComponent() != 8)
    env->ThrowError("ConvertToYV24: only 8 bit sources allowed");
  return Create(args, "ConvertToYUV444", only_8bit, to_yuva, env);
}

AVSValue __cdecl ConvertToPlanarGeneric::CreateYV411(AVSValue args, void* user_data, IScriptEnvironment* env) {
  PClip clip = args[0].AsClip();
  const VideoInfo& vi = clip->GetVideoInfo();
  bool only_8bit = reinterpret_cast<intptr_t>(user_data) == 0;
  // though Avisynth does not have YUVA411, make similar to others
  bool to_yuva = reinterpret_cast<intptr_t>(user_data) == 2;
  if (only_8bit && vi.BitsPerComponent() != 8)
    env->ThrowError("ConvertToYV411: only 8 bit sources allowed");
  return Create(args, "ConvertToYV411", only_8bit, to_yuva, env);
}


/*
static int getPlacement(const AVSValue& _placement, IScriptEnvironment* env) {
  const char* placement = _placement.AsString(0);

  if (placement) {
    if (!lstrcmpi(placement, "mpeg2") || !lstrcmpi(placement, "left"))
      return PLACEMENT_MPEG2;

    if (!lstrcmpi(placement, "mpeg1") || !lstrcmpi(placement, "jpeg") || !lstrcmpi(placement, "center"))
      return PLACEMENT_MPEG1;

    if (!lstrcmpi(placement, "dv"))
      return PLACEMENT_DV;

    if (!lstrcmpi(placement, "top_left"))
      return PLACEMENT_TOP_LEFT;

    env->ThrowError("Convert: Unknown chromaplacement");
  }
  return PLACEMENT_MPEG2;
}
*/


vc_filter_spec getResampler(const char* resampler, AVSValue param1, AVSValue param2, AVSValue param3, bool throw_on_error, IScriptEnvironment* env) {
  if (resampler) {
    if (!lstrcmpi(resampler, "point"))
      return vc_filter_spec{VC_POINT, {}};
    else if (!lstrcmpi(resampler, "bilinear"))
      return vc_filter_spec{VC_TRIANGLE, {}};
    else if (!lstrcmpi(resampler, "bicubic"))
      return vc_filter_spec{VC_BICUBIC, {param1.AsDblDef(1.0/3), param2.AsDblDef(1.0/3)}}; // optional B and C as param1 and param2
    else if (!lstrcmpi(resampler, "lanczos"))
      return vc_filter_spec{VC_LANCZOS, {double(std::clamp((int)param1.AsFloat(3), 1, 150))}}; // optional Taps as param1
    else if (!lstrcmpi(resampler, "lanczos4"))
      return vc_filter_spec{VC_LANCZOS, {double(4)}};
    else if (!lstrcmpi(resampler, "blackman"))
      return vc_filter_spec{VC_BLACKMAN, {double(std::clamp((int)param1.AsFloat(4), 1, 150))}}; // optional Taps as param1
    else if (!lstrcmpi(resampler, "spline16"))
      return vc_filter_spec{VC_SPLINE16, {}};
    else if (!lstrcmpi(resampler, "spline36"))
      return vc_filter_spec{VC_SPLINE36, {}};
    else if (!lstrcmpi(resampler, "spline64"))
      return vc_filter_spec{VC_SPLINE64, {}};
    else if (!lstrcmpi(resampler, "gauss"))
      return vc_filter_spec{VC_GAUSSIAN, {param1.AsDblDef(30.0), param2.AsDblDef(2.0), param3.AsDblDef(4.0)}}; // optional P, B, S as param1, param2, param3
    else if (!lstrcmpi(resampler, "sinc"))
      return vc_filter_spec{VC_SINC, {double(std::clamp((int)param1.AsFloat(4), 1, 150))}}; // optional Taps as param1
    else if (!lstrcmpi(resampler, "sinpow"))
      return vc_filter_spec{VC_SINPOWER, {param1.AsDblDef(2.5)}}; // optional P as param1
    else if (!lstrcmpi(resampler, "sinclin2"))
      return vc_filter_spec{VC_SINCLIN2, {double(std::clamp((int)param1.AsFloat(15), 1, 150))}}; // optional Taps= as param1
    else if (!lstrcmpi(resampler, "userdefined2"))
      return vc_filter_spec{VC_USER_DEFINED2, {param1.AsDblDef(121.0), param2.AsDblDef(19.0), param3.AsDblDef(2.3)}}; // optional B and C and S as param1, param2, param3
    else
      if (throw_on_error)
        env->ThrowError("Convert: Unknown chroma resampler, '%s'", resampler);
      else
        return {-1, {}}; // e.g. from AddBorders: unrecognized filter
  }
  return vc_filter_spec{VC_BICUBIC, {param1.AsDblDef(1.0/3), param2.AsDblDef(1.0/3)}}; // Default colorspace conversion for AviSynth
}

// YUY2 is an 8-bit storage layout of planar YUV422. Keep the historical
// positional parameters; the output placement option is appended to the script API.
AVSValue __cdecl ConvertToPlanarGeneric::CreateConvertToYUY2(AVSValue args, void*, IScriptEnvironment* env) {
  PClip clip = args[0].AsClip();
  bool options = false;
  for (int i = 1; i < 9; ++i)
    options |= args[i].Defined();
  if (clip->GetVideoInfo().IsYUY2() && !options)
    return clip;
  AVSValue planar_args[9] = {clip, args[1], args[2], args[3], args[4], args[8], args[5], args[6], args[7]};
  clip = CreateYUV422(AVSValue(planar_args, 9), (void*)1, env).AsClip();
  if (clip->GetVideoInfo().BitsPerComponent() != 8) {
    AVSValue depth_args[2] = {clip, 8};
    clip = env->Invoke("ConvertBits", AVSValue(depth_args, 2)).AsClip();
  }
  if (clip->GetVideoInfo().IsYUVA())
    clip = new RemoveAlphaPlane(clip, env);
  return new ConvertYV16ToYUY2(clip, env);
}

AVSValue __cdecl ConvertToPlanarGeneric::CreateConvertBackToYUY2(AVSValue args, void*, IScriptEnvironment* env) {
  AVSValue mapped[9] = {args[0], false, args[1], AVSValue(), AVSValue(), AVSValue(), AVSValue(), AVSValue(), AVSValue()};
  return CreateConvertToYUY2(AVSValue(mapped, 9), nullptr, env);
}
