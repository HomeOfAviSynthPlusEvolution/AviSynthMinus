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
