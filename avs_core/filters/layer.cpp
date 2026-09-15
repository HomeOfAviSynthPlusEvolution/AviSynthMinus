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



// Avisynth filter: Layer
// by "poptones" (poptones@myrealbox.com)

#include "avs_composite/adapter.h"
#include "layer.h"
#include <cmath>
#ifdef INTEL_INTRINSICS
#include "intel/layer_sse.h"
#endif
#ifdef AVS_WINDOWS
#include <avs/win.h>
#else
#include <avs/posix.h>
#endif

#include <avs/minmax.h>
#include <avs/alignment.h>
#include "../core/internal.h"
#include "../convert/convert_planar.h"
#include <algorithm>

enum { PLACEMENT_MPEG2, PLACEMENT_MPEG1 }; // for Layer 420, 422


enum MaskMode {
  MASK411,
  MASK420,
  MASK420_MPEG2,
  MASK422,
  MASK422_MPEG2,
  MASK444
};

static int getPlacement(const AVSValue& _placement, IScriptEnvironment* env) {
  const char* placement = _placement.AsString(0);

  if (placement) {
    if (!lstrcmpi(placement, "mpeg2"))
      return PLACEMENT_MPEG2;

    if (!lstrcmpi(placement, "mpeg1"))
      return PLACEMENT_MPEG1;

    env->ThrowError("Layer: Unknown chroma placement");
  }
  return PLACEMENT_MPEG2;
}

/********************************************************************
***** Declare index of new filters for Avisynth's filter engine *****
********************************************************************/

extern const AVSFunction Layer_filters[] = {
  { "Mask",         BUILTIN_FUNC_PREFIX, "cc", Mask::Create },     // clip, mask
  { "ColorKeyMask", BUILTIN_FUNC_PREFIX, "ci[]i[]i[]i", ColorKeyMask::Create },    // clip, color, tolerance[B, toleranceG, toleranceR]
  { "ResetMask",    BUILTIN_FUNC_PREFIX, "c[mask]f", ResetMask::Create },

   // AVS+ also for YUVA, PRGBA



   // AVS+
   // AVS+
   // AVS+


  { "Layer",        BUILTIN_FUNC_PREFIX, "cc[op]s[level]i[x]i[y]i[threshold]i[use_chroma]b[opacity]f[placement]s", Layer::Create },
  /**
    * Layer(clip, overlayclip, operation, amount, xpos, ypos, [threshold=0], [use_chroma=true])
   **/
  { "Subtract", BUILTIN_FUNC_PREFIX, "cc", Subtract::Create },
  { NULL }
};


/******************************
 *******   Mask Filter   ******
 ******************************/

Mask::Mask(PClip _child1, PClip _child2, IScriptEnvironment* env)
  : child1(_child1), child2(_child2)
{
  const VideoInfo& vi1 = child1->GetVideoInfo();
  const VideoInfo& vi2 = child2->GetVideoInfo();
  if (vi1.width != vi2.width || vi1.height != vi2.height)
    env->ThrowError("Mask error: image dimensions don't match");
  if (!((vi1.IsRGB32() && vi2.IsRGB32()) ||
    (vi1.IsRGB64() && vi2.IsRGB64()) ||
    (vi1.IsPlanarRGBA() && vi2.IsPlanarRGBA()))
    )
    env->ThrowError("Mask error: sources must be RGB32, RGB64 or Planar RGBA");

  if (vi1.BitsPerComponent() != vi2.BitsPerComponent())
    env->ThrowError("Mask error: Components are not of the same bit depths");

  vi = vi1;

  pixelsize = vi.ComponentSize();
  bits_per_pixel = vi.BitsPerComponent();

  mask_frames = vi2.num_frames;
}


template<typename pixel_t>
static void mask_c(BYTE* srcp8, const BYTE* alphap8, int src_pitch, int alpha_pitch, size_t width, size_t height) {
  pixel_t* srcp = reinterpret_cast<pixel_t*>(srcp8);
  const pixel_t* alphap = reinterpret_cast<const pixel_t*>(alphap8);

  src_pitch /= sizeof(pixel_t);
  alpha_pitch /= sizeof(pixel_t);

  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      srcp[x * 4 + 3] = (cyb * alphap[x * 4 + 0] + cyg * alphap[x * 4 + 1] + cyr * alphap[x * 4 + 2] + 16384) >> 15;
    }
    srcp += src_pitch;
    alphap += alpha_pitch;
  }
}

template<typename pixel_t>
static void mask_planar_rgb_c(BYTE* dstp8, const BYTE* srcp_r8, const BYTE* srcp_g8, const BYTE* srcp_b8, int dst_pitch, int src_pitch, size_t width, size_t height, int bits_per_pixel) {
  pixel_t* dstp = reinterpret_cast<pixel_t*>(dstp8);
  const pixel_t* srcp_r = reinterpret_cast<const pixel_t*>(srcp_r8);
  const pixel_t* srcp_g = reinterpret_cast<const pixel_t*>(srcp_g8);
  const pixel_t* srcp_b = reinterpret_cast<const pixel_t*>(srcp_b8);
  src_pitch /= sizeof(pixel_t);
  dst_pitch /= sizeof(pixel_t);

  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      dstp[x] = ((cyb * srcp_b[x] + cyg * srcp_g[x] + cyr * srcp_r[x] + 16384) >> 15);
    }
    dstp += dst_pitch;
    srcp_r += src_pitch;
    srcp_g += src_pitch;
    srcp_b += src_pitch;
  }
}

static void mask_planar_rgb_float_c(BYTE* dstp8, const BYTE* srcp_r8, const BYTE* srcp_g8, const BYTE* srcp_b8, int dst_pitch, int src_pitch, size_t width, size_t height) {

  float* dstp = reinterpret_cast<float*>(dstp8);
  const float* srcp_r = reinterpret_cast<const float*>(srcp_r8);
  const float* srcp_g = reinterpret_cast<const float*>(srcp_g8);
  const float* srcp_b = reinterpret_cast<const float*>(srcp_b8);
  src_pitch /= sizeof(float);
  dst_pitch /= sizeof(float);

  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      dstp[x] = cyb_f * srcp_b[x] + cyg_f * srcp_g[x] + cyr_f * srcp_r[x];
    }
    dstp += dst_pitch;
    srcp_r += src_pitch;
    srcp_g += src_pitch;
    srcp_b += src_pitch;
  }
}

PVideoFrame __stdcall Mask::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src1 = child1->GetFrame(n, env);
  PVideoFrame src2 = child2->GetFrame(min(n, mask_frames - 1), env);

  env->MakeWritable(&src1);

  if (vi.IsPlanar()) {
    // planar RGB
    BYTE* dstp = src1->GetWritePtr(PLANAR_A); // destination Alpha plane

    const BYTE* srcp_g = src2->GetReadPtr(PLANAR_G);
    const BYTE* srcp_b = src2->GetReadPtr(PLANAR_B);
    const BYTE* srcp_r = src2->GetReadPtr(PLANAR_R);

    const int dst_pitch = src1->GetPitch();
    const int src_pitch = src2->GetPitch();

    // clip1_alpha = greyscale(clip2)
    if (pixelsize == 1)
      mask_planar_rgb_c<uint8_t>(dstp, srcp_r, srcp_g, srcp_b, dst_pitch, src_pitch, vi.width, vi.height, bits_per_pixel);
    else if (pixelsize == 2)
      mask_planar_rgb_c<uint16_t>(dstp, srcp_r, srcp_g, srcp_b, dst_pitch, src_pitch, vi.width, vi.height, bits_per_pixel);
    else
      mask_planar_rgb_float_c(dstp, srcp_r, srcp_g, srcp_b, dst_pitch, src_pitch, vi.width, vi.height);
  }
  else {
    // Packed RGB32/64
    BYTE* src1p = src1->GetWritePtr();
    const BYTE* src2p = src2->GetReadPtr();

    const int src1_pitch = src1->GetPitch();
    const int src2_pitch = src2->GetPitch();

    // clip1_alpha = greyscale(clip2)
#ifdef INTEL_INTRINSICS
    if ((pixelsize == 1) && (env->GetCPUFlags() & CPUF_SSE2) && IsPtrAligned(src1p, 16) && IsPtrAligned(src2p, 16))
    {
      mask_sse2(src1p, src2p, src1_pitch, src2_pitch, vi.width, vi.height);
    }
    else
#ifdef X86_32
      if ((pixelsize == 1) && (env->GetCPUFlags() & CPUF_MMX))
      {
        mask_mmx(src1p, src2p, src1_pitch, src2_pitch, vi.width, vi.height);
      }
      else
#endif
#endif
      {
        if (pixelsize == 1) {
          mask_c<uint8_t>(src1p, src2p, src1_pitch, src2_pitch, vi.width, vi.height);
        }
        else { // if (pixelsize == 2)
          mask_c<uint16_t>(src1p, src2p, src1_pitch, src2_pitch, vi.width, vi.height);
        }
      }
  }

  return src1;
}

AVSValue __cdecl Mask::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  return new Mask(args[0].AsClip(), args[1].AsClip(), env);
}


/**************************************
 *******   ColorKeyMask Filter   ******
 **************************************/


ColorKeyMask::ColorKeyMask(PClip _child, int _color, int _tolB, int _tolG, int _tolR, IScriptEnvironment* env)
  : GenericVideoFilter(_child), color(_color & 0xffffff), tolB(_tolB & 0xff), tolG(_tolG & 0xff), tolR(_tolR & 0xff)
{
  if (!vi.IsRGB32() && !vi.IsRGB64() && !vi.IsPlanarRGBA())
    env->ThrowError("ColorKeyMask: requires RGB32, RGB64 or Planar RGBA input");
  pixelsize = vi.ComponentSize();
  bits_per_pixel = vi.BitsPerComponent();
  max_pixel_value = (1 << bits_per_pixel) - 1;

  auto rgbcolor8to16 = [](uint8_t color8, int max_pixel_value) { return (uint16_t)(color8 * max_pixel_value / 255); };

  uint64_t r = rgbcolor8to16((color >> 16) & 0xFF, max_pixel_value);
  uint64_t g = rgbcolor8to16((color >> 8) & 0xFF, max_pixel_value);
  uint64_t b = rgbcolor8to16((color) & 0xFF, max_pixel_value);
  uint64_t a = rgbcolor8to16((color >> 24) & 0xFF, max_pixel_value);
  color64 = (a << 48) + (r << 32) + (g << 16) + (b);
  tolR16 = rgbcolor8to16(tolR & 0xFF, max_pixel_value); // scale tolerance
  tolG16 = rgbcolor8to16(tolG & 0xFF, max_pixel_value);
  tolB16 = rgbcolor8to16(tolB & 0xFF, max_pixel_value);
}


template<typename pixel_t>
static void colorkeymask_c(BYTE* pf8, int pitch, int R, int G, int B, int height, int rowsize, int tolB, int tolG, int tolR) {
  pixel_t* pf = reinterpret_cast<pixel_t*>(pf8);
  rowsize /= sizeof(pixel_t);
  pitch /= sizeof(pixel_t);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < rowsize; x += 4) {
      if (IsClose(pf[x], B, tolB) && IsClose(pf[x + 1], G, tolG) && IsClose(pf[x + 2], R, tolR))
        pf[x + 3] = 0;
    }
    pf += pitch;
  }
}

template<typename pixel_t>
static void colorkeymask_planar_c(const BYTE* pfR8, const BYTE* pfG8, const BYTE* pfB8, BYTE* pfA8, int pitch, int R, int G, int B, int height, int width, int tolB, int tolG, int tolR) {
  const pixel_t* pfR = reinterpret_cast<const pixel_t*>(pfR8);
  const pixel_t* pfG = reinterpret_cast<const pixel_t*>(pfG8);
  const pixel_t* pfB = reinterpret_cast<const pixel_t*>(pfB8);
  pixel_t* pfA = reinterpret_cast<pixel_t*>(pfA8);
  pitch /= sizeof(pixel_t);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      if (IsClose(pfB[x], B, tolB) && IsClose(pfG[x], G, tolG) && IsClose(pfR[x], R, tolR))
        pfA[x] = 0;
    }
    pfR += pitch;
    pfG += pitch;
    pfB += pitch;
    pfA += pitch;
  }
}

static void colorkeymask_planar_float_c(const BYTE* pfR8, const BYTE* pfG8, const BYTE* pfB8, BYTE* pfA8, int pitch, float R, float G, float B, int height, int width, float tolB, float tolG, float tolR) {
  typedef float pixel_t;
  const pixel_t* pfR = reinterpret_cast<const pixel_t*>(pfR8);
  const pixel_t* pfG = reinterpret_cast<const pixel_t*>(pfG8);
  const pixel_t* pfB = reinterpret_cast<const pixel_t*>(pfB8);
  pixel_t* pfA = reinterpret_cast<pixel_t*>(pfA8);
  pitch /= sizeof(pixel_t);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      if (IsCloseFloat(pfB[x], B, tolB) && IsCloseFloat(pfG[x], G, tolG) && IsCloseFloat(pfR[x], R, tolR))
        pfA[x] = 0;
    }
    pfR += pitch;
    pfG += pitch;
    pfB += pitch;
    pfA += pitch;
  }
}


PVideoFrame __stdcall ColorKeyMask::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame frame = child->GetFrame(n, env);
  env->MakeWritable(&frame);

  BYTE* pf = frame->GetWritePtr();
  const int pitch = frame->GetPitch();
  const int rowsize = frame->GetRowSize();

  if (vi.IsPlanarRGBA()) {
    const BYTE* pf_g = frame->GetReadPtr(PLANAR_G);
    const BYTE* pf_b = frame->GetReadPtr(PLANAR_B);
    const BYTE* pf_r = frame->GetReadPtr(PLANAR_R);
    BYTE* pf_a = frame->GetWritePtr(PLANAR_A);

    const int pitch = frame->GetPitch();
    const int width = vi.width;

    if (pixelsize == 1) {
      const int R = (color >> 16) & 0xff;
      const int G = (color >> 8) & 0xff;
      const int B = color & 0xff;
      colorkeymask_planar_c<uint8_t>(pf_r, pf_g, pf_b, pf_a, pitch, R, G, B, vi.height, width, tolB, tolG, tolR);
    }
    else if (pixelsize == 2) {
      const int R = (color64 >> 32) & 0xffff;
      const int G = (color64 >> 16) & 0xffff;
      const int B = color64 & 0xffff;
      colorkeymask_planar_c<uint16_t>(pf_r, pf_g, pf_b, pf_a, pitch, R, G, B, vi.height, width, tolB16, tolG16, tolR16);
    }
    else { // float
      const float R = ((color >> 16) & 0xff) / 255.0f;
      const float G = ((color >> 8) & 0xff) / 255.0f;
      const float B = (color & 0xff) / 255.0f;
      colorkeymask_planar_float_c(pf_r, pf_g, pf_b, pf_a, pitch, R, G, B, vi.height, width, tolB / 255.0f, tolG / 255.0f, tolR / 255.0f);
    }
  }
  else {
    // RGB32, RGB64
#ifdef INTEL_INTRINSICS
    if ((pixelsize == 1) && (env->GetCPUFlags() & CPUF_SSE2) && IsPtrAligned(pf, 16))
    {
      colorkeymask_sse2(pf, pitch, color, vi.height, rowsize, tolB, tolG, tolR);
    }
    else
#ifdef X86_32
      if ((pixelsize == 1) && (env->GetCPUFlags() & CPUF_MMX))
      {
        colorkeymask_mmx(pf, pitch, color, vi.height, rowsize, tolB, tolG, tolR);
      }
      else
#endif
#endif
      {
        if (pixelsize == 1) {
          const int R = (color >> 16) & 0xff;
          const int G = (color >> 8) & 0xff;
          const int B = color & 0xff;
          colorkeymask_c<uint8_t>(pf, pitch, R, G, B, vi.height, rowsize, tolB, tolG, tolR);
        }
        else {
          const int R = (color64 >> 32) & 0xffff;
          const int G = (color64 >> 16) & 0xffff;
          const int B = color64 & 0xffff;
          colorkeymask_c<uint16_t>(pf, pitch, R, G, B, vi.height, rowsize, tolB16, tolG16, tolR16);
        }
      }
  }

  return frame;
}

AVSValue __cdecl ColorKeyMask::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  enum { CHILD, COLOR, TOLERANCE_B, TOLERANCE_G, TOLERANCE_R };
  return new ColorKeyMask(args[CHILD].AsClip(),
    args[COLOR].AsInt(0),
    args[TOLERANCE_B].AsInt(10),
    args[TOLERANCE_G].AsInt(args[TOLERANCE_B].AsInt(10)),
    args[TOLERANCE_R].AsInt(args[TOLERANCE_B].AsInt(10)), env);
}


/********************************
 ******  ResetMask filter  ******
 ********************************/


ResetMask::ResetMask(PClip _child, float _mask_f, IScriptEnvironment* env)
  : GenericVideoFilter(_child)
{
  if (std::isnan(_mask_f))
    env->ThrowError("ResetMask: mask cannot be NaN");

  if (!(vi.IsRGB32() || vi.IsRGB64() || vi.IsPlanarRGBA() || vi.IsYUVA()))
    env->ThrowError("ResetMask: format has no alpha channel");

  // new: resetmask has parameter. If none->max transparency

  int max_pixel_value = (1 << vi.BitsPerComponent()) - 1;
  if (_mask_f < 0) {
    mask_f = 1.0f;
    mask = max_pixel_value;
  }
  else {
    mask_f = _mask_f;
    if (mask_f < 0) mask_f = 0;
    mask = (int)mask_f;

    mask = clamp(mask, 0, max_pixel_value);
    mask_f = clamp(mask_f, 0.0f, 1.0f);
  }
}


PVideoFrame ResetMask::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame f = child->GetFrame(n, env);
  env->MakeWritable(&f);

  if (vi.IsPlanarRGBA() || vi.IsYUVA()) {
    const int dst_rowsizeA = f->GetRowSize(PLANAR_A);
    const int dst_pitchA = f->GetPitch(PLANAR_A);
    BYTE* dstp_a = f->GetWritePtr(PLANAR_A);
    const int heightA = f->GetHeight(PLANAR_A);

    switch (vi.ComponentSize())
    {
    case 1:
      fill_plane<BYTE>(dstp_a, heightA, dst_rowsizeA, dst_pitchA, mask);
      break;
    case 2:
      fill_plane<uint16_t>(dstp_a, heightA, dst_rowsizeA, dst_pitchA, mask);
      break;
    case 4:
      fill_plane<float>(dstp_a, heightA, dst_rowsizeA, dst_pitchA, mask_f);
      break;
    }
    return f;
  }
  // RGB32 and RGB64

  BYTE* pf = f->GetWritePtr();
  int pitch = f->GetPitch();
  int rowsize = f->GetRowSize();
  int height = f->GetHeight();

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
        reinterpret_cast<uint16_t*>(pf)[x] = mask;
      }
      pf += pitch;
    }
  }

  return f;
}


AVSValue ResetMask::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  return new ResetMask(args[0].AsClip(), (float)args[1].AsFloat(-1.0f), env);
}


/********************************
 ******  Invert filter  ******
 ********************************/


Layer::Layer(PClip _child1, PClip _child2, const char _op[], int _lev, int _x, int _y,
  int _t, bool _chroma, float _opacity, int _placement, IScriptEnvironment* env)
  : child1(_child1), child2(_child2), Op(_op), levelB(_lev), ofsX(_x), ofsY(_y),
  chroma(_chroma), opacity(_opacity), placement(_placement)
{
  const VideoInfo& vi1 = child1->GetVideoInfo();
  const VideoInfo& vi2 = child2->GetVideoInfo();

  if (vi1.pixel_type != vi2.pixel_type && !vi1.IsSameColorspace(vi2)) // i420 and YV12 are matched OK
    env->ThrowError("Layer: image formats don't match");

  vi = vi1;

  hasAlpha = vi.IsRGB32() || vi.IsRGB64() || vi.IsYUVA() || vi.IsPlanarRGBA();
  bits_per_pixel = vi.BitsPerComponent();

  if (_t < 0 || _t > 255)
    env->ThrowError("Layer: threshold must be between 0 and 255");

  const bool levelSpecified = levelB >= 0;
  const bool strengthSpecified = opacity >= 0.0f;

  if (levelSpecified && strengthSpecified)
    env->ThrowError("Layer: cannot specify both level and opacity");
  if (levelSpecified && bits_per_pixel == 32)
    env->ThrowError("Layer: cannot specify level for 32 bit float format");

  if (levelSpecified)
  {
    if (hasAlpha)
      opacity = (float)levelB / ((1 << bits_per_pixel) + 1); // gives 1.0f for 257 (@8bit) and 65537 (@16 bits)
      // originally levelB was used in formula: (alpha*level + 1) / range_size,
      // now level is calculated from opacity as: level = opacity * ((1 << bits_per_pixel) + 1)
    else
      opacity = (float)levelB / ((1 << bits_per_pixel)); // YUY2 or other non-Alpha, gives 1.0f for 256 (@8bit)
    // we'll calculate back the level as: level = opacity * ((1 << bits_per_pixel))
  }
  else if (!strengthSpecified)
    opacity = 1.0f;

  if (vi.IsRGB32() || vi.IsRGB64() || vi.IsRGB24() || vi.IsRGB48())
    ofsY = static_cast<int>(std::clamp<int64_t>(int64_t(vi.height) - vi2.height - ofsY,
                                              INT_MIN, INT_MAX)); // packed RGB is upside down
  else if ((vi.IsYUV() || vi.IsYUVA()) && !vi.IsY()) {
    // make offsets subsampling friendly
    // e.g. for YUY2: ofsX = ofsX & 0xFFFFFFFE; // X offset for YUY2 must be aligned on even pixels
    ofsX = ofsX & ~((1 << vi.GetPlaneWidthSubsampling(PLANAR_U)) - 1);
    ofsY = ofsY & ~((1 << vi.GetPlaneHeightSubsampling(PLANAR_U)) - 1);
  }

  cp_overlap overlap{};
  avs_composite::Check(cp_intersect(vi.width, vi.height, vi2.width, vi2.height, ofsX, ofsY, &overlap), env);
  xdest = overlap.base_x; ydest = overlap.base_y;
  xsrc = overlap.source_x; ysrc = overlap.source_y;
  xcount = overlap.width; ycount = overlap.height;

  if (!(!lstrcmpi(Op, "Mul") || !lstrcmpi(Op, "Add") || !lstrcmpi(Op, "Fast") ||
    !lstrcmpi(Op, "Subtract") || !lstrcmpi(Op, "Lighten") || !lstrcmpi(Op, "Darken")))
    env->ThrowError("Layer supports the following ops: Fast, Lighten, Darken, Add, Subtract, Mul");

  if (!chroma)
  {
    if (!lstrcmpi(Op, "Darken")) env->ThrowError("Layer: monochrome darken illegal op");
    if (!lstrcmpi(Op, "Lighten")) env->ThrowError("Layer: monochrome lighten illegal op");
    if (!lstrcmpi(Op, "Fast")) env->ThrowError("Layer: this mode not allowed in FAST; use ADD instead");
  }

  // autoscale ThresholdParam from 8 bit base
  // todo check validity
  if (bits_per_pixel == 32)
    ThresholdParam = _t; // n/a
  else
    ThresholdParam = _t << (bits_per_pixel - 8);
  ThresholdParam_f = _t / 255.0f;

  overlay_frames = vi2.num_frames;
}

PVideoFrame __stdcall Layer::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame base = child1->GetFrame(n, env);
  if (xcount <= 0 || ycount <= 0) return base;
  PVideoFrame source = child2->GetFrame(min(n, overlay_frames - 1), env);
  env->MakeWritable(&base);
  using avs_composite::LayerOperation;
  const auto operation = !lstrcmpi(Op, "Mul") ? LayerOperation::Multiply
                       : !lstrcmpi(Op, "Subtract") ? LayerOperation::Subtract
                       : !lstrcmpi(Op, "Fast") ? LayerOperation::Fast
                       : !lstrcmpi(Op, "Lighten") ? LayerOperation::Lighten
                       : !lstrcmpi(Op, "Darken") ? LayerOperation::Darken : LayerOperation::Add;
  avs_composite::LayerFrame(base, source, vi, child2->GetVideoInfo(),
                            {xdest, ydest, xsrc, ysrc, xcount, ycount}, operation, chroma, hasAlpha,
                            opacity, bits_per_pixel == 32 ? ThresholdParam_f : ThresholdParam,
                            placement == PLACEMENT_MPEG1 ? CP_CENTER : CP_MPEG2, env);
  return base;
}


AVSValue __cdecl Layer::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  const VideoInfo& vi1 = args[0].AsClip()->GetVideoInfo();
  const VideoInfo& vi2 = args[1].AsClip()->GetVideoInfo();

  // convert old RGB format to planar RGB
  PClip clip1;
  if (vi1.IsRGB24() || vi1.IsRGB48()) {
    AVSValue new_args[1] = { args[0].AsClip() };
    clip1 = env->Invoke("ConvertToPlanarRGB", AVSValue(new_args, 1)).AsClip();
  }
  /* formats handled by Layer core
  else if (vi1.IsRGB32() || vi1.IsRGB64()) {
    AVSValue new_args[1] = { args[0].AsClip() };
    clip1 = env->Invoke("ConvertToPlanarRGBA", AVSValue(new_args, 1)).AsClip();
  }
  else if (vi1.IsYUY2()) {
    AVSValue new_args[1] = { args[0].AsClip() };
    clip1 = env->Invoke("ConvertToYV16", AVSValue(new_args, 1)).AsClip();
  }
  */
  else {
    clip1 = args[0].AsClip();
  }

  PClip clip2;
  if (vi2.IsRGB24() || vi2.IsRGB48()) {
    AVSValue new_args[1] = { args[1].AsClip() };
    clip2 = env->Invoke("ConvertToPlanarRGB", AVSValue(new_args, 1)).AsClip();
  }
  /* formats handled by Layer core
  else if (vi2.IsRGB32() || vi2.IsRGB64()) {
    AVSValue new_args[1] = { args[1].AsClip() };
    clip2 = env->Invoke("ConvertToPlanarRGBA", AVSValue(new_args, 1)).AsClip();
  }
  else if (vi_orig.IsYUY2()) {
    AVSValue new_args[1] = { args[1].AsClip() };
    clip2 = env->Invoke("ConvertToYV16", AVSValue(new_args, 1)).AsClip();
  }
  */
  else {
    clip2 = args[1].AsClip();
  }

  Layer* Result = new Layer(clip1, clip2, args[2].AsString("Add"), args[3].AsInt(-1),
    args[4].AsInt(0), args[5].AsInt(0), args[6].AsInt(0), args[7].AsBool(true),
    args[8].AsFloatf(-1.0f), // opacity
    getPlacement(args[9], env), // chroma placement
    env);

  if (vi1.IsRGB24()) {
    AVSValue new_args2[1] = { Result };
    return env->Invoke("ConvertToRGB24", AVSValue(new_args2, 1)).AsClip();
  }
  else if (vi1.IsRGB48()) {
    AVSValue new_args2[1] = { Result };
    return env->Invoke("ConvertToRGB48", AVSValue(new_args2, 1)).AsClip();
  }
  /* formats handled by Layer core
  else if (vi1.IsRGB32()) {
    AVSValue new_args2[1] = { Result };
    return env->Invoke("ConvertToRGB32", AVSValue(new_args2, 1)).AsClip();
  }
  else if (vi1.IsRGB64()) {
    AVSValue new_args2[1] = { Result };
    return env->Invoke("ConvertToRGB64", AVSValue(new_args2, 1)).AsClip();
  }
  else if (vi1.IsYUY2()) {
    AVSValue new_args2[1] = { Result };
    return env->Invoke("ConvertToYUY2", AVSValue(new_args2, 1)).AsClip();
  }
  */

  return Result;

}



/**********************************
 *******   Subtract Filter   ******
 *********************************/
bool Subtract::DiffFlag = false;
BYTE Subtract::LUT_Diff8[513];

Subtract::Subtract(PClip _child1, PClip _child2, IScriptEnvironment* env)
  : child1(_child1), child2(_child2)
{
  VideoInfo vi1 = child1->GetVideoInfo();
  VideoInfo vi2 = child2->GetVideoInfo();

  if (vi1.width != vi2.width || vi1.height != vi2.height)
    env->ThrowError("Subtract: image dimensions don't match");

  if (!(vi1.IsSameColorspace(vi2)))
    env->ThrowError("Subtract: image formats don't match");

  vi = vi1;
  vi.num_frames = max(vi1.num_frames, vi2.num_frames);
  vi.num_audio_samples = max(vi1.num_audio_samples, vi2.num_audio_samples);

  pixelsize = vi.ComponentSize();
  bits_per_pixel = vi.BitsPerComponent();

  if (!DiffFlag) { // Init the global Diff table
    DiffFlag = true;
    for (int i = 0; i <= 512; i++) LUT_Diff8[i] = max(0, min(255, i - 129));
    // 0 ..  129  130 131   ... 255 256 257 258     384 ... 512
    // 0 ..   0    1   2  3 ... 126 127 128 129 ... 255 ... 255
  }
}

template<typename pixel_t, int midpixel, bool chroma>
static void subtract_plane(BYTE* src1p, const BYTE* src2p, int src1_pitch, int src2_pitch, int width, int height, int bits_per_pixel)
{
  typedef typename std::conditional < sizeof(pixel_t) == 4, float, int>::type limits_t;

  const limits_t limit_lo = sizeof(pixel_t) <= 2 ? 0 : (limits_t)(chroma ? uv8tof(0) : c8tof(0));
  const limits_t limit_hi = sizeof(pixel_t) == 1 ? 255 : sizeof(pixel_t) == 2 ? ((1 << bits_per_pixel) - 1) : (limits_t)(chroma ? uv8tof(255) : c8tof(255));
  const limits_t equal_luma = sizeof(pixel_t) == 1 ? midpixel : sizeof(pixel_t) == 2 ? (midpixel << (bits_per_pixel - 8)) : (limits_t)(chroma ? uv8tof(midpixel) : c8tof(midpixel));
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      reinterpret_cast<pixel_t*>(src1p)[x] =
        (pixel_t)clamp(
          (limits_t)(reinterpret_cast<pixel_t*>(src1p)[x] - reinterpret_cast<const pixel_t*>(src2p)[x] + equal_luma), // 126: luma of equality
          limit_lo,
          limit_hi);
    }
    src1p += src1_pitch;
    src2p += src2_pitch;
  }
}

PVideoFrame __stdcall Subtract::GetFrame(int n, IScriptEnvironment* env)
{
  int n1 = min(max(0, n), child1->GetVideoInfo().num_frames - 1);
  int n2 = min(max(0, n), child2->GetVideoInfo().num_frames - 1);
  PVideoFrame src1 = child1->GetFrame(n1, env);
  PVideoFrame src2 = child2->GetFrame(n2, env);

  env->MakeWritable(&src1);

  BYTE* src1p = src1->GetWritePtr();
  const BYTE* src2p = src2->GetReadPtr();
  int row_size = src1->GetRowSize();
  int src1_pitch = src1->GetPitch();
  int src2_pitch = src2->GetPitch();

  int width = row_size / pixelsize;
  int height = vi.height;

  if (vi.IsPlanar() && (vi.IsYUV() || vi.IsYUVA())) {
    // alpha
    if (pixelsize == 1) {
      // LUT is a bit faster than clamp version
      for (int y = 0; y < vi.height; y++) {
        for (int x = 0; x < row_size; x++) {
          src1p[x] = LUT_Diff8[src1p[x] - src2p[x] + 126 + 129];
        }
        src1p += src1->GetPitch();
        src2p += src2->GetPitch();
      }
    }
    else if (pixelsize == 2)
      subtract_plane<uint16_t, 126, false>(src1p, src2p, src1_pitch, src2_pitch, width, height, bits_per_pixel);
    else //if (pixelsize==4)
      subtract_plane<float, 126, false>(src1p, src2p, src1_pitch, src2_pitch, width, height, bits_per_pixel);

    // chroma
    row_size = src1->GetRowSize(PLANAR_U);
    if (row_size) {
      width = row_size / pixelsize;
      height = src1->GetHeight(PLANAR_U);
      src1_pitch = src1->GetPitch(PLANAR_U);
      src2_pitch = src2->GetPitch(PLANAR_U);
      // U_plane exists
      BYTE* src1p = src1->GetWritePtr(PLANAR_U);
      const BYTE* src2p = src2->GetReadPtr(PLANAR_U);
      BYTE* src1pV = src1->GetWritePtr(PLANAR_V);
      const BYTE* src2pV = src2->GetReadPtr(PLANAR_V);

      if (pixelsize == 1) {
        // LUT is a bit faster than clamp version
        for (int y = 0; y < height; y++) {
          for (int x = 0; x < width; x++) {
            src1p[x] = LUT_Diff8[src1p[x] - src2p[x] + 128 + 129];
            src1pV[x] = LUT_Diff8[src1pV[x] - src2pV[x] + 128 + 129];
          }
          src1p += src1_pitch;
          src2p += src2_pitch;
          src1pV += src1_pitch;
          src2pV += src2_pitch;
        }
      }
      else if (pixelsize == 2) {
        subtract_plane<uint16_t, 128, true>(src1p, src2p, src1_pitch, src2_pitch, width, height, bits_per_pixel);
        subtract_plane<uint16_t, 128, true>(src1pV, src2pV, src1_pitch, src2_pitch, width, height, bits_per_pixel);
      }
      else { //if (pixelsize==4)
        subtract_plane<float, 128, true>(src1p, src2p, src1_pitch, src2_pitch, width, height, bits_per_pixel);
        subtract_plane<float, 128, true>(src1pV, src2pV, src1_pitch, src2_pitch, width, height, bits_per_pixel);
      }
    }
    return src1;
  } // End planar YUV

  // For YUY2, 50% gray is about (126,128,128) instead of (128,128,128).  Grr...
  if (vi.IsYUY2()) {
    for (int y = 0; y < vi.height; ++y) {
      for (int x = 0; x < row_size; x += 2) {
        src1p[x] = LUT_Diff8[src1p[x] - src2p[x] + 126 + 129];
        src1p[x + 1] = LUT_Diff8[src1p[x + 1] - src2p[x + 1] + 128 + 129];
      }
      src1p += src1->GetPitch();
      src2p += src2->GetPitch();
    }
  }
  else { // RGB
    if (vi.IsPlanarRGB() || vi.IsPlanarRGBA()) {
      const int planesRGB[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };

      // do not diff Alpha
      for (int p = 0; p < 3; p++) {
        const int plane = planesRGB[p];
        src1p = src1->GetWritePtr(plane);
        src2p = src2->GetReadPtr(plane);
        src1_pitch = src1->GetPitch(plane);
        src2_pitch = src2->GetPitch(plane);
        if (pixelsize == 1)
          subtract_plane<uint8_t, 128, false>(src1p, src2p, src1_pitch, src2_pitch, width, height, bits_per_pixel);
        else if (pixelsize == 2)
          subtract_plane<uint16_t, 128, false>(src1p, src2p, src1_pitch, src2_pitch, width, height, bits_per_pixel);
        else
          subtract_plane<float, 128, false>(src1p, src2p, src1_pitch, src2_pitch, width, height, bits_per_pixel);
      }
    }
    else { // packed RGB
      if (pixelsize == 1) {
        for (int y = 0; y < vi.height; ++y) {
          for (int x = 0; x < row_size; ++x)
            src1p[x] = LUT_Diff8[src1p[x] - src2p[x] + 128 + 129];

          src1p += src1->GetPitch();
          src2p += src2->GetPitch();
        }
      }
      else { // pixelsize == 2: RGB48, RGB64
        // width is getrowsize based here: ok.
        subtract_plane<uint16_t, 128, false>(src1p, src2p, src1_pitch, src2_pitch, width, height, bits_per_pixel);
      }
    }
  }
  return src1;
}



AVSValue __cdecl Subtract::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  return new Subtract(args[0].AsClip(), args[1].AsClip(), env);
}
