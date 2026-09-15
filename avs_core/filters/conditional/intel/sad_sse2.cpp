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

#include "sad_sse2.h"
#include <emmintrin.h>
#include <cstdlib>
template<typename pixel_t, bool packedRGB3264>
int64_t calculate_sad_8_or_16_sse2(const BYTE* cur_ptr, const BYTE* other_ptr, int cur_pitch, int other_pitch, size_t rowsize, size_t height)
{
  size_t mod16_width = rowsize / 16 * 16;

  __m128i zero = _mm_setzero_si128();
  int64_t totalsum = 0; // fullframe SAD exceeds int32 at 8+ bit

  __m128i rgb_mask;
  if (packedRGB3264) {
    if constexpr(sizeof(pixel_t) == 1)
      rgb_mask = _mm_set1_epi32(0x00FFFFFF);
    else
      rgb_mask = _mm_set_epi32(0x0000FFFF,0xFFFFFFFF,0x0000FFFF,0xFFFFFFFF);
  }

  for ( size_t y = 0; y < height; y++ )
  {
    __m128i sum = _mm_setzero_si128(); // for one row int is enough
    for ( size_t x = 0; x < mod16_width; x+=16 )
    {
      __m128i src1, src2;
      src1 = _mm_load_si128((__m128i *) (cur_ptr + x));   // 16 bytes or 8 words
      src2 = _mm_load_si128((__m128i *) (other_ptr + x));
      if (packedRGB3264) {
        src1 = _mm_and_si128(src1, rgb_mask); // mask out A channel
        src2 = _mm_and_si128(src2, rgb_mask);
      }
      if constexpr(sizeof(pixel_t) == 1) {
        // this is uint_16 specific, but leave here for sample
        sum = _mm_add_epi32(sum, _mm_sad_epu8(src1, src2)); // sum0_32, 0, sum1_32, 0
      }
      else if constexpr(sizeof(pixel_t) == 2) {
        __m128i greater_t = _mm_subs_epu16(src1, src2); // unsigned sub with saturation
        __m128i smaller_t = _mm_subs_epu16(src2, src1);
        __m128i absdiff = _mm_or_si128(greater_t, smaller_t); //abs(s1-s2)  == (satsub(s1,s2) | satsub(s2,s1))
        // 8 x uint16 absolute differences
        sum = _mm_add_epi32(sum, _mm_unpacklo_epi16(absdiff, zero));
        sum = _mm_add_epi32(sum, _mm_unpackhi_epi16(absdiff, zero));
        // sum0_32, sum1_32, sum2_32, sum3_32
      }
    }
    // summing up partial sums
    if constexpr(sizeof(pixel_t) == 2) {
      // at 16 bits: we have 4 integers for sum: a0 a1 a2 a3
      __m128i a0_a1 = _mm_unpacklo_epi32(sum, zero); // a0 0 a1 0
      __m128i a2_a3 = _mm_unpackhi_epi32(sum, zero); // a2 0 a3 0
      sum = _mm_add_epi32( a0_a1, a2_a3 ); // a0+a2, 0, a1+a3, 0
      /* SSSE3: told to be not too fast
      sum = _mm_hadd_epi32(sum, zero);  // A1+A2, B1+B2, 0+0, 0+0
      sum = _mm_hadd_epi32(sum, zero);  // A1+A2+B1+B2, 0+0+0+0, 0+0+0+0, 0+0+0+0
      */
    }

    // sum here: two 32 bit partial result: sum1 0 sum2 0
    __m128i sum_hi = _mm_unpackhi_epi64(sum, zero);
    // or: __m128i sum_hi = _mm_castps_si128(_mm_movehl_ps(_mm_setzero_ps(), _mm_castsi128_ps(sum)));
    sum = _mm_add_epi32(sum, sum_hi);
    int rowsum = _mm_cvtsi128_si32(sum);

    // rest
    if (mod16_width != rowsize) {
      if (packedRGB3264)
        for (size_t x = mod16_width / sizeof(pixel_t) / 4; x < rowsize / sizeof(pixel_t) / 4; ++x) {
          rowsum += std::abs(reinterpret_cast<const pixel_t *>(cur_ptr)[x*4+0] - reinterpret_cast<const pixel_t *>(other_ptr)[x*4+0]) +
            std::abs(reinterpret_cast<const pixel_t *>(cur_ptr)[x*4+1] - reinterpret_cast<const pixel_t *>(other_ptr)[x*4+1]) +
            std::abs(reinterpret_cast<const pixel_t *>(cur_ptr)[x*4+2] - reinterpret_cast<const pixel_t *>(other_ptr)[x*4+2]);
          // no alpha
        }
      else
        for (size_t x = mod16_width / sizeof(pixel_t); x < rowsize / sizeof(pixel_t); ++x) {
          rowsum += std::abs(reinterpret_cast<const pixel_t *>(cur_ptr)[x] - reinterpret_cast<const pixel_t *>(other_ptr)[x]);
        }
    }

    totalsum += rowsum;

    cur_ptr += cur_pitch;
    other_ptr += other_pitch;
  }
  return totalsum;
}

// instantiate
template int64_t calculate_sad_8_or_16_sse2<uint8_t, false>(const BYTE* cur_ptr, const BYTE* other_ptr, int cur_pitch, int other_pitch, size_t rowsize, size_t height);
template int64_t calculate_sad_8_or_16_sse2<uint8_t, true>(const BYTE* cur_ptr, const BYTE* other_ptr, int cur_pitch, int other_pitch, size_t rowsize, size_t height);
template int64_t calculate_sad_8_or_16_sse2<uint16_t, false>(const BYTE* cur_ptr, const BYTE* other_ptr, int cur_pitch, int other_pitch, size_t rowsize, size_t height);
template int64_t calculate_sad_8_or_16_sse2<uint16_t, true>(const BYTE* cur_ptr, const BYTE* other_ptr, int cur_pitch, int other_pitch, size_t rowsize, size_t height);
