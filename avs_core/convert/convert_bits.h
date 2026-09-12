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

#ifndef __Convert_bits_H__
#define __Convert_bits_H__

#include <avisynth.h>
#include <stdint.h>
#include "convert.h"
#include "avs_video_convert/depth.h"
#include "avs_video_convert/dither.h"


class ConvertBits : public GenericVideoFilter
{
public:
  ConvertBits(PClip _child, const int _dither_mode, const int _target_bitdepth, bool _truerange, int _ColorRange_src, int _ColorRange_dest, int _dither_bitdepth, IScriptEnvironment* env, bool _source_range_from_frame = false);
  PVideoFrame __stdcall GetFrame(int n,IScriptEnvironment* env) override;

  int __stdcall SetCacheHints(int cachehints, int frame_range) override {
    AVS_UNUSED(frame_range);
    return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
  }

  static AVSValue __cdecl Create(AVSValue args, void*, IScriptEnvironment* env);
private:
  std::unique_ptr<avs_video_convert::DepthPlans> depth_plans;
  std::unique_ptr<avs_video_convert::DitherPlans> dither_plans;
  int target_bitdepth;
  int dither_mode;
  int dither_bitdepth;
  bool fulls; // source is full range (defaults: rgb=true, yuv=false (bit shift))
  bool fulld; // destination is full range (defaults: rgb=true, yuv=false (bit shift))
  bool truerange; // if 16->10 range reducing or e.g. 14->16 bit range expansion needed
  bool source_range_from_frame;
  bool default_source_full;
  int pixelsize;
  int bits_per_pixel;
  bool format_change_only;
};

/**********************************
******  Bitdepth conversions  *****
**********************************/

#endif // __Convert_bits_H__
