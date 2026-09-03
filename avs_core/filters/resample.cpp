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
#include <cmath>
#ifdef INTEL_INTRINSICS
#include "intel/resample_sse.h"
#include "intel/resample_avx2.h"
#include "intel/turn_sse.h"
#endif
#include <avs/config.h>

#include "transform.h"
#include "turn.h"
#include <avs/alignment.h>
#include <avs/minmax.h>
#include "../convert/convert_planar.h"
#include "../convert/convert_yuy2.h"
#include "../convert/convert_helper.h"

#include <type_traits>
#include <algorithm>


// Prepares resampling coefficients for end conditions and/or SIMD processing by:
// 1. Sets a "real-life" size for the filter, which at small dimensions can be less than the original
// 2. Aligning filter_size to 8 or 16 boundary for SIMD efficiency
// 3. Right-aligning coefficients within padded arrays to ensure valid access at boundaries
//
// Before:                After right-alignment (filter_size=4, kernel_size=2):
//
// offset->|            offset-2 ->|         
//        [x][y][  ][  ]          [0][0][x][y]
//         ^ ^   ^   ^             ^         ^
//         | |   Off-boundary      |         |
//     Values used                 Values used
//
// This ensures SIMD instructions can safely load full vectors even at image boundaries
// while maintaining correct coefficient positioning and proper zero padding.


void resize_prepare_coeffs(ResamplingProgram* p, IScriptEnvironment* env, int filter_size_alignment) {
  p->filter_size_alignment = filter_size_alignment;
  p->overread_possible = false;

  // note: filter_size_real was the max(kernel_sizes[])
  int filter_size_aligned = AlignNumber(p->filter_size_real, p->filter_size_alignment);

  int target_size_aligned = AlignNumber(p->target_size, ALIGN_RESIZER_TARGET_SIZE);

  // Common variables for both float and integer paths
  void* new_coeff = nullptr;
  void* src_coeff = nullptr;
  size_t element_size = 0;

  // allocate for a larger target_size area and nullify the coeffs.
  // Even between target_size and target_size_aligned.
  if (p->bits_per_pixel == 32) {
    element_size = sizeof(float);
    src_coeff = p->pixel_coefficient_float;
    new_coeff = env->Allocate(element_size * target_size_aligned * filter_size_aligned, 64, AVS_NORMAL_ALLOC);
    if (!new_coeff) {
      env->Free(new_coeff);
      env->ThrowError("Could not reserve memory in a resampler.");
    }
    std::fill_n((float*)new_coeff, target_size_aligned * filter_size_aligned, 0.0f);
  }
  else {
    element_size = sizeof(short);
    src_coeff = p->pixel_coefficient;
    new_coeff = env->Allocate(element_size * target_size_aligned * filter_size_aligned, 64, AVS_NORMAL_ALLOC);
    if (!new_coeff) {
      env->Free(new_coeff);
      env->ThrowError("Could not reserve memory in a resampler.");
    }
    memset(new_coeff, 0, element_size * target_size_aligned * filter_size_aligned);
  }

  const int last_line = p->source_size - 1;

  // Process coefficients - common code for both types
  for (int i = 0; i < p->target_size; i++) {
    const int kernel_size = p->kernel_sizes[i];
    const int offset = p->pixel_offset[i];
    const int last_coeff_index = offset + p->filter_size_real - 1;
    const int shift_needed = last_coeff_index > last_line ? p->filter_size_real - kernel_size : 0;

    // Copy coefficients with appropriate shift
    if (p->bits_per_pixel == 32) {
      float* dst = (float*)new_coeff + i * filter_size_aligned;
      float* src = (float*)src_coeff + i * p->filter_size;
      for (int j = 0; j < kernel_size; j++) {
        dst[j + shift_needed] = src[j];
      }
    }
    else {
      short* dst = (short*)new_coeff + i * filter_size_aligned;
      short* src = (short*)src_coeff + i * p->filter_size;
      for (int j = 0; j < kernel_size; j++) {
        dst[j + shift_needed] = src[j];
      }
    }

    // Update offsets and kernel sizes
    p->pixel_offset[i] -= shift_needed;
    p->kernel_sizes[i] += shift_needed;

    // left side, already right padded with zero coeffs, we can
    // change to actual width to the common one
    if(p->kernel_sizes[i] < p->filter_size_real)
      p->kernel_sizes[i] = p->filter_size_real;

    // In a horizontal resizer, when reading filter_size_alignment pixels,
    // we must protect against source scanline overread.
    // Using this not in only 32-bit float resizers is new in 3.7.4.
    const int start_pos = p->pixel_offset[i];
    const int end_pos_aligned = start_pos + filter_size_aligned - 1;
    const int end_pos = start_pos + p->filter_size_real - 1;
    if (end_pos >= p->source_size) {
      // This issue has already been fixed, so it cannot occur.
    }

    // Check for SIMD optimization limits
    if (end_pos_aligned >= p->source_size) {
      if (!p->overread_possible) {
        // Register the first occurrence, because we are entering the danger zone from here.
        // Up to this point, template-based alignment-aware quick code can be used
        // in H resizers. But beyond this point an e.g. _mm256_loadu_si256() would read into 
        // invalid memory area at the end of the frame buffer.
        p->overread_possible = true;
        p->source_overread_offset = start_pos;
        p->source_overread_beyond_targetx = i; 
      }
    }
  }

  // Fill the extra offset after target_size with fake values.
  // Our aim is to have a safe, up to 8 pixels/cycle simd loop for V resizers.
  // Their coeffs will be 0, so they don't count if such coeffs
  // are multiplied with invalid pixels.
  if (p->target_size < target_size_aligned) {
    p->kernel_sizes.resize(target_size_aligned);
    p->pixel_offset.resize(target_size_aligned);
    for (int i = p->target_size; i < target_size_aligned; ++i) {
      p->kernel_sizes[i] = p->filter_size_real;
      p->pixel_offset[i] = 0; // 0th pixel offset makes no harm
    }
  }

  // Free old coefficients and assign new ones
  if (p->bits_per_pixel == 32) {
    env->Free(p->pixel_coefficient_float);
    p->pixel_coefficient_float = (float*)new_coeff;
  }
  else {
    env->Free(p->pixel_coefficient);
    p->pixel_coefficient = (short*)new_coeff;
  }

  p->filter_size = filter_size_aligned;
  // by now coeffs[old_filter_size][target_size] was copied and padded into coeffs[new_filter_size][target_size]
}

/***************************************
 ***** Vertical Resizer Assembly *******
 ***************************************/

template<typename pixel_t>
static void resize_v_planar_pointresize(BYTE* dst, const BYTE* src, int dst_pitch, int src_pitch, ResamplingProgram* program, int width, int target_height, int bits_per_pixel)
{
  AVS_UNUSED(bits_per_pixel);

  pixel_t* src0 = (pixel_t*)src;
  pixel_t* dst0 = (pixel_t*)dst;
  src_pitch = src_pitch / sizeof(pixel_t);
  dst_pitch = dst_pitch / sizeof(pixel_t);

  for (int y = 0; y < target_height; y++) {
    int offset = program->pixel_offset[y];
    const pixel_t* src_ptr = src0 + src_pitch * offset;

    memcpy(dst0, src_ptr, width * sizeof(pixel_t));

    dst0 += dst_pitch;
  }
}

// C versions: vectorizer friendly version giving 1.6-2.6x speed even with MSVC

template<typename pixel_t, bool lessthan16bit>
static void resize_v_c_planar(BYTE* dst8, const BYTE* src8, int dst_pitch, int src_pitch, ResamplingProgram* program, int width, int target_height, int bits_per_pixel)
{

  int filter_size = program->filter_size;

  typedef typename std::conditional < std::is_floating_point<pixel_t>::value, float, short>::type coeff_t;
  coeff_t* current_coeff;

  if (!std::is_floating_point<pixel_t>::value)
    current_coeff = (coeff_t*)program->pixel_coefficient;
  else
    current_coeff = (coeff_t*)program->pixel_coefficient_float;

  pixel_t* src = (pixel_t*)src8;
  pixel_t* dst = (pixel_t*)dst8;
  src_pitch = src_pitch / sizeof(pixel_t);
  dst_pitch = dst_pitch / sizeof(pixel_t);

  pixel_t limit = 0;
  if (!std::is_floating_point<pixel_t>::value) {  // floats are unscaled and uncapped
    if constexpr (sizeof(pixel_t) == 1) limit = 255;
    else if constexpr (sizeof(pixel_t) == 2) limit = pixel_t((1 << bits_per_pixel) - 1);
  }

  // for 16 bits only
  const short shifttosigned_short = -32768;
  const int shiftfromsigned_int = 32768 << FPScale16bits;

  for (int y = 0; y < target_height; y++) {
    int offset = program->pixel_offset[y];
    const int kernel_size = program->kernel_sizes[y];
    const pixel_t* src_ptr = src + src_pitch * offset;

    // perhaps helps vectorizing decision
    const int ksmod4 = kernel_size / 4 * 4;

    for (int x = 0; x < width; x++) {
      if constexpr (std::is_floating_point<pixel_t>::value) {
        const float* src2_ptr = src_ptr + x;

        float result = 0;
        for (int i = 0; i < ksmod4; i+=4) {
          result += *(src2_ptr + 0 * src_pitch) * current_coeff[i + 0];
          result += *(src2_ptr + 1 * src_pitch) * current_coeff[i + 1];
          result += *(src2_ptr + 2 * src_pitch) * current_coeff[i + 2];
          result += *(src2_ptr + 3 * src_pitch) * current_coeff[i + 3];
          src2_ptr += 4 * src_pitch;
        }
        for (int i = ksmod4; i < kernel_size; i++) {
          result += *src2_ptr * current_coeff[i];
          src2_ptr += src_pitch;
        }
        dst[x] = result;
      }
      else if constexpr (sizeof(pixel_t) == 2) {
        // theoretically, no need for int64 accumulator,
        // sum of coeffs is 1.0 that is (1 << FPScale16bits) in integer arithmetic
        const uint16_t* src2_ptr = src_ptr + x;
        int result = 1 << (FPScale16bits - 1); // rounder;
        for (int i = 0; i < ksmod4; i+=4) {
          int val;
          val = *(src2_ptr + 0 * src_pitch);
          if constexpr (!lessthan16bit)
            val = val + shifttosigned_short;
          result += val * current_coeff[i + 0];

          val = *(src2_ptr + 1 * src_pitch);
          if constexpr (!lessthan16bit)
            val = val + shifttosigned_short;
          result += val * current_coeff[i + 1];

          val = *(src2_ptr + 2 * src_pitch);
          if constexpr (!lessthan16bit)
            val = val + shifttosigned_short;
          result += val * current_coeff[i + 2];

          val = *(src2_ptr + 3 * src_pitch);
          if constexpr (!lessthan16bit)
            val = val + shifttosigned_short;
          result += val * current_coeff[i + 3];

          src2_ptr += 4 * src_pitch;
        }
        for (int i = ksmod4; i < kernel_size; i++) {
          int val = *src2_ptr;
          if constexpr (!lessthan16bit)
            val = val + shifttosigned_short;
          result += val * current_coeff[i];
          src2_ptr += src_pitch;
        }
        if constexpr (!lessthan16bit)
          result = result + shiftfromsigned_int;
        result = result >> FPScale16bits;
        result = result > limit ? limit : result < 0 ? 0 : result; // clamp 10..16 bits
        dst[x] = (uint16_t)result;
      }
      else if constexpr (sizeof(pixel_t) == 1) {
        const uint8_t* src2_ptr = src_ptr + x;
        int result = 1 << (FPScale8bits - 1); // rounder;
        for (int i = 0; i < ksmod4; i+=4) {
          short val;
          val = *(src2_ptr + 0 * src_pitch);
          result += val * current_coeff[i + 0];
          
          val = *(src2_ptr + 1 * src_pitch);
          result += val * current_coeff[i + 1];
          
          val = *(src2_ptr + 2 * src_pitch);
          result += val * current_coeff[i + 2];
          
          val = *(src2_ptr + 3 * src_pitch);
          result += val * current_coeff[i + 3];
          
          src2_ptr += 4 * src_pitch;
        }
        for (int i = ksmod4; i < kernel_size; i++) {
          short val = *src2_ptr;
          result += val * current_coeff[i];
          src2_ptr += src_pitch;
        }
        result = result >> FPScale8bits;
        result = result > limit ? limit : result < 0 ? 0 : result; // clamp 8 bits
        dst[x] = (uint8_t)result;
      }
    }

    dst += dst_pitch;
    current_coeff += filter_size;
  }
}

/***************************************
 ********* Horizontal Resizer** ********
 ***************************************/

template<typename pixel_t, bool lessthan16bit>
static void resize_h_c_planar(BYTE* dst8, const BYTE* src8, int dst_pitch, int src_pitch, ResamplingProgram* program, int width, int height, int bits_per_pixel) {
  int filter_size = program->filter_size;

  typedef typename std::conditional < std::is_floating_point<pixel_t>::value, float, short>::type coeff_t;
  coeff_t* current_coeff;

  pixel_t limit = 0;
  if (!std::is_floating_point<pixel_t>::value) {  // floats are unscaled and uncapped
    if constexpr (sizeof(pixel_t) == 1) limit = 255;
    else if constexpr (sizeof(pixel_t) == 2) limit = pixel_t((1 << bits_per_pixel) - 1);
  }

  src_pitch = src_pitch / sizeof(pixel_t);
  dst_pitch = dst_pitch / sizeof(pixel_t);

  pixel_t* src = (pixel_t*)src8;
  pixel_t* dst = (pixel_t*)dst8;

  // for 16 bits only
  const short shifttosigned_short = -32768;
  const int shiftfromsigned_int = 32768 << FPScale16bits;

  // perhaps helps vectorizing decision
  const int kernel_size = program->filter_size_real;
  const int ksmod4 = kernel_size / 4 * 4;
  const int ksmod8 = kernel_size / 8 * 8;

  // external loop y is much faster
  for (int y = 0; y < height; y++) {
    if (!std::is_floating_point<pixel_t>::value)
      current_coeff = (coeff_t*)program->pixel_coefficient;
    else
      current_coeff = (coeff_t*)program->pixel_coefficient_float;

    pixel_t* dst2_ptr = dst + y * dst_pitch;
    const pixel_t* src_ptr = src + y * src_pitch;

    for (int x = 0; x < width; x++) {
      int begin = program->pixel_offset[x];
      const pixel_t* src2_ptr = src_ptr + begin;

      if constexpr (std::is_floating_point<pixel_t>::value) {
        float result = 0;
        for (int i = 0; i < ksmod4; i+=4) {
          result += src2_ptr[i+0] * current_coeff[i+0];
          result += src2_ptr[i+1] * current_coeff[i+1];
          result += src2_ptr[i+2] * current_coeff[i+2];
          result += src2_ptr[i+3] * current_coeff[i+3];
        }
        for (int i = ksmod4; i < kernel_size; i++) {
          result += src2_ptr[i] * current_coeff[i];
        }
        dst2_ptr[x] = result;
      }
      else if constexpr (sizeof(pixel_t) == 2) {
        // theoretically, no need for int64 accumulator,
        // sum of coeffs is 1.0 that is (1 << FPScale16bits) in integer arithmetic
        int result = 1 << (FPScale16bits - 1); // rounder;
        for (int i = 0; i < ksmod4; i += 4) {
          int val;
          val = src2_ptr[i+0];
          if constexpr (!lessthan16bit)
            val = val + shifttosigned_short;
          result += val * current_coeff[i+0];

          val = src2_ptr[i+1];
          if constexpr (!lessthan16bit)
            val = val + shifttosigned_short;
          result += val * current_coeff[i+1];

          val = src2_ptr[i + 2];
          if constexpr (!lessthan16bit)
            val = val + shifttosigned_short;
          result += val * current_coeff[i + 2];

          val = src2_ptr[i + 3];
          if constexpr (!lessthan16bit)
            val = val + shifttosigned_short;
          result += val * current_coeff[i + 3];
        }
        for (int i = ksmod4; i < kernel_size; i++) {
          int val = src2_ptr[i];
          if constexpr (!lessthan16bit)
            val = val + shifttosigned_short;
          result += val * current_coeff[i];
        }
        if constexpr (!lessthan16bit)
          result = result + shiftfromsigned_int;
        result = result >> FPScale16bits;
        result = result > limit ? limit : result < 0 ? 0 : result; // clamp 10..16 bits
        dst2_ptr[x] = (uint16_t)result;
      }
      else if constexpr (sizeof(pixel_t) == 1) {
        int result = 1 << (FPScale8bits - 1); // rounder;
        for (int i = 0; i < ksmod8; i += 8) {
          short val;
          val = src2_ptr[i + 0];
          result += val * current_coeff[i + 0];
          val = src2_ptr[i + 1];
          result += val * current_coeff[i + 1];
          val = src2_ptr[i + 2];
          result += val * current_coeff[i + 2];
          val = src2_ptr[i + 3];
          result += val * current_coeff[i + 3];
          val = src2_ptr[i + 4];
          result += val * current_coeff[i + 4];
          val = src2_ptr[i + 5];
          result += val * current_coeff[i + 5];
          val = src2_ptr[i + 6];
          result += val * current_coeff[i + 6];
          val = src2_ptr[i + 7];
          result += val * current_coeff[i + 7];
        }
        for (int i = ksmod8; i < ksmod4; i += 4) {
          short val;
          val = src2_ptr[i + 0];
          result += val * current_coeff[i + 0];
          val = src2_ptr[i + 1];
          result += val * current_coeff[i + 1];
          val = src2_ptr[i + 2];
          result += val * current_coeff[i + 2];
          val = src2_ptr[i + 3];
          result += val * current_coeff[i + 3];
          val = src2_ptr[i + 4];
        }
        for (int i = ksmod4; i < kernel_size; i++) {
          short val = src2_ptr[i];
          result += val * current_coeff[i];
        }
        result = result >> FPScale8bits;
        result = result > limit ? limit : result < 0 ? 0 : result; // clamp 8 bits
        dst2_ptr[x] = (uint8_t)result;
      }
      current_coeff += filter_size;
    }
  }
}

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

FilteredResizeH::FilteredResizeH(PClip _child, double subrange_left, double subrange_width,
  int target_width, ResamplingFunction* func, bool preserve_center, int chroma_placement, IScriptEnvironment* env)
  : GenericVideoFilter(_child),
  resampling_program_luma(nullptr), resampling_program_chroma(nullptr), 
  resampler_h_chroma(nullptr), resampler_h_luma(nullptr),
  resampler_chroma(nullptr), resampler_luma(nullptr)

{
  src_width = vi.width;
  src_height = vi.height;
  dst_width = target_width;
  dst_height = vi.height;

  pixelsize = vi.ComponentSize(); // AVS16
  bits_per_pixel = vi.BitsPerComponent();
  grey = vi.IsY();

  bool isRGBPfamily = vi.IsPlanarRGB() || vi.IsPlanarRGBA();

  if (target_width <= 0) {
    env->ThrowError("Resize: Width must be greater than 0.");
  }

  if (vi.IsPlanar() && !grey && !isRGBPfamily) {
    const int mask = (1 << vi.GetPlaneWidthSubsampling(PLANAR_U)) - 1;

    if (target_width & mask)
      env->ThrowError("Resize: Planar destination height must be a multiple of %d.", mask + 1);
  }

  double center_pos_h_luma;
  double center_pos_h_chroma;
  GetCenterShiftForResizers(center_pos_h_luma, center_pos_h_chroma, preserve_center, chroma_placement, vi, true /* for horizontal */);
  // 3.7.4- parameter, old Avisynth behavior: 0.5, 0.5

  // Main resampling program
  resampling_program_luma = func->GetResamplingProgram(vi.width, subrange_left, subrange_width, target_width, bits_per_pixel, 
    center_pos_h_luma, center_pos_h_luma, // for resizing it's the same for source and dest
    env);
  if (vi.IsPlanar() && !grey && !isRGBPfamily) {
    const int shift = vi.GetPlaneWidthSubsampling(PLANAR_U);
    const int div = 1 << shift;


    resampling_program_chroma = func->GetResamplingProgram(
      vi.width >> shift,
      subrange_left / div,
      subrange_width / div,
      target_width >> shift,
      bits_per_pixel,
      center_pos_h_chroma, center_pos_h_chroma, // horizontal
      env);
  }

// when not fast_resize, then we use vertical resizers between turnleft/turnright
#ifdef INTEL_INTRINSICS
  int cpu = env->GetCPUFlags();
  bool has_sse2 = (cpu & CPUF_SSE2) != 0;
#else
  int cpu = 0;
#endif

  fast_resize = vi.IsPlanar();
  // PF 2025: H is not slower than V in C implementation.
  // Still, H resizers are incompatible with packed RGB formats

    if (!fast_resize) {

      // nonfast-resize: using V resizer for horizontal resizing between a turnleft/right

      resampler_luma = FilteredResizeV::GetResampler(cpu, pixelsize, bits_per_pixel, resampling_program_luma, env);

      if (vi.IsPlanar() && !grey && !isRGBPfamily) {
        resampler_chroma = FilteredResizeV::GetResampler(cpu, pixelsize, bits_per_pixel, resampling_program_chroma, env);
      }

      // Temporary buffer size for turns
      temp_1_pitch = AlignNumber(vi.BytesFromPixels(src_height), FRAME_ALIGN);
      temp_2_pitch = AlignNumber(vi.BytesFromPixels(dst_height), FRAME_ALIGN);

      // Initialize Turn function
      // see turn.cpp
      if (vi.IsRGB24()) {
#ifdef INTEL_INTRINSICS
        // no intel intentionally
#endif
        turn_left = turn_left_rgb24;
        turn_right = turn_right_rgb24;
      }
      else if (vi.IsRGB32()) {
#ifdef INTEL_INTRINSICS
        if (has_sse2) {
          turn_left = turn_left_rgb32_sse2;
          turn_right = turn_right_rgb32_sse2;
        }
        else
#endif
        {
          turn_left = turn_left_rgb32_c;
          turn_right = turn_right_rgb32_c;
        }
      }
      else if (vi.IsRGB48()) {
#ifdef INTEL_INTRINSICS
        // no intel intentionally
#endif
        turn_left = turn_left_rgb48_c;
        turn_right = turn_right_rgb48_c;
      }
      else if (vi.IsRGB64()) {
#ifdef INTEL_INTRINSICS
        if (has_sse2) {
          turn_left = turn_left_rgb64_sse2;
          turn_right = turn_right_rgb64_sse2;
        }
        else

#endif
        {
          turn_left = turn_left_rgb64_c;
          turn_right = turn_right_rgb64_c;
        }
      }
      else {
        switch (vi.ComponentSize()) {// AVS16
        case 1: // 8 bit
#ifdef INTEL_INTRINSICS
          if (has_sse2) {
            turn_left = turn_left_plane_8_sse2;
            turn_right = turn_right_plane_8_sse2;
          }
          else
#endif
          {
            turn_left = turn_left_plane_8_c;
            turn_right = turn_right_plane_8_c;
          }
          break;
        case 2: // 16 bit
#ifdef INTEL_INTRINSICS
          if (has_sse2) {
            turn_left = turn_left_plane_16_sse2;
            turn_right = turn_right_plane_16_sse2;
          }
          else
#endif
          {
            turn_left = turn_left_plane_16_c;
            turn_right = turn_right_plane_16_c;
          }
          break;
        default: // 32 bit
#ifdef INTEL_INTRINSICS
          if (has_sse2) {
            turn_left = turn_left_plane_32_sse2;
            turn_right = turn_right_plane_32_sse2;
          }
          else
#endif
          {
            turn_left = turn_left_plane_32_c;
            turn_right = turn_right_plane_32_c;
          }
        }
      }
    }
    else {
      // planar format (or Y)
      resampler_h_luma = GetResampler(cpu, pixelsize, bits_per_pixel, resampling_program_luma, env);

      if (!grey && !isRGBPfamily) {
        resampler_h_chroma = GetResampler(cpu, pixelsize, bits_per_pixel, resampling_program_chroma, env);
      }
    }
  // Change target video info size
  vi.width = target_width;
}

PVideoFrame __stdcall FilteredResizeH::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);

  bool isRGBPfamily = vi.IsPlanarRGB() || vi.IsPlanarRGBA();

  if (!fast_resize) {
    // e.g. not aligned, not mod4
    // temp_1_pitch and temp_2_pitch is pixelsize-aware
    BYTE* temp_1 = static_cast<BYTE*>(env->Allocate(temp_1_pitch * src_width, FRAME_ALIGN, AVS_POOLED_ALLOC));
    BYTE* temp_2 = static_cast<BYTE*>(env->Allocate(temp_2_pitch * dst_width, FRAME_ALIGN, AVS_POOLED_ALLOC));
    if (!temp_1 || !temp_2) {
      env->Free(temp_1);
      env->Free(temp_2);
      env->ThrowError("Could not reserve memory in a resampler.");
    }

    if (!vi.IsRGB() || isRGBPfamily) {
      // Y/G Plane
      turn_right(src->GetReadPtr(), temp_1, src_width * pixelsize, src_height, src->GetPitch(), temp_1_pitch); // * pixelsize: turn_right needs GetPlaneWidth full size
      resampler_luma(temp_2, temp_1, temp_2_pitch, temp_1_pitch, resampling_program_luma, src_height, dst_width, bits_per_pixel);
      turn_left(temp_2, dst->GetWritePtr(), dst_height * pixelsize, dst_width, temp_2_pitch, dst->GetPitch());

      if (isRGBPfamily)
      {
        turn_right(src->GetReadPtr(PLANAR_B), temp_1, src_width * pixelsize, src_height, src->GetPitch(PLANAR_B), temp_1_pitch); // * pixelsize: turn_right needs GetPlaneWidth full size
        resampler_luma(temp_2, temp_1, temp_2_pitch, temp_1_pitch, resampling_program_luma, src_height, dst_width, bits_per_pixel);
        turn_left(temp_2, dst->GetWritePtr(PLANAR_B), dst_height * pixelsize, dst_width, temp_2_pitch, dst->GetPitch(PLANAR_B));

        turn_right(src->GetReadPtr(PLANAR_R), temp_1, src_width * pixelsize, src_height, src->GetPitch(PLANAR_R), temp_1_pitch); // * pixelsize: turn_right needs GetPlaneWidth full size
        resampler_luma(temp_2, temp_1, temp_2_pitch, temp_1_pitch, resampling_program_luma, src_height, dst_width, bits_per_pixel);
        turn_left(temp_2, dst->GetWritePtr(PLANAR_R), dst_height * pixelsize, dst_width, temp_2_pitch, dst->GetPitch(PLANAR_R));
      }
      else if (!grey) {
        const int shift = vi.GetPlaneWidthSubsampling(PLANAR_U);
        const int shift_h = vi.GetPlaneHeightSubsampling(PLANAR_U);

        const int src_chroma_width = src_width >> shift;
        const int dst_chroma_width = dst_width >> shift;
        const int src_chroma_height = src_height >> shift_h;
        const int dst_chroma_height = dst_height >> shift_h;

        // turn_xxx: width * pixelsize: needs GetPlaneWidth-like full size
        // U Plane
        turn_right(src->GetReadPtr(PLANAR_U), temp_1, src_chroma_width * pixelsize, src_chroma_height, src->GetPitch(PLANAR_U), temp_1_pitch);
        resampler_luma(temp_2, temp_1, temp_2_pitch, temp_1_pitch, resampling_program_chroma, src_chroma_height, dst_chroma_width, bits_per_pixel);
        turn_left(temp_2, dst->GetWritePtr(PLANAR_U), dst_chroma_height * pixelsize, dst_chroma_width, temp_2_pitch, dst->GetPitch(PLANAR_U));

        // V Plane
        turn_right(src->GetReadPtr(PLANAR_V), temp_1, src_chroma_width * pixelsize, src_chroma_height, src->GetPitch(PLANAR_V), temp_1_pitch);
        resampler_luma(temp_2, temp_1, temp_2_pitch, temp_1_pitch, resampling_program_chroma, src_chroma_height, dst_chroma_width, bits_per_pixel);
        turn_left(temp_2, dst->GetWritePtr(PLANAR_V), dst_chroma_height * pixelsize, dst_chroma_width, temp_2_pitch, dst->GetPitch(PLANAR_V));
      }
      if (vi.IsYUVA() || vi.IsPlanarRGBA())
      {
        turn_right(src->GetReadPtr(PLANAR_A), temp_1, src_width * pixelsize, src_height, src->GetPitch(PLANAR_A), temp_1_pitch); // * pixelsize: turn_right needs GetPlaneWidth full size
        resampler_luma(temp_2, temp_1, temp_2_pitch, temp_1_pitch, resampling_program_luma, src_height, dst_width, bits_per_pixel);
        turn_left(temp_2, dst->GetWritePtr(PLANAR_A), dst_height * pixelsize, dst_width, temp_2_pitch, dst->GetPitch(PLANAR_A));
      }

    }
    else {
      // packed RGB
      // First left, then right. Reason: packed RGB bottom to top. Right+left shifts RGB24/RGB32 image to the opposite horizontal direction
      turn_left(src->GetReadPtr(), temp_1, vi.BytesFromPixels(src_width), src_height, src->GetPitch(), temp_1_pitch);
      resampler_luma(temp_2, temp_1, temp_2_pitch, temp_1_pitch, resampling_program_luma, vi.BytesFromPixels(src_height) / pixelsize, dst_width, bits_per_pixel);
      turn_right(temp_2, dst->GetWritePtr(), vi.BytesFromPixels(dst_height), dst_width, temp_2_pitch, dst->GetPitch());
    }

    env->Free(temp_1);
    env->Free(temp_2);
  }
  else {

    // Y Plane
    resampler_h_luma(dst->GetWritePtr(), src->GetReadPtr(), dst->GetPitch(), src->GetPitch(), resampling_program_luma, dst_width, dst_height, bits_per_pixel);

    if (isRGBPfamily) {
      resampler_h_luma(dst->GetWritePtr(PLANAR_B), src->GetReadPtr(PLANAR_B), dst->GetPitch(PLANAR_B), src->GetPitch(PLANAR_B), resampling_program_luma, dst_width, dst_height, bits_per_pixel);
      resampler_h_luma(dst->GetWritePtr(PLANAR_R), src->GetReadPtr(PLANAR_R), dst->GetPitch(PLANAR_R), src->GetPitch(PLANAR_R), resampling_program_luma, dst_width, dst_height, bits_per_pixel);
    }
    else if (!grey) {
      const int dst_chroma_width = dst_width >> vi.GetPlaneWidthSubsampling(PLANAR_U);
      const int dst_chroma_height = dst_height >> vi.GetPlaneHeightSubsampling(PLANAR_U);

      // U Plane
      resampler_h_chroma(dst->GetWritePtr(PLANAR_U), src->GetReadPtr(PLANAR_U), dst->GetPitch(PLANAR_U), src->GetPitch(PLANAR_U), resampling_program_chroma, dst_chroma_width, dst_chroma_height, bits_per_pixel);

      // V Plane
      resampler_h_chroma(dst->GetWritePtr(PLANAR_V), src->GetReadPtr(PLANAR_V), dst->GetPitch(PLANAR_V), src->GetPitch(PLANAR_V), resampling_program_chroma, dst_chroma_width, dst_chroma_height, bits_per_pixel);
    }
    if (vi.IsYUVA() || vi.IsPlanarRGBA())
    {
      resampler_h_luma(dst->GetWritePtr(PLANAR_A), src->GetReadPtr(PLANAR_A), dst->GetPitch(PLANAR_A), src->GetPitch(PLANAR_A), resampling_program_luma, dst_width, dst_height, bits_per_pixel);
    }

  }

  return dst;
}

ResamplerH FilteredResizeH::GetResampler(int CPU, int pixelsize, int bits_per_pixel, ResamplingProgram* program, IScriptEnvironment* env)
{
  // even for plain C, maybe once we write more vectorizer compiler-friendly code
  int simd_coeff_count_padding = 8;
#ifdef INTEL_INTRINSICS
  if (CPU & CPUF_SSSE3) {
    // both 8 and 16 bit SSSE3 and AVX2 horizontal resizer benefits from 16 pixels/cycle
    // float is also using 32 bytes, but as 32/sizeof(float) = 8, then don't need 16
    if (pixelsize == 1 || pixelsize == 2)
      simd_coeff_count_padding = 16;
  }
#endif
  // not only prepares and pads for SIMD, but corrects and reorders
  // coeffs at the right/bottom end, since we have variable kernel size
  // because of boundary conditions
  resize_prepare_coeffs(program, env, simd_coeff_count_padding);

  if (pixelsize == 1)
  {
#ifdef INTEL_INTRINSICS
    if (CPU & CPUF_AVX2) {
      return resizer_h_avx2_generic_uint8_t;
    }
    if (CPU & CPUF_SSSE3) {
      return resizer_h_ssse3_generic;
    }
#endif
    return resize_h_c_planar<uint8_t, 1>;
  }
  else if (pixelsize == 2) {
#ifdef INTEL_INTRINSICS
    if (CPU & CPUF_AVX2) {
      if (bits_per_pixel < 16)
        return resizer_h_avx2_generic_uint16_t<true>;
      else
        return resizer_h_avx2_generic_uint16_t<false>;
    }
    if (CPU & CPUF_SSSE3) {
      if (bits_per_pixel < 16)
        return resizer_h_ssse3_generic_uint16_t<true>;
      else
        return resizer_h_ssse3_generic_uint16_t<false>;
    }
#endif
    if (bits_per_pixel == 16)
      return resize_h_c_planar<uint16_t, 0>;
    else
      return resize_h_c_planar<uint16_t, 1>;
  }
  else { //if (pixelsize == 4)
#ifdef INTEL_INTRINSICS
    if (CPU & CPUF_AVX2) {
      return resizer_h_avx2_generic_float;
    }
    if (CPU & CPUF_SSSE3) {
      return resizer_h_ssse3_generic_float;
    }
#endif
    return resize_h_c_planar<float, 0>;
  }
}

FilteredResizeH::~FilteredResizeH(void)
{
  if (resampling_program_luma) { delete resampling_program_luma; }
  if (resampling_program_chroma) { delete resampling_program_chroma; }
}

/***************************************
 ***** Filtered Resize - Vertical ******
 ***************************************/

FilteredResizeV::FilteredResizeV(PClip _child, double subrange_top, double subrange_height,
  int target_height, ResamplingFunction* func, 
  bool preserve_center, int chroma_placement,
  IScriptEnvironment* env)
  : GenericVideoFilter(_child),
  resampling_program_luma(0), resampling_program_chroma(0)
{
  if (target_height <= 0)
    env->ThrowError("Resize: Height must be greater than 0.");

  pixelsize = vi.ComponentSize(); // AVS16
  bits_per_pixel = vi.BitsPerComponent();
  grey = vi.IsY();
  bool isRGBPfamily = vi.IsPlanarRGB() || vi.IsPlanarRGBA();

  if (vi.IsPlanar() && !grey && !isRGBPfamily) {
    const int mask = (1 << vi.GetPlaneHeightSubsampling(PLANAR_U)) - 1;

    if (target_height & mask)
      env->ThrowError("Resize: Planar destination height must be a multiple of %d.", mask + 1);
  }

  if (vi.IsRGB() && !isRGBPfamily)
    subrange_top = vi.height - subrange_top - subrange_height; // packed RGB upside down

#ifdef INTEL_INTRINSICS
  int cpu = env->GetCPUFlags();
#else
  int cpu = 0;
#endif

  double center_pos_v_luma;
  double center_pos_v_chroma;
  GetCenterShiftForResizers(center_pos_v_luma, center_pos_v_chroma, preserve_center, chroma_placement, vi, false /* for vertical */);
  // 3.7.4- parameter, old Avisynth behavior: 0.5, 0.5

  // Create resampling program and pitch table
  resampling_program_luma = func->GetResamplingProgram(vi.height, subrange_top, subrange_height, target_height, bits_per_pixel, 
    center_pos_v_luma, center_pos_v_luma, // for resizing it's the same for source and dest
    env);
  resampler_luma = GetResampler(cpu, pixelsize, bits_per_pixel, resampling_program_luma, env);

  if (vi.IsPlanar() && !grey && !isRGBPfamily) {
    const int shift = vi.GetPlaneHeightSubsampling(PLANAR_U);
    const int div = 1 << shift;

    resampling_program_chroma = func->GetResamplingProgram(
      vi.height >> shift,
      subrange_top / div,
      subrange_height / div,
      target_height >> shift,
      bits_per_pixel,
      center_pos_v_chroma, center_pos_v_chroma, // for resizing it's the same for source and dest
      env);

    resampler_chroma = GetResampler(cpu, pixelsize, bits_per_pixel, resampling_program_chroma, env);
  }

  // Change target video info size
  vi.height = target_height;
}

PVideoFrame __stdcall FilteredResizeV::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);
  int src_pitch = src->GetPitch();
  int dst_pitch = dst->GetPitch();
  const BYTE* srcp = src->GetReadPtr();
  BYTE* dstp = dst->GetWritePtr();

  bool isRGBPfamily = vi.IsPlanarRGB() || vi.IsPlanarRGBA();

  // Do resizing
  int work_width = vi.IsPlanar() ? vi.width : vi.BytesFromPixels(vi.width) / pixelsize; // packed RGB: or vi.width * vi.NumComponent()
  resampler_luma(dstp, srcp, dst_pitch, src_pitch, resampling_program_luma, work_width, vi.height, bits_per_pixel);
  if (isRGBPfamily)
  {
    src_pitch = src->GetPitch(PLANAR_B);
    dst_pitch = dst->GetPitch(PLANAR_B);
    srcp = src->GetReadPtr(PLANAR_B);
    dstp = dst->GetWritePtr(PLANAR_B);
    
    resampler_luma(dstp, srcp, dst_pitch, src_pitch, resampling_program_luma, work_width, vi.height, bits_per_pixel);
    
    src_pitch = src->GetPitch(PLANAR_R);
    dst_pitch = dst->GetPitch(PLANAR_R);
    srcp = src->GetReadPtr(PLANAR_R);
    dstp = dst->GetWritePtr(PLANAR_R);

    resampler_luma(dstp, srcp, dst_pitch, src_pitch, resampling_program_luma, work_width, vi.height, bits_per_pixel);
  }
  else if (!grey && vi.IsPlanar()) {
    int width = vi.width >> vi.GetPlaneWidthSubsampling(PLANAR_U);
    int height = vi.height >> vi.GetPlaneHeightSubsampling(PLANAR_U);

    // Plane U resizing
    src_pitch = src->GetPitch(PLANAR_U);
    dst_pitch = dst->GetPitch(PLANAR_U);
    srcp = src->GetReadPtr(PLANAR_U);
    dstp = dst->GetWritePtr(PLANAR_U);

    resampler_chroma(dstp, srcp, dst_pitch, src_pitch, resampling_program_chroma, width, height, bits_per_pixel);

    // Plane V resizing
    src_pitch = src->GetPitch(PLANAR_V);
    dst_pitch = dst->GetPitch(PLANAR_V);
    srcp = src->GetReadPtr(PLANAR_V);
    dstp = dst->GetWritePtr(PLANAR_V);

    resampler_chroma(dstp, srcp, dst_pitch, src_pitch, resampling_program_chroma, width, height, bits_per_pixel);
  }

  if (vi.IsYUVA() || vi.IsPlanarRGBA()) {
    src_pitch = src->GetPitch(PLANAR_A);
    dst_pitch = dst->GetPitch(PLANAR_A);
    srcp = src->GetReadPtr(PLANAR_A);
    dstp = dst->GetWritePtr(PLANAR_A);
    resampler_luma(dstp, srcp, dst_pitch, src_pitch, resampling_program_luma, work_width, vi.height, bits_per_pixel);
  }

  return dst;
}

ResamplerV FilteredResizeV::GetResampler(int CPU, int pixelsize, int bits_per_pixel, ResamplingProgram* program, IScriptEnvironment* env)
{

  resize_prepare_coeffs(program, env, 8); 
  // for SIMD friendliness and more: consolidate the kernel_size vs filter_size at the end.
  // See comments at FilteredResizeH::GetResampler

  if (program->filter_size == 1) {
    // Fast pointresize
    switch (pixelsize) // AVS16
    {
    case 1: return resize_v_planar_pointresize<uint8_t>;
    case 2: return resize_v_planar_pointresize<uint16_t>;
    default: // case 4:
      return resize_v_planar_pointresize<float>;
    }
  }
  else {
    // Other resizers
    if (pixelsize == 1)
    {
#ifdef INTEL_INTRINSICS
      if (CPU & CPUF_AVX2)
        return resize_v_avx2_planar_uint8_t;
      if (CPU & CPUF_SSE2)
        return resize_v_sse2_planar;
#ifdef X86_32
      if (CPU & CPUF_MMX)
        return resize_v_mmx_planar;
#endif
#endif
      // C version
      return resize_v_c_planar<uint8_t, 1>;
    }
    else if (pixelsize == 2)
    {
#ifdef INTEL_INTRINSICS
      if (CPU & CPUF_AVX2) {
        if (bits_per_pixel < 16)
          return resize_v_avx2_planar_uint16_t<true>;
        else
          return resize_v_avx2_planar_uint16_t<false>;
      }
      if (CPU & CPUF_SSE2) {
        if (bits_per_pixel < 16)
          return resize_v_sse2_planar_uint16_t<true>;
        else
          return resize_v_sse2_planar_uint16_t<false>;
      }
#endif
      // C version
      if(bits_per_pixel == 16)
        return resize_v_c_planar<uint16_t, 0>;
      else
        return resize_v_c_planar<uint16_t, 1>;
    }
    else // pixelsize== 4
    {
#ifdef INTEL_INTRINSICS
      if (CPU & CPUF_AVX2) {
        return resize_v_avx2_planar_float;
      }
      if (CPU & CPUF_SSE2) {
        return resize_v_sse2_planar_float;
      }
#endif
      return resize_v_c_planar<float, 0>;
    }
  }
}

FilteredResizeV::~FilteredResizeV(void)
{
  if (resampling_program_luma) { delete resampling_program_luma; }
  if (resampling_program_chroma) { delete resampling_program_chroma; }
}


/**********************************************
 *******   Resampling Factory Methods   *******
 **********************************************/

PClip FilteredResize::CreateResizeH(PClip clip, double subrange_left, double subrange_width, int target_width, bool force,
  ResamplingFunction* func, bool preserve_center, int chroma_placement, IScriptEnvironment* env)
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
  // Convert interleaved yuv to planar yuv
  PClip result = clip;
  if (vi.IsYUY2()) {
    result = new ConvertYUY2ToYV16(result, env);
  }

  result = new FilteredResizeH(result, subrange_left, subrange_width, target_width, func, preserve_center, chroma_placement, env);
  
  if (vi.IsYUY2()) {
    result = new ConvertYV16ToYUY2(result, env);
  }

  return result;
}


PClip FilteredResize::CreateResizeV(PClip clip, double subrange_top, double subrange_height, int target_height, bool force,
  ResamplingFunction* func, bool preserve_center, int chroma_placement, IScriptEnvironment* env)
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
  return new FilteredResizeV(clip, subrange_top, subrange_height, target_height, func, preserve_center, chroma_placement, env);
}


PClip FilteredResize::CreateResize(PClip clip, int target_width, int target_height, const AVSValue* args, int force,
  ResamplingFunction* f, 
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
  auto f = PointFilter();
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}


AVSValue __cdecl FilteredResize::Create_BilinearResize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = TriangleFilter();
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}


AVSValue __cdecl FilteredResize::Create_BicubicResize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = MitchellNetravaliFilter(args[3].AsDblDef(1. / 3.), args[4].AsDblDef(1. / 3.));
  const int force = args[9].AsInt(0);

  bool preserve_center = args[10].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[11].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[5], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_LanczosResize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = LanczosFilter(args[7].AsInt(3));
  const int force = args[8].AsInt(0);

  bool preserve_center = args[9].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[10].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_Lanczos4Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = LanczosFilter(4);
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_BlackmanResize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = BlackmanFilter(args[7].AsInt(4));
  const int force = args[8].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_Spline16Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = Spline16Filter();
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_Spline36Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = Spline36Filter();
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_Spline64Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = Spline64Filter();
  const int force = args[7].AsInt(0);

  bool preserve_center = args[8].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[9].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_GaussianResize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = GaussianFilter(args[7].AsFloat(30.0f), args[8].AsFloat(2.0f), args[9].AsFloat(4.0f)); // defaults at two more places
  const int force = args[10].AsInt(0);

  bool preserve_center = args[11].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[12].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

AVSValue __cdecl FilteredResize::Create_SincResize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = SincFilter(args[7].AsInt(4));
  const int force = args[8].AsInt(0);
 
  bool preserve_center = args[9].AsBool(true); // [keep_center] default Avisynth
  const char * placement_name = args[10].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

// like GaussianFilter(); optional P
AVSValue __cdecl FilteredResize::Create_SinPowerResize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = SinPowerFilter(args[7].AsFloat(2.5f));
  const int force = args[8].AsInt(0);

  bool preserve_center = args[9].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[10].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

// like SincFilter or LanczosFilter: optional Taps
AVSValue __cdecl FilteredResize::Create_SincLin2Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = SincLin2Filter(args[7].AsInt(15));
  const int force = args[8].AsInt(0);

  bool preserve_center = args[9].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[10].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[3], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

// like bicubic, plus 's'upport: optional B and C and S
AVSValue __cdecl FilteredResize::Create_UserDefined2Resize(AVSValue args, void*, IScriptEnvironment* env)
{
  auto f = UserDefined2Filter(args[3].AsFloat(121.0f), args[4].AsFloat(19.0f), args[5].AsFloat(2.3f));
  const int force = args[10].AsInt(0);

  bool preserve_center = args[11].AsBool(true); // [keep_center] default Avisynth
  const char* placement_name = args[12].AsString("auto"); // [placement]s
  const int forced_chroma_placement = -1; // no force, used internally

  return CreateResize(args[0].AsClip(), args[1].AsInt(), args[2].AsInt(), &args[6], force, &f, preserve_center, placement_name, forced_chroma_placement, env);
}

