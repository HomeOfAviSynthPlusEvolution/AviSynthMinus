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


#include "levels.h"
#include <float.h>
#include <cstdio>
#include <cmath>
#include <avs/minmax.h>
#include <avs/alignment.h>
#include "../core/internal.h"
#include <algorithm>
#include <string>

#define PI 3.141592653589793


/********************************************************************
***** Declare index of new filters for Avisynth's filter engine *****
********************************************************************/

extern const AVSFunction Levels_filters[] = {
  { "MaskHS",    BUILTIN_FUNC_PREFIX, "c[startHue]f[endHue]f[maxSat]f[minSat]f[coring]b[realcalc]b", MaskHS::Create },

  { 0 }
};


static void __cdecl free_buffer(void* buff, IScriptEnvironment* env)
{
    if (buff) {
        static_cast<IScriptEnvironment2*>(env)->Free(buff);
    }
}

// Limits for MaskHS
static void get_limits(luma_chroma_limits_t &d, int bits_per_pixel) {
  int tv_range_lo_luma_8 = 16;
  int tv_range_hi_luma_8 = 235;
  int tv_range_lo_chroma_8 = tv_range_lo_luma_8;
  int tv_range_hi_chroma_8 = 240;

  if (bits_per_pixel == 32) {
    d.tv_range_low_luma_f = tv_range_lo_luma_8 / 255.0f;
    d.tv_range_hi_luma_f = tv_range_hi_luma_8 / 255.0f;
    d.full_range_low_luma_f = 0.0f;
    d.full_range_hi_luma_f = 1.0f;
#ifdef FLOAT_CHROMA_IS_HALF_CENTERED
    d.middle_chroma_f = 0.5f;
#else
    d.middle_chroma_f = 0.0f;
#endif
    d.tv_range_low_chroma_f = (tv_range_lo_chroma_8 - 128) / 255.0f + d.middle_chroma_f; // -112
    d.tv_range_hi_chroma_f = (tv_range_hi_chroma_8 - 128) / 255.0f + d.middle_chroma_f; // 112
    d.full_range_low_chroma_f = d.middle_chroma_f - 0.5f; // -0.5..0.5 or 0..1.0
    d.full_range_hi_chroma_f = d.middle_chroma_f + 0.5f;

    d.range_luma_f = d.tv_range_hi_luma_f - d.tv_range_low_luma_f;
    d.range_chroma_f = d.tv_range_hi_chroma_f - d.tv_range_low_chroma_f;
  }
  else {
    d.tv_range_low = tv_range_lo_luma_8 << (bits_per_pixel - 8); // 16-240,64-960, 256-3852,... 4096-61692
    d.tv_range_hi_luma = tv_range_hi_luma_8 << (bits_per_pixel - 8);
    d.tv_range_hi_chroma = tv_range_hi_chroma_8 << (bits_per_pixel - 8);
    d.middle_chroma = 1 << (bits_per_pixel - 1); // 128
    d.range_luma = d.tv_range_hi_luma - d.tv_range_low; // 219
    d.range_chroma = d.tv_range_hi_chroma - d.tv_range_low; // 224
  }
}

/********************************
 *******   MaskHS Filter   ******
 ********************************/

/* Hue and saturation selection for MaskHS. */
static bool ProcessPixel(double X, double Y, double startHue, double endHue,
    double maxSat, double minSat, double p, int &iSat)
{
    // a hue analog
    double T = atan2(X, Y) * 180.0 / PI;
    if (T < 0.0) T += 360.0;

    // startHue <= hue <= endHue
    if (startHue < endHue) {
        if (T > endHue || T < startHue) return false;
    }
    else {
        if (T<startHue && T>endHue) return false;
    }

    const double W = X*X + Y*Y;

    // In Range, full adjust but no need to interpolate
    if (minSat*minSat <= W && W <= maxSat*maxSat) return true;

    // p == 0 (no interpolation) needed for MaskHS
    if (p == 0.0) return false;

    // Interpolation range is +/-p for p>0
    // 180 is not in degrees!
    // its sqrt(127^2 + 127^2): max overshoot for 8 bits; U and V is 0 +/- 127
    const double max = min(maxSat + p, 180.0);
    const double min = ::max(minSat - p, 0.0);

    // Outside of [min-p, max+p] no adjustment
    // minSat-p <= (U^2 + V^2) <= maxSat+p
    if (W <= min*min || max*max <= W) return false; // don't adjust

    // Interpolate saturation value
    const double holdSat = W < 180.0*180.0 ? sqrt(W) : 180.0;

    if (holdSat < minSat) { // within p of lower range
        iSat += (int)((512 - iSat) * (minSat - holdSat) / p);
    }
    else { // within p of upper range
        iSat += (int)((512 - iSat) * (holdSat - maxSat) / p);
    }

    return true;
}

MaskHS::MaskHS(PClip _child, double _startHue, double _endHue, double _maxSat, double _minSat, bool _coring, bool _realcalc,
    IScriptEnvironment* env)
    : GenericVideoFilter(_child), dstartHue(_startHue), dendHue(_endHue), dmaxSat(_maxSat), dminSat(_minSat), coring(_coring), realcalc(_realcalc)
{
    if (vi.IsRGB())
        env->ThrowError("MaskHS: YUV data only (no RGB)");

    if (vi.NumComponents() == 1) {
        env->ThrowError("MaskHS: clip must contain chroma.");
    }

    if (dstartHue < 0.0 || dstartHue >= 360.0)
        env->ThrowError("MaskHS: startHue must be greater than or equal to 0.0 and less than 360.0");

    if (dendHue <= 0.0 || dendHue > 360.0)
        env->ThrowError("MaskHS: endHue must be greater than 0.0 and less than or equal to 360.0");

    if (dminSat >= dmaxSat)
        env->ThrowError("MaskHS: MinSat must be less than MaxSat");

    if (dminSat < 0.0 || dminSat >= 150.0)
        env->ThrowError("MaskHS: minSat must be greater than or equal to 0 and less than 150.");

    if (dmaxSat <= 0.0 || dmaxSat > 150.0)
        env->ThrowError("MaskHS: maxSat must be greater than 0 and less than or equal to 150.");

    pixelsize = vi.ComponentSize();
    bits_per_pixel = vi.BitsPerComponent();
    lut_size = pixelsize == 4 ? 0 : 1 << bits_per_pixel;
    max_pixel_value = pixelsize == 4 ? 1 : lut_size - 1;

    get_limits(limits, bits_per_pixel);

    mask_low = coring ? limits.tv_range_low : 0;
    mask_high = coring ? limits.tv_range_hi_luma : max_pixel_value;

    if (bits_per_pixel == 32) {
      mask_low_f = coring ? limits.tv_range_low_luma_f : limits.full_range_low_luma_f;
      mask_high_f = coring ? limits.tv_range_hi_luma_f : limits.full_range_hi_luma_f;
    }

    realcalc_chroma = realcalc;
    if (vi.IsPlanar() && (bits_per_pixel > 12)) // max bitdepth is 12 for lut
      realcalc_chroma = true;

    // 100% equals sat=119 (= maximal saturation of valid RGB (R=255,G=B=0)
    // 150% (=180) - 100% (=119) overshoot
    minSat = 1.19 * dminSat;
    maxSat = 1.19 * dmaxSat;

    if (!(realcalc_chroma && vi.IsPlanar()))
    { // fill lookup tables for UV
      size_t map_size = pixelsize * lut_size * lut_size;
      // for  8 bit : 1 * 256 * 256 = 65536 byte
      // for 10 bit : 2 * 1024 * 1024 = 2 MByte
      // for 12 bit : 2 * 4096 * 4096 = 32 MByte
      mapUV = static_cast<uint8_t*>(env->Allocate(map_size, 8, AVS_NORMAL_ALLOC)); // uint16_t for (U+V bytes), casted to uint32_t for (U+V words in non-8 bit)
      if (!mapUV)
        env->ThrowError("Tweak: Could not reserve memory.");
      env->AtExit(free_buffer, mapUV);

      // apply mask
      double uv_range_corr = 1.0 / (1 << (bits_per_pixel - 8)); // no float here
      for (int u = 0; u < lut_size; u++) {
          const double destu = (u - limits.middle_chroma) * uv_range_corr; // processpixel's minSat and maxSat is for 256 range
          int ushift = u << bits_per_pixel;
          for (int v = 0; v < lut_size; v++) {
              const double destv = (v - limits.middle_chroma) * uv_range_corr;
              int iSat = 0; // won't be used in MaskHS; interpolation is skipped since p==0:
              bool ppres = ProcessPixel(destv, destu, dstartHue, dendHue, maxSat, minSat, 0.0, iSat);
              if(pixelsize==1)
                  mapUV[ushift | v] = ppres ? mask_high : mask_low;
              else
                  reinterpret_cast<uint16_t *>(mapUV)[ushift | v] = ppres ? mask_high : mask_low;
          }
      }
    } // end of lut calculation
    // #define MaskPointResizing
#ifndef MaskPointResizing
    vi.width >>= vi.GetPlaneWidthSubsampling(PLANAR_U);
    vi.height >>= vi.GetPlaneHeightSubsampling(PLANAR_U);
#endif
    switch(bits_per_pixel) {
    case 8: vi.pixel_type = VideoInfo::CS_Y8; break;
    case 10: vi.pixel_type = VideoInfo::CS_Y10; break;
    case 12: vi.pixel_type = VideoInfo::CS_Y12; break;
    case 14: vi.pixel_type = VideoInfo::CS_Y14; break;
    case 16: vi.pixel_type = VideoInfo::CS_Y16; break;
    case 32: vi.pixel_type = VideoInfo::CS_Y32; break;
    }
}



PVideoFrame __stdcall MaskHS::GetFrame(int n, IScriptEnvironment* env)
{
    PVideoFrame src = child->GetFrame(n, env);
    PVideoFrame dst = env->NewVideoFrameP(vi, &src);

    uint8_t* dstp = dst->GetWritePtr();
    int dst_pitch = dst->GetPitch();

    // show mask
    if (child->GetVideoInfo().IsYUY2()) {
        const uint8_t* srcp = src->GetReadPtr();
        const int src_pitch = src->GetPitch();
        const int height = src->GetHeight();

#ifndef MaskPointResizing
        const int row_size = src->GetRowSize() >> 2;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < row_size; x++) {
                dstp[x] = mapUV[((srcp[x * 4 + 1]) << 8) | srcp[x * 4 + 3]];
            }
            srcp += src_pitch;
            dstp += dst_pitch;
        }
#else
        const int row_size = src->GetRowSize();

        for (int y = 0; y < height; y++) {
            for (int xs = 0, xd = 0; xs < row_size; xs += 4, xd += 2) {
                const BYTE mapped = mapY[((srcp[xs + 1]) << 8) | srcp[xs + 3]];
                dstp[xd] = mapped;
                dstp[xd + 1] = mapped;
            }
            srcp += src_pitch;
            dstp += dst_pitch;
        }
#endif
    }
    else if (child->GetVideoInfo().IsPlanar()) {
        const int srcu_pitch = src->GetPitch(PLANAR_U);
        const uint8_t* srcpu = src->GetReadPtr(PLANAR_U);
        const uint8_t* srcpv = src->GetReadPtr(PLANAR_V);
        const int width = src->GetRowSize(PLANAR_U) / pixelsize;
        const int heightu = src->GetHeight(PLANAR_U);

#ifndef MaskPointResizing
        if(realcalc_chroma) {
          double uv_range_corr = (pixelsize == 4) ? 255.0 : 1.0 / (1 << (bits_per_pixel - 8));
          if(pixelsize == 1) {
            for (int y = 0; y < heightu; ++y) {
              for (int x = 0; x < width; ++x) {
                const double destu = srcpu[x] - limits.middle_chroma;
                const double destv = srcpv[x] - limits.middle_chroma;
                int iSat = 0; // won't be used in MaskHS; interpolation is skipped since p==0:
                bool ppres = ProcessPixel(destv * uv_range_corr, destu * uv_range_corr, dstartHue, dendHue, maxSat, minSat, 0.0, iSat);
                dstp[x] = ppres ? mask_high : mask_low;
              }
              dstp += dst_pitch;
              srcpu += srcu_pitch;
              srcpv += srcu_pitch;
            }
          }
          else if (pixelsize == 2) {
            for (int y = 0; y < heightu; ++y) {
              for (int x = 0; x < width; ++x) {
                const double destu = (reinterpret_cast<const uint16_t *>(srcpu)[x] - limits.middle_chroma);
                const double destv = (reinterpret_cast<const uint16_t *>(srcpv)[x] - limits.middle_chroma);
                int iSat = 0; // won't be used in MaskHS; interpolation is skipped since p==0:
                bool ppres = ProcessPixel(destv * uv_range_corr, destu * uv_range_corr, dstartHue, dendHue, maxSat, minSat, 0.0, iSat);
                reinterpret_cast<uint16_t *>(dstp)[x] = ppres ? mask_high : mask_low;
              }
              dstp += dst_pitch;
              srcpu += srcu_pitch;
              srcpv += srcu_pitch;
            }
          } else { // pixelsize == 4
            for (int y = 0; y < heightu; ++y) {
              for (int x = 0; x < width; ++x) {
                const double destu = (reinterpret_cast<const float *>(srcpu)[x] - limits.middle_chroma_f);
                const double destv = (reinterpret_cast<const float *>(srcpv)[x] - limits.middle_chroma_f);
                int iSat = 0; // won't be used in MaskHS; interpolation is skipped since p==0:
                bool ppres = ProcessPixel(destv * uv_range_corr, destu * uv_range_corr, dstartHue, dendHue, maxSat, minSat, 0.0, iSat);
                reinterpret_cast<float *>(dstp)[x] = ppres ? mask_high_f : mask_low_f;
              }
              dstp += dst_pitch;
              srcpu += srcu_pitch;
              srcpv += srcu_pitch;
            }
          }

        } else {
          // use LUT
          if(pixelsize==1) {
            for (int y = 0; y < heightu; ++y) {
                for (int x = 0; x < width; ++x) {
                    dstp[x] = mapUV[((srcpu[x]) << 8) | srcpv[x]];
                }
                dstp += dst_pitch;
                srcpu += srcu_pitch;
                srcpv += srcu_pitch;
            }
          }
          else if (pixelsize == 2) {
            for (int y = 0; y < heightu; ++y) {
              for (int x = 0; x < width; ++x) {
                reinterpret_cast<uint16_t *>(dstp)[x] =
                  reinterpret_cast<uint16_t *>(mapUV)[((reinterpret_cast<const uint16_t *>(srcpu)[x]) << bits_per_pixel) | reinterpret_cast<const uint16_t *>(srcpv)[x]];
              }
              dstp += dst_pitch;
              srcpu += srcu_pitch;
              srcpv += srcu_pitch;
            }
          } // no lut for float (and for 14-16 bit)
        }
#else
        const int swidth = child->GetVideoInfo().GetPlaneWidthSubsampling(PLANAR_U);
        const int sheight = child->GetVideoInfo().GetPlaneHeightSubsampling(PLANAR_U);
        const int sw = 1 << swidth;
        const int sh = 1 << sheight;

        const int dpitch = dst_pitch << sheight;
        for (int y = 0; y < heightu; ++y) {
            for (int x = 0; x < row_sizeu; ++x) {
                const BYTE mapped = mapY[((srcpu[x]) << 8) | srcpv[x]];
                const int sx = x << swidth;

                for (int lumv = 0; lumv < sh; ++lumv) {
                    const int sy = lumv*dst_pitch + sx;

                    for (int lumh = 0; lumh < sw; ++lumh) {
                        dstp[sy + lumh] = mapped;
                    }
                }
            }
            dstp += dpitch;
            srcpu += srcu_pitch;
            srcpv += srcu_pitch;
        }
#endif
    }
    return dst;
}



AVSValue __cdecl MaskHS::Create(AVSValue args, void* , IScriptEnvironment* env)
{
    return new MaskHS(args[0].AsClip(),
        args[1].AsDblDef(0.0),    // startHue
        args[2].AsDblDef(360.0),    // endHue
        args[3].AsDblDef(150.0),    // maxSat
        args[4].AsDblDef(0.0),    // minSat
        args[5].AsBool(false),      // coring
        args[6].AsBool(false),      // realcalc
      env);
}

