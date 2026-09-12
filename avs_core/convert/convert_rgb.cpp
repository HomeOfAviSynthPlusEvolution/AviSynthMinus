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


#include "video_convert/layout.h"
#include "avs_simd/target_policy.h"
#include "convert_rgb.h"
namespace {
const vc_layout_functions* LayoutForEnvironment(IScriptEnvironment* env) {
  const auto* layout = vc_get_layout_functions(avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_layout_supported_targets()));
  if (!layout)
    env->ThrowError("RGB layout: cannot select kernel");
  return layout;
}
PVideoFrame RepackFrame(PClip child, const VideoInfo& vi, const vc_layout_functions* layout,
                       int sc, int dc, int n, IScriptEnvironment* env) {
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);
  const int status = layout->repack_bgr({src->GetReadPtr(), src->GetPitch()},
    {dst->GetWritePtr(), dst->GetPitch()}, vi.ComponentSize() == 1 ? VC_U8 : VC_U16,
    sc, dc, vi.ComponentSize() == 1 ? 255 : 65535, {vi.width, vi.height, 0, vi.height});
  if (status != VC_OK)
    env->ThrowError("RGB layout: channel conversion failed (%d)", status);
  return dst;
}
} // namespace

RGBtoRGBA::RGBtoRGBA(PClip src, IScriptEnvironment* env)
  : GenericVideoFilter(src), layout(LayoutForEnvironment(env)) {
  vi.pixel_type = vi.ComponentSize() == 1 ? VideoInfo::CS_BGR32 : VideoInfo::CS_BGR64;
}
PVideoFrame __stdcall RGBtoRGBA::GetFrame(int n, IScriptEnvironment* env) {
  return RepackFrame(child, vi, layout, 3, 4, n, env);
}

RGBAtoRGB::RGBAtoRGB(PClip src, IScriptEnvironment* env)
  : GenericVideoFilter(src), layout(LayoutForEnvironment(env)) {
  vi.pixel_type = vi.ComponentSize() == 1 ? VideoInfo::CS_BGR24 : VideoInfo::CS_BGR48;
}
PVideoFrame __stdcall RGBAtoRGB::GetFrame(int n, IScriptEnvironment* env) {
  return RepackFrame(child, vi, layout, 4, 3, n, env);
}

PackedRGBtoPlanarRGB::PackedRGBtoPlanarRGB(PClip src, bool _sourceHasAlpha, bool _targetHasAlpha, IScriptEnvironment* env)
  : GenericVideoFilter(src), layout(LayoutForEnvironment(env)), sourceHasAlpha(_sourceHasAlpha), targetHasAlpha(_targetHasAlpha) {
  vi.pixel_type = vi.ComponentSize() == 1 ?
    (targetHasAlpha ? VideoInfo::CS_RGBAP : VideoInfo::CS_RGBP) :
    (targetHasAlpha ? VideoInfo::CS_RGBAP16 : VideoInfo::CS_RGBP16);
}
PVideoFrame __stdcall PackedRGBtoPlanarRGB::GetFrame(int n, IScriptEnvironment* env) {
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);
  vc_rgb_planes output{{dst->GetWritePtr(PLANAR_R), dst->GetPitch(PLANAR_R)},
    {dst->GetWritePtr(PLANAR_G), dst->GetPitch(PLANAR_G)},
    {dst->GetWritePtr(PLANAR_B), dst->GetPitch(PLANAR_B)}, {nullptr, 0}};
  if (targetHasAlpha)
    output.a = {dst->GetWritePtr(PLANAR_A), dst->GetPitch(PLANAR_A)};
  const int pitch = src->GetPitch();
  const vc_const_plane source{src->GetReadPtr() + ptrdiff_t(pitch) * (vi.height - 1), -pitch};
  const int status = layout->unpack_bgr(source, output, vi.ComponentSize() == 1 ? VC_U8 : VC_U16,
    sourceHasAlpha ? 4 : 3, vi.ComponentSize() == 1 ? 255 : 65535, {vi.width, vi.height, 0, vi.height});
  if (status != VC_OK)
    env->ThrowError("PackedRGBtoPlanarRGB: layout conversion failed (%d)", status);
  return dst;
}

PlanarRGBtoPackedRGB::PlanarRGBtoPackedRGB(PClip src, bool _targetHasAlpha, IScriptEnvironment* env)
  : GenericVideoFilter(src), layout(LayoutForEnvironment(env)), targetHasAlpha(_targetHasAlpha)
{
  vi.pixel_type = src->GetVideoInfo().ComponentSize() == 1 ?
    (targetHasAlpha ? VideoInfo::CS_BGR32 : VideoInfo::CS_BGR24) : // PlanarRGB(A)->RGB24/32
    (targetHasAlpha ? VideoInfo::CS_BGR64 : VideoInfo::CS_BGR48);  // PlanarRGB(A)->RGB48/64
}

PVideoFrame __stdcall PlanarRGBtoPackedRGB::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame src = child->GetFrame(n, env);
  PVideoFrame dst = env->NewVideoFrameP(vi, &src);
  vc_const_rgb_planes source{
    {src->GetReadPtr(PLANAR_R), src->GetPitch(PLANAR_R)},
    {src->GetReadPtr(PLANAR_G), src->GetPitch(PLANAR_G)},
    {src->GetReadPtr(PLANAR_B), src->GetPitch(PLANAR_B)},
    {nullptr, 0}};
  if (child->GetVideoInfo().IsPlanarRGBA())
    source.a = {src->GetReadPtr(PLANAR_A), src->GetPitch(PLANAR_A)};
  const int pitch = dst->GetPitch();
  const vc_plane destination{dst->GetWritePtr() + ptrdiff_t(pitch) * (vi.height - 1), -pitch};
  const int status = layout->pack_bgr(source, destination, vi.ComponentSize() == 1 ? VC_U8 : VC_U16,
    targetHasAlpha ? 4 : 3, vi.ComponentSize() == 1 ? 255 : 65535, {vi.width, vi.height, 0, vi.height});
  if (status != VC_OK)
    env->ThrowError("PlanarRGBtoPackedRGB: layout conversion failed (%d)", status);
  return dst;
}
