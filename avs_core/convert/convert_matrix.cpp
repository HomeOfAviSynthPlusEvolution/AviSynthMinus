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


#include "convert_matrix.h"
#include "convert_helper.h"
#include <avisynth.h>
#ifdef AVS_WINDOWS
#include <avs/win.h>
#else
#include <avs/posix.h>
#endif 

static void BuildMatrix_Rgb2Yuv_core(double Kr, double Kb, int int_arith_shift, bool full_scale_s, bool full_scale_d, int bits_per_pixel, ConversionMatrix& matrix)
{
  int Sy, Suv, Oy, Orgb, cmin = 0, cmax = 0;
  float Sy_f, Suv_f, Oy_f, Orgb_f;

  if (bits_per_pixel <= 16) {
    Oy = full_scale_d ? 0 : (16 << (bits_per_pixel - 8));
    Oy_f = (float)Oy; // for 16 bits

    Orgb = full_scale_s ? 0 : (16 << (bits_per_pixel - 8));
    Orgb_f = (float)Orgb; // for 16 bits

    int ymin = (full_scale_d ? 0 : 16) << (bits_per_pixel - 8);
    int max_pixel_value = (1 << bits_per_pixel) - 1;
    int ymax = full_scale_d ? max_pixel_value : (235 << (bits_per_pixel - 8));
    Sy = ymax - ymin;
    Sy_f = (float)Sy;

    cmin = full_scale_d ? 0 : (16 << (bits_per_pixel - 8));
    cmax = full_scale_d ? max_pixel_value : (240 << (bits_per_pixel - 8));
    Suv = (cmax - cmin) / 2;
    Suv_f = (cmax - cmin) / 2.0f;

  }
  else {
    Oy_f = full_scale_d ? 0.0f : (16.0f / 255.0f);
    Oy = full_scale_d ? 0 : 16; // n/a

    Orgb_f = full_scale_s ? 0.0f : (16.0f / 255.0f);
    Orgb = full_scale_s ? 0 : 16; // n/a

    Sy_f = full_scale_d ? c8tof(255) : (c8tof(235) - c8tof(16));
    Suv_f = full_scale_d ? (0.5f - -0.5f) / 2 : (uv8tof(240) - uv8tof(16)) / 2;
  }


  /*
    Kr   = {0.299, 0.2126}
    Kb   = {0.114, 0.0722}
    Kg   = 1 - Kr - Kb // {0.587, 0.7152}
    Srgb = 255
    Sy   = {219, 255}   // { 235-16, 255-0 }
    Suv  = {112, 127}   // { (240-16)/2, (255-0)/2 }
    Oy   = {16, 0}
    Ouv  = 128

    R = r/Srgb                     // 0..1
    G = g/Srgb
    B = b*Srgb

    Y = Kr*R + Kg*G + Kb*B         // 0..1
    U = B - (Kr*R + Kg*G)/(1-Kb)   //-1..1
    V = R - (Kg*G + Kb*B)/(1-Kr)

    y = Y*Sy  + Oy                 // 16..235, 0..255
    u = U*Suv + Ouv                // 16..240, 1..255
    v = V*Suv + Ouv
  */
  const int mulfac_int = 1 << int_arith_shift;
  const double mulfac = double(mulfac_int); // integer aritmetic precision scale

  const double Kg = 1. - Kr - Kb;

  if (bits_per_pixel <= 16) {
    const auto Srgb = (full_scale_s ? double((1 << bits_per_pixel) - 1) : double(219 << (bits_per_pixel - 8)));
    const double Suv_d = (cmax - cmin) / 2.0;
    matrix.y_b = (int)(Sy * Kb * mulfac / Srgb + 0.5); //B
    matrix.y_g = (int)(Sy * Kg * mulfac / Srgb + 0.5); //G
    matrix.y_r = (int)(Sy * Kr * mulfac / Srgb + 0.5); //R
    matrix.u_b = (int)(Suv_d * mulfac / Srgb + 0.5);
    matrix.u_g = (int)(Suv_d * Kg / (Kb - 1) * mulfac / Srgb + 0.5);
    matrix.u_r = (int)(Suv_d * Kr / (Kb - 1) * mulfac / Srgb + 0.5);
    matrix.v_b = (int)(Suv_d * Kb / (Kr - 1) * mulfac / Srgb + 0.5);
    matrix.v_g = (int)(Suv_d * Kg / (Kr - 1) * mulfac / Srgb + 0.5);
    matrix.v_r = (int)(Suv_d * mulfac / Srgb + 0.5);

    matrix.offset_y = Oy;
    matrix.offset_rgb = -Orgb; // yes, minus, because addition is used

    // Preserve the rounded total luma gain, including limited RGB expansion.
    // For a full-range source this is unity; a limited source needs Sy/Srgb.
    if (matrix.offset_y == 0) {
      const int luma_sum = (int)(Sy * mulfac / Srgb + 0.5);
      matrix.y_g = luma_sum - (matrix.y_r + matrix.y_b);
    }

  }

  // for 16 bits, float is used, no unsigned 16 bit arithmetic
  double Srgb_f = (bits_per_pixel == 32) ? (full_scale_s ? 1.0 : 219.0 / 255.0)
    : (full_scale_s ? double((1 << bits_per_pixel) - 1) : double(219 << (bits_per_pixel - 8)));
  matrix.y_b_f = (float)(Sy_f * Kb / Srgb_f); //B
  matrix.y_g_f = (float)(Sy_f * Kg / Srgb_f); //G
  matrix.y_r_f = (float)(Sy_f * Kr / Srgb_f); //R
  matrix.u_b_f = (float)(Suv_f / Srgb_f);
  matrix.u_g_f = (float)(Suv_f * Kg / (Kb - 1) / Srgb_f);
  matrix.u_r_f = (float)(Suv_f * Kr / (Kb - 1) / Srgb_f);
  matrix.v_b_f = (float)(Suv_f * Kb / (Kr - 1) / Srgb_f);
  matrix.v_g_f = (float)(Suv_f * Kg / (Kr - 1) / Srgb_f);
  matrix.v_r_f = (float)(Suv_f / Srgb_f);
  matrix.offset_y_f = Oy_f;
  matrix.offset_rgb_f = -Orgb_f; // yes, minus, because addition is used
}

static void BuildMatrix_Yuv2Rgb_core(double Kr, double Kb, int int_arith_shift, bool full_scale_s, bool full_scale_d, int bits_per_pixel, ConversionMatrix& matrix)
{
  int Sy, Suv, Oy, Orgb, cmin = 0, cmax = 0;
  float Sy_f, Suv_f, Oy_f, Orgb_f;

  if (bits_per_pixel <= 16) {
    Oy = full_scale_s ? 0 : (16 << (bits_per_pixel - 8));
    Oy_f = (float)Oy; // for 16 bits

    Orgb = full_scale_d ? 0 : (16 << (bits_per_pixel - 8));
    Orgb_f = (float)Orgb; // for 16 bits

    int ymin = (full_scale_s ? 0 : 16) << (bits_per_pixel - 8);
    int max_pixel_value = (1 << bits_per_pixel) - 1;
    int ymax = full_scale_s ? max_pixel_value : (235 << (bits_per_pixel - 8));
    Sy = ymax - ymin;
    Sy_f = (float)Sy;

    cmin = full_scale_s ? 0 : (16 << (bits_per_pixel - 8));
    cmax = full_scale_s ? max_pixel_value : (240 << (bits_per_pixel - 8));
    Suv = (cmax - cmin) / 2;
    Suv_f = (cmax - cmin) / 2.0f;
  }
  else {
    Oy_f = full_scale_s ? 0.0f : (16.0f / 255.0f);
    Oy = full_scale_s ? 0 : 16; // n/a

    Orgb_f = full_scale_d ? 0.0f : (16.0f / 255.0f);
    Orgb = full_scale_d ? 0 : 16; // n/a

    Sy_f = full_scale_s ? c8tof(255) : (c8tof(235) - c8tof(16));
    Suv_f = full_scale_s ? (0.5f - -0.5f) / 2 : (uv8tof(240) - uv8tof(16)) / 2;
  }


  /*
    Kr   = {0.299, 0.2126}
    Kb   = {0.114, 0.0722}
    Kg   = 1 - Kr - Kb // {0.587, 0.7152}
    Srgb = 255
    Sy   = {219, 255}   // { 235-16, 255-0 }
    Suv  = {112, 127}   // { (240-16)/2, (255-0)/2 }
    Oy   = {16, 0}
    Ouv  = 128

    Y =(y-Oy)  / Sy                         // 0..1
    U =(u-Ouv) / Suv                        //-1..1
    V =(v-Ouv) / Suv

    R = Y                  + V*(1-Kr)       // 0..1
    G = Y - U*(1-Kb)*Kb/Kg - V*(1-Kr)*Kr/Kg
    B = Y + U*(1-Kb)

    r = R*Srgb                              // 0..255   0..65535
    g = G*Srgb
    b = B*Srgb
  */


  const double mulfac = double(1 << int_arith_shift); // integer aritmetic precision scale

  const double Kg = 1. - Kr - Kb;

  if (bits_per_pixel <= 16) {
    const auto Srgb = (full_scale_d ? double((1 << bits_per_pixel) - 1) : double(219 << (bits_per_pixel - 8)));
    const double Suv_d = (cmax - cmin) / 2.0;

    matrix.y_b = (int)(Srgb * 1.000 * mulfac / Sy + 0.5); //Y
    matrix.u_b = (int)(Srgb * (1 - Kb) * mulfac / Suv_d + 0.5); //U
    matrix.v_b = (int)(Srgb * 0.000 * mulfac / Suv_d + 0.5); //V

    matrix.y_g = (int)(Srgb * 1.000 * mulfac / Sy + 0.5);
    matrix.u_g = (int)(Srgb * (Kb - 1) * Kb / Kg * mulfac / Suv_d + 0.5);
    matrix.v_g = (int)(Srgb * (Kr - 1) * Kr / Kg * mulfac / Suv_d + 0.5);

    matrix.y_r = (int)(Srgb * 1.000 * mulfac / Sy + 0.5);
    matrix.u_r = (int)(Srgb * 0.000 * mulfac / Suv_d + 0.5);
    matrix.v_r = (int)(Srgb * (1 - Kr) * mulfac / Suv_d + 0.5);

    matrix.offset_y = -Oy;
    matrix.offset_rgb = Orgb;
  }

  double Srgb_f = (bits_per_pixel == 32) ? (full_scale_d ? 1.0 : 219.0 / 255.0)
    : (full_scale_d ? double((1 << bits_per_pixel) - 1) : double(219 << (bits_per_pixel - 8)));
  matrix.y_b_f = (float)(Srgb_f * 1.000 / Sy_f); //Y
  matrix.u_b_f = (float)(Srgb_f * (1 - Kb) / Suv_f); //U
  matrix.v_b_f = (float)(Srgb_f * 0.000 / Suv_f); //V
  matrix.y_g_f = (float)(Srgb_f * 1.000 / Sy_f);
  matrix.u_g_f = (float)(Srgb_f * (Kb - 1) * Kb / Kg / Suv_f);
  matrix.v_g_f = (float)(Srgb_f * (Kr - 1) * Kr / Kg / Suv_f);
  matrix.y_r_f = (float)(Srgb_f * 1.000 / Sy_f);
  matrix.u_r_f = (float)(Srgb_f * 0.000 / Suv_f);
  matrix.v_r_f = (float)(Srgb_f * (1 - Kr) / Suv_f);
  matrix.offset_y_f = -Oy_f;
  matrix.offset_rgb_f = Orgb_f;
}

bool GetKrKb(int matrix, double& kr, double& kb)
{
  switch (matrix) {
  case Matrix_e::AVS_MATRIX_BT470_BG:
  case Matrix_e::AVS_MATRIX_ST170_M: kr = 0.299; kb = 0.114; break;
  case Matrix_e::AVS_MATRIX_BT709: kr = 0.2126; kb = 0.0722; break;
  case Matrix_e::AVS_MATRIX_AVERAGE: kr = 1.0 / 3; kb = 1.0 / 3; break;
  case Matrix_e::AVS_MATRIX_BT2020_CL:
  case Matrix_e::AVS_MATRIX_BT2020_NCL: kr = 0.2627; kb = 0.0593; break;
  case Matrix_e::AVS_MATRIX_BT470_M: kr = 0.3; kb = 0.11; break;
  case Matrix_e::AVS_MATRIX_ST240_M: kr = 0.212; kb = 0.087; break;
  case Matrix_e::AVS_MATRIX_RGB: kr = 0; kb = 0; break;
  default: return false;
  }
  return true;
}

bool do_BuildMatrix_Rgb2Yuv(int _Matrix, int _ColorRange, int _ColorRange_Out, int int_arith_shift, int bits_per_pixel, ConversionMatrix& matrix)
{
  if (_ColorRange != ColorRange_e::AVS_RANGE_FULL && _ColorRange != ColorRange_e::AVS_RANGE_LIMITED)
    return false;
  if (_ColorRange_Out != ColorRange_e::AVS_RANGE_FULL && _ColorRange_Out != ColorRange_e::AVS_RANGE_LIMITED)
    return false;

  const bool is_full_s = _ColorRange == ColorRange_e::AVS_RANGE_FULL;
  const bool is_full_d = _ColorRange_Out == ColorRange_e::AVS_RANGE_FULL;

  double kr, kb;
  if (!GetKrKb(_Matrix, kr, kb))
    return false;
  BuildMatrix_Rgb2Yuv_core(kr, kb, int_arith_shift, is_full_s, is_full_d, bits_per_pixel, matrix);
  return true;
}

bool do_BuildMatrix_Yuv2Rgb(int _Matrix, int _ColorRange, int _ColorRange_Out, int int_arith_shift, int bits_per_pixel, ConversionMatrix& matrix)
{
  if (_ColorRange != ColorRange_e::AVS_RANGE_FULL && _ColorRange != ColorRange_e::AVS_RANGE_LIMITED)
    return false;
  if (_ColorRange_Out != ColorRange_e::AVS_RANGE_FULL && _ColorRange_Out != ColorRange_e::AVS_RANGE_LIMITED)
    return false;

  const bool is_full_s = _ColorRange == ColorRange_e::AVS_RANGE_FULL;
  const bool is_full_d = _ColorRange_Out == ColorRange_e::AVS_RANGE_FULL;

  double kr, kb;
  if (!GetKrKb(_Matrix, kr, kb))
    return false;
  BuildMatrix_Yuv2Rgb_core(kr, kb, int_arith_shift, is_full_s, is_full_d, bits_per_pixel, matrix);
  return true;
}
