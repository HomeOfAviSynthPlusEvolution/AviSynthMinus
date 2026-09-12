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
// Avisynth solely through the interfaces defined in avisynth.h, regardless of
// the license terms of these independent modules, and to copy and distribute
// the resulting combined work under terms of your choice, provided that every
// copy of the combined work is accompanied by a complete copy of the source
// code of Avisynth (the version of Avisynth used to produce the combined work),
// being distributed under the terms of the GNU General Public License plus this
// exception.  An independent module is a module which is not derived from or
// based on Avisynth, such as 3rd-party filters, import and export plugins, or
// graphical user interfaces.

#include "avs_video_convert/resize.h"
#include "avs_simd/target_policy.h"
#include <array>
#include <limits>
#include <memory>
#include <video_convert/layout.h>

namespace avs_video_convert {
namespace {
using Plan = std::unique_ptr<vc_resample_plan, decltype(&vc_resample_destroy)>;
void Check(int status, IScriptEnvironment* env) {
  if (status != VC_OK)
    env->ThrowError("Resize: VideoConvert operation failed (status %d).", status);
}
class ResizeAxis final : public GenericVideoFilter {
  std::array<Plan, 2> plans{{{nullptr, vc_resample_destroy}, {nullptr, vc_resample_destroy}}};
  std::array<int, 4> plan_indices{};
  std::array<int, 4> planes{{PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A}};
  std::array<int, 4> source_heights{};
  int count = 1, source_width, bytes, components;
  bool packed_horizontal, packed_yuy2;
  const vc_layout_functions* layout = nullptr;

public:
  ResizeAxis(PClip source, int axis, double start, double size, int target, const vc_filter_spec& filter,
             double luma_center, double chroma_center, IScriptEnvironment* env)
      : GenericVideoFilter(source), source_width(vi.width), bytes(vi.ComponentSize()), components(vi.NumComponents()),
        packed_horizontal(!vi.IsPlanar() && axis == VC_HORIZONTAL), packed_yuy2(vi.IsYUY2() && axis == VC_HORIZONTAL) {
    const bool horizontal = axis == VC_HORIZONTAL;
    if (target <= 0)
      env->ThrowError("Resize: Target size must be greater than 0.");
    const bool rgb = vi.IsPlanarRGB() || vi.IsPlanarRGBA();
    const bool chroma = packed_yuy2 || (vi.IsPlanar() && !vi.IsY() && !rgb);
    if (chroma) {
      const int shift = packed_yuy2  ? 1
                        : horizontal ? vi.GetPlaneWidthSubsampling(PLANAR_U)
                                     : vi.GetPlaneHeightSubsampling(PLANAR_U);
      if (target & ((1 << shift) - 1))
        env->ThrowError("Resize: Planar destination size must be a multiple of %d.", 1 << shift);
    }
    if (!horizontal && vi.IsRGB() && !rgb)
      start = vi.height - start - size;
    if (rgb)
      planes = {{PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A}};
    count = vi.IsPlanar() || packed_yuy2 ? components : 1;
    const auto target_bit = avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_resample_supported_targets());
    if (packed_horizontal) {
      if (!vi.IsRGB() && !packed_yuy2)
        env->ThrowError("Resize: Horizontal packed input must be RGB or YUY2.");
      layout = vc_get_layout_functions(avs_simd::ChooseTarget(env->GetCPUFlagsEx(), vc_layout_supported_targets()));
    }
    for (int i = 0; i < count; ++i) {
      const bool uv = chroma && (i == 1 || i == 2);
      const int sx = uv ? (packed_yuy2 ? 1 : vi.GetPlaneWidthSubsampling(PLANAR_U)) : 0;
      const int sy = uv && !packed_yuy2 ? vi.GetPlaneHeightSubsampling(PLANAR_U) : 0;
      const int shift = horizontal ? sx : sy;
      int width = vi.width >> sx, height = vi.height >> sy;
      if (packed_horizontal)
        height = 1;
      else if (!vi.IsPlanar())
        width = vi.BytesFromPixels(vi.width) / bytes;
      const double center = uv ? chroma_center : luma_center;
      const vc_resample_config config{
          axis,   width,  height, target >> shift, vi.BitsPerComponent(), start / (1 << shift), size / (1 << shift),
          center, center, filter};
      source_heights[i] = height;
      plan_indices[i] = uv ? 1 : 0;
      if (plans[plan_indices[i]])
        continue;
      vc_resample_plan* raw = nullptr;
      Check(vc_resample_create_for_target(&config, target_bit, &raw), env);
      plans[plan_indices[i]].reset(raw);
    }
    if (horizontal)
      vi.width = target;
    else
      vi.height = target;
  }
  int __stdcall SetCacheHints(int hint, int) override { return hint == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0; }
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override {
    const PVideoFrame source = child->GetFrame(n, env);
    PVideoFrame destination = env->NewVideoFrameP(vi, &source);
    if (!packed_horizontal) {
      for (int i = 0; i < count; ++i) {
        const int plane = planes[i];
        Check(vc_resample_execute(plans[plan_indices[i]].get(),
                                  {{source->GetReadPtr(plane), source->GetPitch(plane)}, {0, source_heights[i]}},
                                  {{destination->GetWritePtr(plane), destination->GetPitch(plane)},
                                   {0, destination->GetHeight(plane)}}),
              env);
      }
      return destination;
    }
    // Packed horizontal execution uses bounded row storage, retaining physical
    // row order, chroma phase and independent channels (including RGB alpha).
    if (size_t(source_width) > (size_t(PTRDIFF_MAX) - 63) / bytes ||
        size_t(vi.width) > (size_t(PTRDIFF_MAX) - 63) / bytes)
      env->ThrowError("Resize: Row buffer is too large.");
    const size_t input_pitch = (size_t(source_width) * bytes + 63) & ~size_t(63);
    const size_t output_pitch = (size_t(vi.width) * bytes + 63) & ~size_t(63);
    if (output_pitch > size_t(PTRDIFF_MAX) / 4 || input_pitch > size_t(PTRDIFF_MAX) / 4 - output_pitch)
      env->ThrowError("Resize: Row buffer is too large.");
    auto release = [env](void* p) {
      env->Free(p);
    };
    std::unique_ptr<void, decltype(release)> storage(
        env->Allocate((input_pitch + output_pitch) * 4, 64, AVS_POOLED_ALLOC), release);
    if (!storage)
      env->ThrowError("Resize: Could not reserve row storage.");
    auto* input = static_cast<unsigned char*>(storage.get());
    auto* output = input + input_pitch * 4;
    const std::array<vc_plane, 4> input_planes{{{input, ptrdiff_t(input_pitch)},
                                                {input + input_pitch, ptrdiff_t(input_pitch)},
                                                {input + input_pitch * 2, ptrdiff_t(input_pitch)},
                                                {input + input_pitch * 3, ptrdiff_t(input_pitch)}}};
    const std::array<vc_const_plane, 4> output_planes{{{output, ptrdiff_t(output_pitch)},
                                                       {output + output_pitch, ptrdiff_t(output_pitch)},
                                                       {output + output_pitch * 2, ptrdiff_t(output_pitch)},
                                                       {output + output_pitch * 3, ptrdiff_t(output_pitch)}}};
    const int storage_type = bytes == 1 ? VC_U8 : VC_U16;
    for (int y = 0; y < vi.height; ++y) {
      const vc_const_plane src{source->GetReadPtr() + ptrdiff_t(y) * source->GetPitch(), source->GetPitch()};
      const vc_plane dst{destination->GetWritePtr() + ptrdiff_t(y) * destination->GetPitch(), destination->GetPitch()};
      const vc_rows input_rows{source_width, 1, 0, 1}, output_rows{vi.width, 1, 0, 1};
      if (packed_yuy2)
        Check(layout->unpack_yuy2(src, {input_planes[0], input_planes[1], input_planes[2]}, input_rows), env);
      else
        Check(layout->unpack_bgr(src, {input_planes[0], input_planes[1], input_planes[2], input_planes[3]},
                                 storage_type, components, 0, input_rows),
              env);
      for (int i = 0; i < components; ++i)
        Check(vc_resample_execute(plans[plan_indices[i]].get(),
                                  {{input + input_pitch * i, ptrdiff_t(input_pitch)}, {0, 1}},
                                  {{output + output_pitch * i, ptrdiff_t(output_pitch)}, {0, 1}}),
              env);
      if (packed_yuy2)
        Check(layout->pack_yuy2({output_planes[0], output_planes[1], output_planes[2]}, dst, output_rows), env);
      else
        Check(layout->pack_bgr({output_planes[0], output_planes[1], output_planes[2], output_planes[3]}, dst,
                               storage_type, components, 0, output_rows),
              env);
    }
    return destination;
  }
};
} // namespace
PClip CreateResizeAxis(PClip source, int axis, double crop_start, double crop_size, int target_size,
                       const vc_filter_spec& filter, double luma_center, double chroma_center,
                       IScriptEnvironment* env) {
  return new ResizeAxis(source, axis, crop_start, crop_size, target_size, filter, luma_center, chroma_center, env);
}
} // namespace avs_video_convert
