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
  { "Invert",       BUILTIN_FUNC_PREFIX, "c[channels]s", Invert::Create },
  { "ShowAlpha",    BUILTIN_FUNC_PREFIX, "c[pixel_type]s", ShowChannel::Create, (void*)3 }, // AVS+ also for YUVA, PRGBA
  { "ShowRed",      BUILTIN_FUNC_PREFIX, "c[pixel_type]s", ShowChannel::Create, (void*)2 },
  { "ShowGreen",    BUILTIN_FUNC_PREFIX, "c[pixel_type]s", ShowChannel::Create, (void*)1 },
  { "ShowBlue",     BUILTIN_FUNC_PREFIX, "c[pixel_type]s", ShowChannel::Create, (void*)0 },
  { "ShowY",        BUILTIN_FUNC_PREFIX, "c[pixel_type]s", ShowChannel::Create, (void*)4 }, // AVS+
  { "ShowU",        BUILTIN_FUNC_PREFIX, "c[pixel_type]s", ShowChannel::Create, (void*)5 }, // AVS+
  { "ShowV",        BUILTIN_FUNC_PREFIX, "c[pixel_type]s", ShowChannel::Create, (void*)6 }, // AVS+
  { "MergeRGB",     BUILTIN_FUNC_PREFIX, "ccc[pixel_type]s", MergeRGB::Create, (void*)0 },
  { "MergeARGB",    BUILTIN_FUNC_PREFIX, "cccc[pixel_type]s", MergeRGB::Create, (void*)1 },
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


Invert::Invert(PClip _child, const char* _channels, IScriptEnvironment* env)
  : GenericVideoFilter(_child)
{
  doB = doG = doR = doA = doY = doU = doV = false;

  for (int k = 0; _channels[k] != '\0'; ++k) {
    switch (_channels[k]) {
    case 'B':
    case 'b':
      doB = true;
      break;
    case 'G':
    case 'g':
      doG = true;
      break;
    case 'R':
    case 'r':
      doR = true;
      break;
    case 'A':
    case 'a':
      doA = (vi.NumComponents() > 3);
      break;
    case 'Y':
    case 'y':
      doY = true;
      break;
    case 'U':
    case 'u':
      doU = (vi.NumComponents() > 1);
      break;
    case 'V':
    case 'v':
      doV = (vi.NumComponents() > 1);
      break;
    default:
      break;
    }
  }
  pixelsize = vi.ComponentSize();
  bits_per_pixel = vi.BitsPerComponent();
  if (vi.IsYUY2()) {
    mask = doY ? 0x00ff00ff : 0;
    mask |= doU ? 0x0000ff00 : 0;
    mask |= doV ? 0xff000000 : 0;
  }
  else if (vi.IsRGB32()) {
    mask = doB ? 0x000000ff : 0;
    mask |= doG ? 0x0000ff00 : 0;
    mask |= doR ? 0x00ff0000 : 0;
    mask |= doA ? 0xff000000 : 0;
  }
  else if (vi.IsRGB64()) {
    mask64 = doB ? 0x000000000000ffffull : 0;
    mask64 |= (doG ? 0x00000000ffff0000ull : 0);
    mask64 |= (doR ? 0x0000ffff00000000ull : 0);
    mask64 |= (doA ? 0xffff000000000000ull : 0);
  }
  else {
    mask = 0xffffffff;
    mask64 = (1 << bits_per_pixel) - 1;
    mask64 |= (mask64 << 48) | (mask64 << 32) | (mask64 << 16); // works for 10 bit, too
    // RGB24/48 is special case no use of this mask
  }
}


//mod4 width is required
static void invert_frame_c(BYTE* frame, int pitch, int width, int height, int mask) {
  for (int y = 0; y < height; ++y) {
    int* intptr = reinterpret_cast<int*>(frame);

    for (int x = 0; x < width / 4; ++x) {
      intptr[x] = intptr[x] ^ mask;
    }
    frame += pitch;
  }
}

static void invert_frame_uint16_c(BYTE* frame, int pitch, int width, int height, uint64_t mask64) {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width / 8; ++x) {
      reinterpret_cast<uint64_t*>(frame)[x] = reinterpret_cast<uint64_t*>(frame)[x] ^ mask64;
    }
    frame += pitch;
  }
}

static void invert_plane_c(BYTE* frame, int pitch, int row_size, int height) {
  int mod4_width = row_size / 4 * 4;
  for (int y = 0; y < height; ++y) {
    int* intptr = reinterpret_cast<int*>(frame);

    for (int x = 0; x < mod4_width / 4; ++x) {
      intptr[x] = intptr[x] ^ 0xFFFFFFFF;
    }

    for (int x = mod4_width; x < row_size; ++x) {
      frame[x] = frame[x] ^ 255;
    }
    frame += pitch;
  }
}

static void invert_plane_uint16_c(BYTE* frame, int pitch, int row_size, int height, uint64_t mask64) {
  int mod8_width = row_size / 8 * 8;
  uint16_t mask16 = mask64 & 0xFFFF; // for planes, all 16 bit parts of 64 bit mask is the same
  for (int y = 0; y < height; ++y) {

    for (int x = 0; x < mod8_width / 8; ++x) {
      reinterpret_cast<uint64_t*>(frame)[x] ^= mask64;
    }

    for (int x = mod8_width; x < row_size; ++x) {
      reinterpret_cast<uint16_t*>(frame)[x] ^= mask16;
    }
    frame += pitch;
  }
}

static void invert_plane_float_c(BYTE* frame, int pitch, int row_size, int height, bool chroma) {
  const int width = row_size / sizeof(float);
#ifdef FLOAT_CHROMA_IS_HALF_CENTERED
  const float max = 1.0f;
#else
  const float max = chroma ? 0.0f : 1.0f;
#endif
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      reinterpret_cast<float*>(frame)[x] = max - reinterpret_cast<float*>(frame)[x];
    }
    frame += pitch;
  }
}

static void invert_frame(BYTE* frame, int pitch, int rowsize, int height, int mask, uint64_t mask64, int pixelsize, IScriptEnvironment* env) {
#ifdef INTEL_INTRINSICS
  if ((pixelsize == 1 || pixelsize == 2) && (env->GetCPUFlags() & CPUF_SSE2) && IsPtrAligned(frame, 16))
  {
    if (pixelsize == 1)
      invert_frame_sse2(frame, pitch, rowsize, height, mask);
    else
      invert_frame_uint16_sse2(frame, pitch, rowsize, height, mask64);
  }
#ifdef X86_32
  else if ((pixelsize == 1) && (env->GetCPUFlags() & CPUF_MMX))
  {
    invert_frame_mmx(frame, pitch, rowsize, height, mask);
  }
#endif
  else
#endif
  {
    if (pixelsize == 1)
      invert_frame_c(frame, pitch, rowsize, height, mask);
    else
      invert_frame_uint16_c(frame, pitch, rowsize, height, mask64);
  }
}

static void invert_plane(BYTE* frame, int pitch, int rowsize, int height, int pixelsize, uint64_t mask64, bool chroma, IScriptEnvironment* env) {
#ifdef INTEL_INTRINSICS
  if ((pixelsize == 1 || pixelsize == 2) && (env->GetCPUFlags() & CPUF_SSE2) && IsPtrAligned(frame, 16))
  {
    if (pixelsize == 1)
      invert_frame_sse2(frame, pitch, rowsize, height, 0xffffffff);
    else if (pixelsize == 2)
      invert_frame_uint16_sse2(frame, pitch, rowsize, height, mask64);
  }
#ifdef X86_32
  else if ((pixelsize == 1) && (env->GetCPUFlags() & CPUF_MMX))
  {
    invert_plane_mmx(frame, pitch, rowsize, height);
  }
#endif
  else
#endif
  {
    if (pixelsize == 1)
      invert_plane_c(frame, pitch, rowsize, height);
    else if (pixelsize == 2)
      invert_plane_uint16_c(frame, pitch, rowsize, height, mask64);
    else {
      invert_plane_float_c(frame, pitch, rowsize, height, chroma);
    }
  }
}

PVideoFrame Invert::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame f = child->GetFrame(n, env);

  env->MakeWritable(&f);

  BYTE* pf = f->GetWritePtr();
  int pitch = f->GetPitch();
  int rowsize = f->GetRowSize();
  int height = f->GetHeight();

  if (vi.IsPlanar()) {
    // planar YUV
    if (vi.IsYUV() || vi.IsYUVA()) {
      if (doY)
        invert_plane(pf, pitch, f->GetRowSize(PLANAR_Y_ALIGNED), height, pixelsize, mask64, false, env);
      if (doU)
        invert_plane(f->GetWritePtr(PLANAR_U), f->GetPitch(PLANAR_U), f->GetRowSize(PLANAR_U_ALIGNED), f->GetHeight(PLANAR_U), pixelsize, mask64, true, env);
      if (doV)
        invert_plane(f->GetWritePtr(PLANAR_V), f->GetPitch(PLANAR_V), f->GetRowSize(PLANAR_V_ALIGNED), f->GetHeight(PLANAR_V), pixelsize, mask64, true, env);
    }
    // planar RGB
    if (vi.IsPlanarRGB() || vi.IsPlanarRGBA()) {
      if (doG) // first plane, GetWritePtr w/o parameters
        invert_plane(pf, pitch, f->GetRowSize(PLANAR_G_ALIGNED), height, pixelsize, mask64, false, env);
      if (doB)
        invert_plane(f->GetWritePtr(PLANAR_B), f->GetPitch(PLANAR_B), f->GetRowSize(PLANAR_B_ALIGNED), f->GetHeight(PLANAR_B), pixelsize, mask64, false, env);
      if (doR)
        invert_plane(f->GetWritePtr(PLANAR_R), f->GetPitch(PLANAR_R), f->GetRowSize(PLANAR_R_ALIGNED), f->GetHeight(PLANAR_R), pixelsize, mask64, false, env);
    }
    // alpha
    if (doA && (vi.IsPlanarRGBA() || vi.IsYUVA()))
      invert_plane(f->GetWritePtr(PLANAR_A), f->GetPitch(PLANAR_A), f->GetRowSize(PLANAR_A_ALIGNED), f->GetHeight(PLANAR_A), pixelsize, mask64, false, env);
  }
  else if (vi.IsYUY2() || vi.IsRGB32() || vi.IsRGB64()) {
    invert_frame(pf, pitch, rowsize, height, mask, mask64, pixelsize, env);
  }
  else if (vi.IsRGB24()) {
    int rMask = doR ? 0xff : 0;
    int gMask = doG ? 0xff : 0;
    int bMask = doB ? 0xff : 0;
    for (int i = 0; i < height; i++) {

      for (int j = 0; j < rowsize; j += 3) {
        pf[j + 0] = pf[j + 0] ^ bMask;
        pf[j + 1] = pf[j + 1] ^ gMask;
        pf[j + 2] = pf[j + 2] ^ rMask;
      }
      pf += pitch;
    }
  }
  else if (vi.IsRGB48()) {
    int rMask = doR ? 0xffff : 0;
    int gMask = doG ? 0xffff : 0;
    int bMask = doB ? 0xffff : 0;
    for (int i = 0; i < height; i++) {
      for (int j = 0; j < rowsize / pixelsize; j += 3) {
        reinterpret_cast<uint16_t*>(pf)[j + 0] ^= bMask;
        reinterpret_cast<uint16_t*>(pf)[j + 1] ^= gMask;
        reinterpret_cast<uint16_t*>(pf)[j + 2] ^= rMask;
      }
      pf += pitch;
    }
  }

  return f;
}


AVSValue Invert::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  return new Invert(args[0].AsClip(), args[0].AsClip()->GetVideoInfo().IsRGB() ? args[1].AsString("RGBA") : args[1].AsString("YUVA"), env);
}


/**********************************
 ******  ShowChannel filter  ******
 **********************************/


ShowChannel::ShowChannel(PClip _child, const char* pixel_type, int _channel, IScriptEnvironment* env)
  : GenericVideoFilter(_child), channel(_channel), input_type(_child->GetVideoInfo().pixel_type),
  pixelsize(_child->GetVideoInfo().ComponentSize()), bits_per_pixel(_child->GetVideoInfo().BitsPerComponent())
{
  static const char* const ShowText[7] = { "Blue", "Green", "Red", "Alpha", "Y", "U", "V" };

  input_type_is_packed_rgb = vi.IsRGB() && !vi.IsPlanar();
  input_type_is_planar_rgb = vi.IsPlanarRGB();
  input_type_is_planar_rgba = vi.IsPlanarRGBA();
  input_type_is_yuva = vi.IsYUVA();
  input_type_is_yuv = vi.IsYUV() && vi.IsPlanar();
  input_type_is_planar = vi.IsPlanar();

  int orig_channel = channel;

  // A channel
  if ((channel == 3) && !vi.IsRGB32() && !vi.IsRGB64() && !vi.IsPlanarRGBA() && !vi.IsYUVA())
    env->ThrowError("ShowAlpha: RGB32, RGB64, Planar RGBA or YUVA data only");

  // R, G, B channel
  if ((channel >= 0) && (channel <= 2) && !vi.IsRGB())
    env->ThrowError("Show%s: plane is valid only with RGB or planar RGB(A) source", ShowText[channel]);

  // Y, U, V channel (4,5,6)
  if ((channel >= 4) && (channel <= 6)) {
    if (!vi.IsYUV() && !vi.IsYUVA())
      env->ThrowError("Show%s: plane is valid only with YUV(A) source", ShowText[channel]);
    if (channel != 4 && vi.IsY())
      env->ThrowError("Show%s: invalid plane for greyscale source", ShowText[channel]);
    channel -= 4; // map to 0,1,2
  }

  int target_bits_per_pixel;

  const int orig_width = vi.width;
  const int orig_height = vi.height;

  if (input_type_is_yuv || input_type_is_yuva)
  {
    if (channel == 1 || channel == 2) // U or V: target can be smaller than Y
    {
      vi.width >>= vi.GetPlaneWidthSubsampling(PLANAR_U);
      vi.height >>= vi.GetPlaneHeightSubsampling(PLANAR_U);
    }
  }

  const bool empty_pixel_type = pixel_type == nullptr || *pixel_type == 0;

  if (!lstrcmpi(pixel_type, "rgb") || (vi.IsRGB() && empty_pixel_type)) {
    // target is RGB, rgb (packed) is adaptively 32 or 64 bits
    //                rgb (planar) is of any bit depths
    if (vi.IsPlanar()) {
      // YUV, planar RGB or Y
      // always alphaless planar RGB output
      switch (bits_per_pixel) {
      case 8: vi.pixel_type = VideoInfo::CS_RGBP8; break;
      case 10: vi.pixel_type = VideoInfo::CS_RGBP10; break;
      case 12: vi.pixel_type = VideoInfo::CS_RGBP12; break;
      case 14: vi.pixel_type = VideoInfo::CS_RGBP14; break;
      case 16: vi.pixel_type = VideoInfo::CS_RGBP16; break;
      case 32: vi.pixel_type = VideoInfo::CS_RGBPS; break;
      }
    }
    else if (vi.IsRGB()) {
      // packed RGB source
      switch (bits_per_pixel) {
      case 8: vi.pixel_type = VideoInfo::CS_BGR32; break; // bit-depth adaptive
      case 16: vi.pixel_type = VideoInfo::CS_BGR64; break;
      default: env->ThrowError("Show%s: source must be 8 or 16 bits", ShowText[orig_channel]);
      }
    }
    else {
      env->ThrowError("Show%s: unsupported source format", ShowText[orig_channel]);
    }
    target_bits_per_pixel = bits_per_pixel;
  }
  else if (!lstrcmpi(pixel_type, "yuv") || ((vi.IsYUV() || vi.IsYUVA()) && empty_pixel_type)) {
    // target is YUV, rgb (packed) is adaptively 32 or 64 bits
    //                rgb (planar) is of any bit depths
    //                YUV,Y: 420
      // RGB source, when only 'yuv' is given, convert to 444
    switch (bits_per_pixel) {
    case 8: vi.pixel_type = VideoInfo::CS_YV24; break;
    case 10: vi.pixel_type = VideoInfo::CS_YUV444P10; break;
    case 12: vi.pixel_type = VideoInfo::CS_YUV444P12; break;
    case 14: vi.pixel_type = VideoInfo::CS_YUV444P14; break;
    case 16: vi.pixel_type = VideoInfo::CS_YUV444P16; break;
    case 32: vi.pixel_type = VideoInfo::CS_YUV444PS; break;
    }
    target_bits_per_pixel = bits_per_pixel;
  }
  else {
    // explicitely given output pixel type
    
    // first try
    // Append bit depth and check
    std::string format = pixel_type;
    // RGBP --> RGBPS, Y -> Y16, YUV420 -> YUV420P10
    if (!lstrcmpi(pixel_type, "y")) {
      format = format + std::to_string(bits_per_pixel); // Y8..Y16, also Y32
    }
    else if (!lstrcmpi(pixel_type, "rgbp") || !lstrcmpi(pixel_type, "rgbap")) {
      if (bits_per_pixel == 32)
        format = format + "S"; // RGBAPS
      else
        format = format + std::to_string(bits_per_pixel); // RGBP16
    }
    else {
      // hopefully like "yuv420" or "yuva444"
      if (bits_per_pixel == 32)
        format = format + "PS"; // YUV420PS
      else
        format = format + "P" + std::to_string(bits_per_pixel); // YUV420P16
    }

    int new_pixel_type = GetPixelTypeFromName(format.c_str());
    if (new_pixel_type == VideoInfo::CS_UNKNOWN) {
      new_pixel_type = GetPixelTypeFromName(pixel_type);
      if (new_pixel_type == VideoInfo::CS_UNKNOWN)
        env->ThrowError("Show%s: invalid pixel_type!", ShowText[orig_channel]);
    }
    // new output format
    vi.pixel_type = new_pixel_type;

    if (vi.IsYUY2()) {
      if (vi.width & 1) {
        env->ThrowError("Show%s: width must be mod 2 for yuy2", ShowText[orig_channel]);
      }
    }
    if (vi.Is420()) {
      if (vi.width & 1) {
        env->ThrowError("Show%s: width must be mod 2 for 4:2:0 target", ShowText[orig_channel]);
      }
      if (vi.height & 1) {
        env->ThrowError("Show%s: height must be mod 2 for 4:2:0 target", ShowText[orig_channel]);
      }
    }
    if (vi.Is422()) {
      if (vi.width & 1) {
        env->ThrowError("Show%s: width must be mod 2 for 4:2:2 target", ShowText[orig_channel]);
      }
    }
    if (vi.IsYV411()) {
      if (vi.width & 3) {
        env->ThrowError("Show%s: width must be mod 4 for 4:1:1 target", ShowText[orig_channel]);
      }
    }

    target_bits_per_pixel = vi.BitsPerComponent();
  }

  if (target_bits_per_pixel != bits_per_pixel)
    env->ThrowError("Show%s: source bit depth must be %d for %s", ShowText[orig_channel], target_bits_per_pixel, pixel_type);

  target_hasalpha = vi.IsRGB32() || vi.IsRGB64() || vi.IsPlanarRGBA() || vi.IsYUVA();
  source_hasalpha = input_type == VideoInfo::CS_BGR32 || input_type == VideoInfo::CS_BGR64 || input_type_is_planar_rgba || input_type_is_yuva;

  if (target_hasalpha && source_hasalpha && (vi.width != orig_width || vi.height != orig_height)) {
    env->ThrowError("Show%s: subsampled source plane and alpha-aware source and destination format: alpha dimensions must be the same", ShowText[orig_channel]);
  }

}


template<typename pixel_t, bool source_hasalpha, bool target_hasalpha>
static void planar_to_packedrgb(uint8_t* dstp, int dst_pitch, const uint8_t* srcp, const uint8_t* srcp_a, int src_pitch, int width, int height)
{
  // packed RGB is upside-down
  dstp += (height - 1) * dst_pitch;
  constexpr int target_rgb_step = target_hasalpha ? 4 : 3;
  constexpr int max_pixel_value = sizeof(pixel_t) == 1 ? 255 : 65535;

  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; j++) {
      pixel_t* curr_dstp = reinterpret_cast<pixel_t*>(dstp);
      curr_dstp[j * target_rgb_step + 0] = curr_dstp[j * target_rgb_step + 1] = curr_dstp[j * target_rgb_step + 2] = reinterpret_cast<const pixel_t*>(srcp)[j];
      if constexpr(target_hasalpha) {
        const int alpha = source_hasalpha ? reinterpret_cast<const pixel_t*>(srcp_a)[j] : max_pixel_value;
        curr_dstp[j * target_rgb_step + 3] = alpha;
      }
    }
    srcp += src_pitch;
    if constexpr(source_hasalpha)
      srcp_a += src_pitch; // alpha has the same pitch
    dstp -= dst_pitch;
  }
}

template<typename pixel_t, bool source_hasalpha, bool target_hasalpha>
static void packed_to_packedrgb(uint8_t* dstp, int dst_pitch, const uint8_t* srcp, int pitch, int width, int height, int channel)
{
  constexpr int target_rgb_step = target_hasalpha ? 4 : 3;
  constexpr int source_rgb_step = source_hasalpha ? 4 : 3;
  constexpr int max_pixel_value = sizeof(pixel_t) == 1 ? 255 : 65535;

  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; j++) {
      pixel_t* curr_dstp = reinterpret_cast<pixel_t*>(dstp);
      const int pixel = reinterpret_cast<const pixel_t*>(srcp)[j * source_rgb_step + channel];
      curr_dstp[j * target_rgb_step + 0] = curr_dstp[j * target_rgb_step + 1] = curr_dstp[j * target_rgb_step + 2] = pixel;
      if constexpr(target_hasalpha) {
        const int alpha = source_hasalpha ? reinterpret_cast<const pixel_t*>(srcp)[j * source_rgb_step + 3] : max_pixel_value;
        curr_dstp[j * target_rgb_step + 3] = alpha; // alpha
      }
    }
    srcp += pitch;
    dstp += dst_pitch;
  }
}

template<typename pixel_t, bool source_hasalpha, bool target_hasalpha>
static void packed_to_planarrgb(uint8_t* dstp_r, uint8_t* dstp_g, uint8_t* dstp_b, uint8_t* dstp_a, int dst_pitch, const uint8_t* srcp, int src_pitch, int width, int height, int channel)
{
  constexpr int source_rgb_step = source_hasalpha ? 4 : 3;
  constexpr int max_pixel_value = sizeof(pixel_t) == 1 ? 255 : 65535;

  // packed RGB is upside-down
  srcp += (height - 1) * src_pitch;

  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      const int pixel = reinterpret_cast<const pixel_t*>(srcp)[j * source_rgb_step + channel];
      reinterpret_cast<pixel_t*>(dstp_g)[j] =
        reinterpret_cast<pixel_t*>(dstp_b)[j] =
        reinterpret_cast<pixel_t*>(dstp_r)[j] = pixel;
      if constexpr (target_hasalpha) {
        const int alpha = source_hasalpha ? reinterpret_cast<const pixel_t*>(srcp)[j * source_rgb_step + 3] : max_pixel_value;
        reinterpret_cast<pixel_t*>(dstp_a)[j] = alpha;
      }
    }
    srcp -= src_pitch;
    dstp_g += dst_pitch;
    dstp_b += dst_pitch;
    dstp_r += dst_pitch;
    if constexpr (target_hasalpha)
      dstp_a += dst_pitch;
  }
}

template<typename pixel_t, bool source_hasalpha, bool target_hasalpha>
static void packed_to_luma_alpha(uint8_t* dstp_y, uint8_t* dstp_a, int dst_pitch, const uint8_t* srcp, int src_pitch, int width, int height, int channel)
{
  constexpr int source_rgb_step = source_hasalpha ? 4 : 3;
  constexpr int max_pixel_value = sizeof(pixel_t) == 1 ? 255 : 65535;

  // packed RGB is upside-down
  srcp += (height - 1) * src_pitch;

  for (int i = 0; i < height; ++i) {
    for (int j = 0; j < width; ++j) {
      const int pixel = reinterpret_cast<const pixel_t*>(srcp)[j * source_rgb_step + channel];
      reinterpret_cast<pixel_t*>(dstp_y)[j] = pixel;
      if constexpr (target_hasalpha) {
        const int alpha = source_hasalpha ? reinterpret_cast<const pixel_t*>(srcp)[j * source_rgb_step + 3] : max_pixel_value;
        reinterpret_cast<pixel_t*>(dstp_a)[j] = alpha;
      }
    }
    srcp -= src_pitch;
    dstp_y += dst_pitch;
    if constexpr (target_hasalpha)
      dstp_a += dst_pitch;
  }
}


PVideoFrame ShowChannel::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);

  // for planar these will be reread for proper plane
  const BYTE* srcp = src->GetReadPtr();
  const int height = src->GetHeight();
  const int pitch = src->GetPitch();
  const int rowsize = src->GetRowSize();

  const float chroma_center_f = 0.0f;

  if (input_type_is_packed_rgb) {
    PVideoFrame dst = env->NewVideoFrameP(vi, &src);

    if (!vi.IsRGB()) {
      // delete _Matrix when target is not an RGB
      auto props = env->getFramePropsRW(dst);
      env->propDeleteKey(props, "_Matrix");
    }

    const int source_rgb_step = source_hasalpha ? 4 : 3;
    const int w = rowsize / pixelsize / source_rgb_step;

    if (vi.IsRGB() && !vi.IsPlanar())
    {
      // packed RGB to packed RGB
      BYTE* dstp = dst->GetWritePtr();
      const int dstpitch = dst->GetPitch();

      if (pixelsize == 1) {
        if (!source_hasalpha && !target_hasalpha)
          packed_to_packedrgb<uint8_t, false, false>(dstp, dstpitch, srcp, pitch, w, height, channel);
        else if (!source_hasalpha && target_hasalpha)
          packed_to_packedrgb<uint8_t, false, true>(dstp, dstpitch, srcp, pitch, w, height, channel);
        else if (source_hasalpha && !target_hasalpha)
          packed_to_packedrgb<uint8_t, true, false>(dstp, dstpitch, srcp, pitch, w, height, channel);
        else // if (source_hasalpha && target_hasalpha)
          packed_to_packedrgb<uint8_t, true, true>(dstp, dstpitch, srcp, pitch, w, height, channel);
      }
      else {
        if (!source_hasalpha && !target_hasalpha)
          packed_to_packedrgb<uint16_t, false, false>(dstp, dstpitch, srcp, pitch, w, height, channel);
        else if (!source_hasalpha && target_hasalpha)
          packed_to_packedrgb<uint16_t, false, true>(dstp, dstpitch, srcp, pitch, w, height, channel);
        else if (source_hasalpha && !target_hasalpha)
          packed_to_packedrgb<uint16_t, true, false>(dstp, dstpitch, srcp, pitch, w, height, channel);
        else // if (source_hasalpha && target_hasalpha)
          packed_to_packedrgb<uint16_t, true, true>(dstp, dstpitch, srcp, pitch, w, height, channel);
      }
    }
    else if (vi.pixel_type == VideoInfo::CS_YUY2)
    {
      // packed RGB to YUY2
      BYTE* dstp = dst->GetWritePtr();
      const int dstpitch = dst->GetPitch();

      // RGB is upside-down
      srcp += (height - 1) * pitch;

      for (int i = 0; i < height; ++i) {
        for (int j = 0; j < w; j++) {
          dstp[j * 2 + 0] = srcp[j * source_rgb_step + channel];
          dstp[j * 2 + 1] = 128;
        }
        srcp -= pitch;
        dstp += dstpitch;
      }
    }
    else if (vi.IsYUV() || vi.IsYUVA() || vi.IsY())
    {
      // packed RGB -> Y, YUV(A)
      BYTE* dstp = dst->GetWritePtr();
      int dstpitch = dst->GetPitch();

      BYTE* dstp_a = target_hasalpha ? dst->GetWritePtr(PLANAR_A) : nullptr;

      if (pixelsize == 1) {
        if (!source_hasalpha && !target_hasalpha)
          packed_to_luma_alpha<uint8_t, false, false>(dstp, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else if (!source_hasalpha && target_hasalpha)
          packed_to_luma_alpha<uint8_t, false, true>(dstp, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else if (source_hasalpha && !target_hasalpha)
          packed_to_luma_alpha<uint8_t, true, false>(dstp, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else // if (source_hasalpha && target_hasalpha)
          packed_to_luma_alpha<uint8_t, true, true>(dstp, dstp_a, dstpitch, srcp, pitch, w, height, channel);
      }
      else {
        // 16 bit
        if (!source_hasalpha && !target_hasalpha)
          packed_to_luma_alpha<uint16_t, false, false>(dstp, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else if (!source_hasalpha && target_hasalpha)
          packed_to_luma_alpha<uint16_t, false, true>(dstp, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else if (source_hasalpha && !target_hasalpha)
          packed_to_luma_alpha<uint16_t, true, false>(dstp, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else // if (source_hasalpha && target_hasalpha)
          packed_to_luma_alpha<uint16_t, true, true>(dstp, dstp_a, dstpitch, srcp, pitch, w, height, channel);
      }

      // fill chroma neutral
      if (!vi.IsY())
      {
        int uvrowsize = dst->GetRowSize(PLANAR_U);
        int uvpitch = dst->GetPitch(PLANAR_U);
        int dstheight = dst->GetHeight(PLANAR_U);
        BYTE* dstp_u = dst->GetWritePtr(PLANAR_U);
        BYTE* dstp_v = dst->GetWritePtr(PLANAR_V);
        switch (pixelsize) {
        case 1: fill_chroma<BYTE>(dstp_u, dstp_v, dstheight, uvrowsize, uvpitch, (BYTE)0x80); break;
        case 2: fill_chroma<uint16_t>(dstp_u, dstp_v, dstheight, uvrowsize, uvpitch, 1 << (vi.BitsPerComponent() - 1)); break;
        case 4:
          fill_chroma<float>(dstp_u, dstp_v, dstheight, uvrowsize, uvpitch, chroma_center_f);
          break;
        }
      }
    }
    else if (vi.IsPlanarRGB() || vi.IsPlanarRGBA())
    {  // packed RGB -> Planar RGB 8/16 bit
      BYTE* dstp_g = dst->GetWritePtr(PLANAR_G);
      BYTE* dstp_b = dst->GetWritePtr(PLANAR_B);
      BYTE* dstp_r = dst->GetWritePtr(PLANAR_R);
      int dstpitch = dst->GetPitch();

      BYTE* dstp_a = target_hasalpha ? dst->GetWritePtr(PLANAR_A) : nullptr;

      if (pixelsize == 1) {
        if (!source_hasalpha && !target_hasalpha)
          packed_to_planarrgb<uint8_t, false, false>(dstp_r, dstp_g, dstp_b, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else if (!source_hasalpha && target_hasalpha)
          packed_to_planarrgb<uint8_t, false, true>(dstp_r, dstp_g, dstp_b, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else if (source_hasalpha && !target_hasalpha)
          packed_to_planarrgb<uint8_t, true, false>(dstp_r, dstp_g, dstp_b, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else // if (source_hasalpha && target_hasalpha)
          packed_to_planarrgb<uint8_t, true, true>(dstp_r, dstp_g, dstp_b, dstp_a, dstpitch, srcp, pitch, w, height, channel);
      }
      else {
        // 16 bit
        if (!source_hasalpha && !target_hasalpha)
          packed_to_planarrgb<uint16_t, false, false>(dstp_r, dstp_g, dstp_b, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else if (!source_hasalpha && target_hasalpha)
          packed_to_planarrgb<uint16_t, false, true>(dstp_r, dstp_g, dstp_b, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else if (source_hasalpha && !target_hasalpha)
          packed_to_planarrgb<uint16_t, true, false>(dstp_r, dstp_g, dstp_b, dstp_a, dstpitch, srcp, pitch, w, height, channel);
        else // if (source_hasalpha && target_hasalpha)
          packed_to_planarrgb<uint16_t, true, true>(dstp_r, dstp_g, dstp_b, dstp_a, dstpitch, srcp, pitch, w, height, channel);
      }
    }
    return dst;
  } // end of packed rgb source
  
  if (input_type_is_planar_rgb || input_type_is_planar_rgba || input_type_is_yuv || input_type_is_yuva) {
    // planar source
    const int planesYUV[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
    const int planesRGB[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
    const int* planes = (input_type_is_planar_rgb || input_type_is_planar_rgba) ? planesRGB : planesYUV;
    int final_channel = channel;
    // RGB channels: B=0 G=1 R=2 (like packed)
    // Planar order: G=0 B=1 R=2
    if (input_type_is_planar_rgb || input_type_is_planar_rgba) {
      // exchange B and G
      if (channel == 0) final_channel = 1;
      else if (channel == 1) final_channel = 0;
    }
    const int plane = planes[final_channel];

    const BYTE* srcp = src->GetReadPtr(plane); // source plane
    const BYTE* srcp_a = source_hasalpha ? src->GetReadPtr(PLANAR_A) : nullptr;

    const int width = src->GetRowSize(plane) / pixelsize;
    const int height = src->GetHeight(plane);
    const int pitch = src->GetPitch(plane);

    if (vi.IsRGB() && !vi.IsPlanar())
    {
      // planar RGBP/YUVA -> packed RGB
      PVideoFrame dst = env->NewVideoFrameP(vi, &src);
      BYTE* dstp = dst->GetWritePtr();
      const int dstpitch = dst->GetPitch();

      if (!input_type_is_planar_rgb && !input_type_is_planar_rgba) {
        auto props = env->getFramePropsRW(dst);
        // delete _Matrix and ChromaLocation when source is not RGB
        env->propDeleteKey(props, "_Matrix");
        env->propDeleteKey(props, "_ChromaLocation");
      }

      if (bits_per_pixel == 8) {
        if (target_hasalpha) {
          if (source_hasalpha)
            planar_to_packedrgb<uint8_t, true, true>(dstp, dstpitch, srcp, srcp_a, pitch, width, height);
          else
            planar_to_packedrgb<uint8_t, false, true>(dstp, dstpitch, srcp, srcp_a, pitch, width, height);
        }
        else {
          planar_to_packedrgb<uint8_t, false, false>(dstp, dstpitch, srcp, srcp_a, pitch, width, height);
        }
      }
      else {
        // 16 bits
        if (target_hasalpha) {
          if (source_hasalpha)
            planar_to_packedrgb<uint16_t, true, true>(dstp, dstpitch, srcp, srcp_a, pitch, width, height);
          else
            planar_to_packedrgb<uint16_t, false, true>(dstp, dstpitch, srcp, srcp_a, pitch, width, height);
        }
        else {
          planar_to_packedrgb<uint16_t, false, false>(dstp, dstpitch, srcp, srcp_a, pitch, width, height);
        }
      }

      return dst;
    }
    else if (vi.pixel_type == VideoInfo::CS_YUY2) // RGB(A)P/YUVA->YUY2
    {
      PVideoFrame dst = env->NewVideoFrameP(vi, &src);
      BYTE* dstp = dst->GetWritePtr();
      const int dstpitch = dst->GetPitch();

      auto props = env->getFramePropsRW(dst);
      env->propDeleteKey(props, "_Matrix");
      env->propDeleteKey(props, "_ChromaLocation");

      for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; j++) {
          dstp[j * 2 + 0] = srcp[j];
          dstp[j * 2 + 1] = 128;
        }
        srcp += pitch;
        dstp += dstpitch;
      }
      return dst;
    }
    else
    { // planar to planar
      // RGB(A)P/YUVA -> YV12/16/24/Y8 + 16bit
      PVideoFrame dst = env->NewVideoFrameP(vi, &src);

      // remove frame props if either src or target is not RGB
      if (!(input_type_is_planar_rgb || input_type_is_planar_rgba || vi.IsRGB())) {
        // RGB origin to YUV
        auto props = env->getFramePropsRW(dst);
        env->propDeleteKey(props, "_Matrix");
        if(input_type_is_yuv || input_type_is_yuva)
          env->propDeleteKey(props, "_ChromaLocation");
      }

      if (vi.IsYUV() || vi.IsYUVA() || vi.IsY()) // Y8, YV12, Y16, YUV420P16, etc.
      {
        BYTE* dstp = dst->GetWritePtr();
        int dstpitch = dst->GetPitch();

        // copy source plane to luma
        env->BitBlt(dstp, dstpitch, srcp, pitch, width * pixelsize, height);
        // fill UV with neutral
        if (!vi.IsY())
        {
          int uvrowsize = dst->GetRowSize(PLANAR_U);
          int uvpitch = dst->GetPitch(PLANAR_U);
          int dstheight = dst->GetHeight(PLANAR_U);
          BYTE* dstp_u = dst->GetWritePtr(PLANAR_U);
          BYTE* dstp_v = dst->GetWritePtr(PLANAR_V);
          switch (pixelsize) {
          case 1: fill_chroma<uint8_t>(dstp_u, dstp_v, dstheight, uvrowsize, uvpitch, (uint8_t)0x80); break;
          case 2: fill_chroma<uint16_t>(dstp_u, dstp_v, dstheight, uvrowsize, uvpitch, 1 << (vi.BitsPerComponent() - 1)); break;
          case 4: fill_chroma<float>(dstp_u, dstp_v, dstheight, uvrowsize, uvpitch, chroma_center_f); break;
          }
        }
      }
      else if (vi.IsPlanarRGB() || vi.IsPlanarRGBA())
      {  // RGBP(A)/YUVA -> Planar RGB
        BYTE* dstp_g = dst->GetWritePtr(PLANAR_G);
        BYTE* dstp_b = dst->GetWritePtr(PLANAR_B);
        BYTE* dstp_r = dst->GetWritePtr(PLANAR_R);

        int dstpitch = dst->GetPitch();
        int dstwidth = dst->GetRowSize() / pixelsize;

        // copy to all channels
        if (pixelsize == 1) {
          for (int i = 0; i < height; ++i) {
            for (int j = 0; j < dstwidth; ++j) {
              dstp_g[j] = dstp_b[j] = dstp_r[j] = srcp[j];
            }
            srcp += pitch;
            dstp_g += dstpitch;
            dstp_b += dstpitch;
            dstp_r += dstpitch;
          }
        }
        else if (pixelsize == 2) {
          for (int i = 0; i < height; ++i) {
            for (int j = 0; j < dstwidth; ++j) {
              reinterpret_cast<uint16_t*>(dstp_g)[j] =
                reinterpret_cast<uint16_t*>(dstp_b)[j] =
                reinterpret_cast<uint16_t*>(dstp_r)[j] = reinterpret_cast<const uint16_t*>(srcp)[j];
            }
            srcp += pitch;
            dstp_g += dstpitch;
            dstp_b += dstpitch;
            dstp_r += dstpitch;
          }
        }
        else { // pixelsize==4
          for (int i = 0; i < height; ++i) {
            for (int j = 0; j < dstwidth; ++j) {
              reinterpret_cast<float*>(dstp_g)[j] =
                reinterpret_cast<float*>(dstp_b)[j] =
                reinterpret_cast<float*>(dstp_r)[j] = reinterpret_cast<const float*>(srcp)[j];
            }
            srcp += pitch;
            dstp_g += dstpitch;
            dstp_b += dstpitch;
            dstp_r += dstpitch;
          }
        }
      }
      if (target_hasalpha) {
        // fill alpha with transparent
        const int dst_rowsizeA = dst->GetRowSize(PLANAR_A);
        const int dst_pitchA = dst->GetPitch(PLANAR_A);
        BYTE* dstp_a = dst->GetWritePtr(PLANAR_A);
        const int heightA = dst->GetHeight(PLANAR_A);

        if (source_hasalpha) {
          // copy source alpha plane to target alpha plane
          env->BitBlt(dstp_a, dst_pitchA, srcp_a, pitch, width* pixelsize, height);
        }
        else {
          switch (vi.ComponentSize())
          {
          case 1:
            fill_plane<uint8_t>(dstp_a, heightA, dst_rowsizeA, dst_pitchA, 0xFF);
            break;
          case 2:
            fill_plane<uint16_t>(dstp_a, heightA, dst_rowsizeA, dst_pitchA, (1 << vi.BitsPerComponent()) - 1);
            break;
          case 4:
            fill_plane<float>(dstp_a, heightA, dst_rowsizeA, dst_pitchA, 1.0f);
            break;
          }
        }
      }
      return dst;
    }
  } // planar RGB(A) or YUVA source

  env->ThrowError("ShowChannel: unexpected end of function");
  return src;
}


AVSValue ShowChannel::Create(AVSValue args, void* channel, IScriptEnvironment* env)
{
  // yuy2 is autoconverted to YV16
  PClip clip = args[0].AsClip();
  const VideoInfo& vi = clip->GetVideoInfo();

  if (vi.IsYUY2()) {
    AVSValue new_args[1] = { clip };
    clip = env->Invoke("ConvertToYV16", AVSValue(new_args, 1)).AsClip();
  }
  return new ShowChannel(clip, args[1].AsString(""), (int)(size_t)channel, env);
}


/**********************************
 ******  MergeRGB filter  ******
 **********************************/


MergeRGB::MergeRGB(PClip _child, PClip _blue, PClip _green, PClip _red, PClip _alpha,
  const char* pixel_type, IScriptEnvironment* env)
  : GenericVideoFilter(_child), blue(_blue), green(_green), red(_red), alpha(_alpha),
  viB(blue->GetVideoInfo()), viG(green->GetVideoInfo()), viR(red->GetVideoInfo()),
  viA(((alpha) ? alpha : child)->GetVideoInfo()), myname((alpha) ? "MergeARGB" : "MergeRGB")
{
  vi = viR; // comparison base

  if ((vi.BitsPerComponent() != viB.BitsPerComponent()) || (vi.BitsPerComponent() != viG.BitsPerComponent()) ||
    (vi.BitsPerComponent() != viR.BitsPerComponent()) || (vi.BitsPerComponent() != viA.BitsPerComponent()))
    env->ThrowError("%s: All clips must have the same bit depth.", myname);

  if ((vi.width != viB.width) || (vi.width != viG.width) || (vi.width != viR.width) || (vi.width != viA.width))
    env->ThrowError("%s: All clips must have the same width.", myname);

  if ((vi.height != viB.height) || (vi.height != viG.height) || (vi.height != viR.height) || (vi.height != viA.height))
    env->ThrowError("%s: All clips must have the same height.", myname);

  const int is_any_planar_rgb =
    viR.IsPlanarRGB() || viR.IsPlanarRGBA() ||
    viG.IsPlanarRGB() || viG.IsPlanarRGBA() ||
    viB.IsPlanarRGB() || viB.IsPlanarRGBA() ||
    viA.IsPlanarRGB() || viA.IsPlanarRGBA();

  const int bits_per_pixel = viR.BitsPerComponent();
  const bool empty_pixel_type = pixel_type == nullptr || *pixel_type == 0;

  // planar rgb target if
  // - pixel_type is "rgb" or
  // - pixel_type not specified and
  //   - bit depth is not 8 or 16 (cannot have packed rgb representation) or
  //   - any of the input clips is planar rgb

  if (!lstrcmpi(pixel_type, "rgb") ||
    (empty_pixel_type && (is_any_planar_rgb || (bits_per_pixel != 8 && bits_per_pixel != 16))))
  {
    switch (bits_per_pixel) {
    case 8: vi.pixel_type = alpha ? VideoInfo::CS_RGBAP8 : VideoInfo::CS_RGBP8; break;
    case 10: vi.pixel_type = alpha ? VideoInfo::CS_RGBAP10 : VideoInfo::CS_RGBP10; break;
    case 12: vi.pixel_type = alpha ? VideoInfo::CS_RGBAP12 : VideoInfo::CS_RGBP12; break;
    case 14: vi.pixel_type = alpha ? VideoInfo::CS_RGBAP14 : VideoInfo::CS_RGBP14; break;
    case 16: vi.pixel_type = alpha ? VideoInfo::CS_RGBAP16 : VideoInfo::CS_RGBP16; break;
    case 32: vi.pixel_type = alpha ? VideoInfo::CS_RGBAPS : VideoInfo::CS_RGBPS; break;
    }
  }
  else if (empty_pixel_type && vi.BitsPerComponent() == 8) {
    // default for 8 bit
    vi.pixel_type = VideoInfo::CS_BGR32;
  }
  else if (empty_pixel_type && vi.BitsPerComponent() == 16) {
    // default for 16 bit
    vi.pixel_type = VideoInfo::CS_BGR64;
  }
  else {
    // explicitely given output pixel type

    // first try
    // Append bit depth and check
    std::string format = pixel_type;
    // RGBP --> RGBPS, Y -> Y16, YUV420 -> YUV420P10
    if (!lstrcmpi(pixel_type, "y")) {
      format = format + std::to_string(bits_per_pixel); // Y8..Y16, also Y32
    }
    else if (!lstrcmpi(pixel_type, "rgbp") || !lstrcmpi(pixel_type, "rgbap")) {
      if (bits_per_pixel == 32)
        format = format + "S"; // RGBAPS
      else
        format = format + std::to_string(bits_per_pixel); // RGBP16
    }
    else {
      // hopefully like "yuv420" or "yuva444"
      if (bits_per_pixel == 32)
        format = format + "PS"; // YUV420PS
      else
        format = format + "P" + std::to_string(bits_per_pixel); // YUV420P16
    }

    int new_pixel_type = GetPixelTypeFromName(format.c_str());
    if (new_pixel_type == VideoInfo::CS_UNKNOWN) {
      new_pixel_type = GetPixelTypeFromName(pixel_type);
      if (new_pixel_type == VideoInfo::CS_UNKNOWN)
        env->ThrowError("%s: invalid pixel_type!", myname);
    }
    // new output format
    vi.pixel_type = new_pixel_type;
  }

  if (vi.BitsPerComponent() != bits_per_pixel)
    env->ThrowError("%s: target bit depth (%d) must match with sources (%d)", myname, vi.BitsPerComponent(), bits_per_pixel);

  if (!vi.IsRGB())
    env->ThrowError("%s: target format must be an RGB format", myname);

  if(alpha && vi.NumComponents() != 4)
    env->ThrowError("MergeARGB: target format must have an alpha channel");

  // When not in ARGB mode, target is still allowed to have an alpha channel.
  // If no alpha source is given, target alpha will be filled by default 0 value.

  if (alpha && (viA.IsRGB24() || viA.IsRGB48() || viA.IsPlanarRGB()))
    env->ThrowError("MergeARGB: Alpha source channel cannot be obtained from RGB24, RGB48 or alphaless planar RGB");
}


PVideoFrame MergeRGB::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame B = blue->GetFrame(n, env);
  PVideoFrame G = green->GetFrame(n, env);
  PVideoFrame R = red->GetFrame(n, env);
  PVideoFrame A = (alpha) ? alpha->GetFrame(n, env) : 0;

  // choose one for frame property source: R
  PVideoFrame dst = env->NewVideoFrameP(vi, &R);

  auto props = env->getFramePropsRW(dst);
  if (!viR.IsRGB()) {
    // delete _Matrix and ChromaLocation when source is not RGB
    env->propDeleteKey(props, "_Matrix");
    env->propDeleteKey(props, "_ChromaLocation");
  }

  const int pixelsize = viR.ComponentSize();

  if (!vi.IsPlanar()) {
    // target is packed RGB
    const int height = dst->GetHeight();
    const int pitch = dst->GetPitch();
    const int rowsize = dst->GetRowSize();
    const int modulo = pitch - rowsize;

    BYTE* dstp = dst->GetWritePtr();


    int planeB = viB.IsPlanar() ? (viB.IsRGB() ? PLANAR_B : PLANAR_Y) : 0;
    int planeG = viG.IsPlanar() ? (viG.IsRGB() ? PLANAR_G : PLANAR_Y) : 0;
    int planeR = viR.IsPlanar() ? (viR.IsRGB() ? PLANAR_R : PLANAR_Y) : 0;
    int planeA = viA.IsPlanar() ? (viA.IsRGB() ? PLANAR_A : PLANAR_Y) : 0;

    // RGB is upside-down, backscan any Planar to match
    const int Bpitch = (viB.IsPlanar()) ? -(B->GetPitch(planeB)) : B->GetPitch();
    const int Gpitch = (viG.IsPlanar()) ? -(G->GetPitch(planeG)) : G->GetPitch();
    const int Rpitch = (viR.IsPlanar()) ? -(R->GetPitch(planeR)) : R->GetPitch();

    // Bump any RGB channels, move any YUV channels to last line
    const BYTE* Bp = B->GetReadPtr(planeB) + (viB.IsPlanar() ? Bpitch * (1 - height) : 0);
    const BYTE* Gp = G->GetReadPtr(planeG) + (viG.IsPlanar() ? Gpitch * (1 - height) : (viG.IsRGB() ? 1 * pixelsize : 0));
    const BYTE* Rp = R->GetReadPtr(planeR) + (viR.IsPlanar() ? Rpitch * (1 - height) : (viR.IsRGB() ? 2 * pixelsize : 0));

    // Adjustment from the end of 1 line to the start of the next
    const int Bmodulo = Bpitch - B->GetRowSize(planeB);
    const int Gmodulo = Gpitch - G->GetRowSize(planeG);
    const int Rmodulo = Rpitch - R->GetRowSize(planeR);

    // Number of bytes per pixel (1, 2, 3 or 4 .. 8)
    const int Bstride = viB.IsPlanar() ? pixelsize : (viB.BitsPerPixel() >> 3);
    const int Gstride = viG.IsPlanar() ? pixelsize : (viG.BitsPerPixel() >> 3);
    const int Rstride = viR.IsPlanar() ? pixelsize : (viR.BitsPerPixel() >> 3);

    // End of VFB
    BYTE const* yend = dstp + pitch * height;

    if (alpha) { // ARGB mode
      const int Apitch = (viA.IsPlanar()) ? -(A->GetPitch(planeA)) : A->GetPitch();
      const BYTE* Ap = A->GetReadPtr(planeA) + (viA.IsPlanar() ? Apitch * (1 - height) : (viA.IsRGB() ? 3 * pixelsize : 0));
      const int Amodulo = Apitch - A->GetRowSize(planeA);
      const int Astride = viA.IsPlanar() ? pixelsize : (viA.BitsPerPixel() >> 3);

      switch (pixelsize) {
      case 1:
        while (dstp < yend) {
          BYTE const* xend = dstp + rowsize;
          while (dstp < xend) {
            *dstp++ = *Bp; Bp += Bstride;
            *dstp++ = *Gp; Gp += Gstride;
            *dstp++ = *Rp; Rp += Rstride;
            *dstp++ = *Ap; Ap += Astride;
          }
          dstp += modulo;
          Bp += Bmodulo;
          Gp += Gmodulo;
          Rp += Rmodulo;
          Ap += Amodulo;
        }
        break;
      case 2:
      {
        uint16_t* dstp16 = reinterpret_cast<uint16_t*>(dstp);
        uint16_t const* yend16 = dstp16 + pitch * height / sizeof(uint16_t);
        while (dstp16 < yend16) {
          uint16_t const* xend16 = dstp16 + rowsize / sizeof(uint16_t);
          while (dstp16 < xend16) {
            *dstp16++ = *reinterpret_cast<const uint16_t*>(Bp); Bp += Bstride;
            *dstp16++ = *reinterpret_cast<const uint16_t*>(Gp); Gp += Gstride;
            *dstp16++ = *reinterpret_cast<const uint16_t*>(Rp); Rp += Rstride;
            *dstp16++ = *reinterpret_cast<const uint16_t*>(Ap); Ap += Astride;
          }
          dstp16 += modulo / sizeof(uint16_t);
          Bp += Bmodulo;
          Gp += Gmodulo;
          Rp += Rmodulo;
          Ap += Amodulo;
        }
      }
      break;
      default:
        env->ThrowError("%s: float pixel type not supported", myname);
        break;
      }
    }
    else if (vi.pixel_type == VideoInfo::CS_BGR32 || vi.pixel_type == VideoInfo::CS_BGR64) { // RGB32 mode
      switch (pixelsize) {
      case 1:
        while (dstp < yend) {
          BYTE const* xend = dstp + rowsize;
          while (dstp < xend) {
            *dstp++ = *Bp; Bp += Bstride;
            *dstp++ = *Gp; Gp += Gstride;
            *dstp++ = *Rp; Rp += Rstride;
            *dstp++ = 0; // default Alpha is 0!
          }
          dstp += modulo;
          Bp += Bmodulo;
          Gp += Gmodulo;
          Rp += Rmodulo;
        }
        break;
      case 2:
      {
        uint16_t* dstp16 = reinterpret_cast<uint16_t*>(dstp);
        uint16_t const* yend16 = dstp16 + pitch * height / sizeof(uint16_t);
        while (dstp16 < yend16) {
          uint16_t const* xend16 = dstp16 + rowsize / sizeof(uint16_t);
          while (dstp16 < xend16) {
            *dstp16++ = *reinterpret_cast<const uint16_t*>(Bp); Bp += Bstride;
            *dstp16++ = *reinterpret_cast<const uint16_t*>(Gp); Gp += Gstride;
            *dstp16++ = *reinterpret_cast<const uint16_t*>(Rp); Rp += Rstride;
            *dstp16++ = 0; // default Alpha is 0!
          }
          dstp16 += modulo / sizeof(uint16_t);
          Bp += Bmodulo;
          Gp += Gmodulo;
          Rp += Rmodulo;
        }
      }
      break;
      default:
        env->ThrowError("%s: float pixel type not supported", myname);
        break;
      }
    }
    else if (vi.pixel_type == VideoInfo::CS_BGR24 || vi.pixel_type == VideoInfo::CS_BGR48) { // RGB24 mode
      switch (pixelsize) {
      case 1:
        while (dstp < yend) {
          BYTE const* xend = dstp + rowsize;
          while (dstp < xend) {
            *dstp++ = *Bp; Bp += Bstride;
            *dstp++ = *Gp; Gp += Gstride;
            *dstp++ = *Rp; Rp += Rstride;
          }
          dstp += modulo;
          Bp += Bmodulo;
          Gp += Gmodulo;
          Rp += Rmodulo;
        }
        break;
      case 2:
      {
        uint16_t* dstp16 = reinterpret_cast<uint16_t*>(dstp);
        uint16_t const* yend16 = dstp16 + pitch * height / sizeof(uint16_t);
        while (dstp16 < yend16) {
          uint16_t const* xend16 = dstp16 + rowsize / sizeof(uint16_t);
          while (dstp16 < xend16) {
            *dstp16++ = *reinterpret_cast<const uint16_t*>(Bp); Bp += Bstride;
            *dstp16++ = *reinterpret_cast<const uint16_t*>(Gp); Gp += Gstride;
            *dstp16++ = *reinterpret_cast<const uint16_t*>(Rp); Rp += Rstride;
          }
          dstp16 += modulo / sizeof(uint16_t);
          Bp += Bmodulo;
          Gp += Gmodulo;
          Rp += Rmodulo;
        }
      }
      break;
      default:
        env->ThrowError("%s: float pixel type not supported", myname);
        break;
      }
    }
    else
      env->ThrowError("%s: unexpected end of function", myname);

    return dst;
  }

  // target is planar RGB
  for (int p = 0; p < vi.NumComponents(); p++) {
    int planes_r[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
    const int plane = planes_r[p];

    const VideoInfo& vi_src =
      plane == PLANAR_G ? viG :
      plane == PLANAR_B ? viB :
      plane == PLANAR_R ? viR : viA;

    PVideoFrame& src =
      plane == PLANAR_G ? G :
      plane == PLANAR_B ? B :
      plane == PLANAR_R ? R : A;

    // actual plane source is not a packed RGB type

    if (p == 3 && !alpha) {
      // fill alpha, but not ARGB mode

      const int dst_rowsizeA = dst->GetRowSize(PLANAR_A);
      const int dst_pitchA = dst->GetPitch(PLANAR_A);
      BYTE* dstp_a = dst->GetWritePtr(PLANAR_A);
      const int heightA = dst->GetHeight(PLANAR_A);
      // unlike in other filters, default alpha value here is 0 instead of max transparent 255
      switch (vi.ComponentSize())
      {
      case 1:
        fill_plane<uint8_t>(dstp_a, heightA, dst_rowsizeA, dst_pitchA, 0);
        break;
      case 2:
        fill_plane<uint16_t>(dstp_a, heightA, dst_rowsizeA, dst_pitchA, 0);
        break;
      case 4:
        fill_plane<float>(dstp_a, heightA, dst_rowsizeA, dst_pitchA, 0.0f);
        break;
      }
    }
    else {
      if (vi_src.IsPlanar())
      {
        // plane copy
        const int plane_src = vi_src.IsRGB() ? plane : PLANAR_Y;
        env->BitBlt(dst->GetWritePtr(plane), dst->GetPitch(plane), src->GetReadPtr(plane_src),
          src->GetPitch(plane_src), src->GetRowSize(plane_src), src->GetHeight(plane_src));
      }
      else {
        // fill a plane from packed RGB
        // packed RGB -> Y, YUV(A)
        BYTE* dstp = dst->GetWritePtr(plane);
        int dstpitch = dst->GetPitch(plane);


        const BYTE* srcp = src->GetReadPtr();
        const int pitch = src->GetPitch();
        const int packed_rgb_channel =
          plane == PLANAR_G ? 1 :
          plane == PLANAR_B ? 0 :
          plane == PLANAR_R ? 2 : 3;

        if (pixelsize == 1) {
          if (vi_src.NumComponents() == 3)
            packed_to_luma_alpha<uint8_t, false, false>(dstp, nullptr, dstpitch, srcp, pitch, vi.width, vi.height, packed_rgb_channel);
          else
            packed_to_luma_alpha<uint8_t, true, false>(dstp, nullptr, dstpitch, srcp, pitch, vi.width, vi.height, packed_rgb_channel);
        }
        else {
          if (vi_src.NumComponents() == 3)
            packed_to_luma_alpha<uint16_t, false, false>(dstp, nullptr, dstpitch, srcp, pitch, vi.width, vi.height, packed_rgb_channel);
          else
            packed_to_luma_alpha<uint16_t, true, false>(dstp, nullptr, dstpitch, srcp, pitch, vi.width, vi.height, packed_rgb_channel);
        }
      }
    }
  }
  return dst;
}


AVSValue MergeRGB::Create(AVSValue args, void* mode, IScriptEnvironment* env)
{
  if (mode) // ARGB
    return new MergeRGB(args[0].AsClip(), args[3].AsClip(), args[2].AsClip(), args[1].AsClip(), args[0].AsClip(), args[4].AsString(""), env);
  else      // RGB[type]
    return new MergeRGB(args[0].AsClip(), args[2].AsClip(), args[1].AsClip(), args[0].AsClip(), 0, args[3].AsString(""), env);
}


/*******************************
 *******   Layer Filter   ******
 *******************************/

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
