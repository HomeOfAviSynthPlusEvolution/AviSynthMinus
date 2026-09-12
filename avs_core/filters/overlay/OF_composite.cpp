// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#include "overlayfunctions.h"
#include "avs_composite/adapter.h"

namespace {
class CompositeOverlay final : public OverlayFunction {
  void Blend(ImageOverlayInternal* base, ImageOverlayInternal* source, ImageOverlayInternal* mask) {
    using namespace avs_composite;
    const auto* k = Kernels(env);
    const cp_format format = Format(bits_per_pixel);
    const int bytes = cp_sample_bytes(format);
    cp_plane dst[3];
    cp_const_plane src[3]{}, masks[3]{};
    const int count = greyscale ? 1 : 3;
    for (int c = 0; c < count; ++c) {
      dst[c] = {base->GetPtrByIndex(c), base->GetPitchByIndex(c), bytes};
      src[c] = {source->GetPtrByIndex(c), source->GetPitchByIndex(c), bytes};
      if (mask) masks[c] = {mask->GetPtrByIndex(c), mask->GetPitchByIndex(c), bytes};
    }
    const cp_rows rows{base->w(), base->h(), 0, base->h()};
    if (!rgb && !greyscale && of_mode != OF_Blend && of_mode != OF_Blend_Compat &&
        of_mode != OF_Luma && of_mode != OF_Chroma && of_mode != OF_Lighten && of_mode != OF_Darken) {
      const int operation = of_mode == OF_Add ? CP_YUV_ADD : of_mode == OF_Subtract ? CP_YUV_SUBTRACT
                          : of_mode == OF_Multiply ? CP_YUV_MULTIPLY : of_mode == OF_SoftLight ? CP_YUV_SOFT_LIGHT
                          : of_mode == OF_HardLight ? CP_YUV_HARD_LIGHT : of_mode == OF_Difference ? CP_YUV_DIFFERENCE
                          : CP_YUV_EXCLUSION;
      const cp_yuv_config config{format, operation, Opacity(opacity_f)};
      const cp_const_yuv m{masks[0], masks[1], masks[2]};
      // Multiply consumes source Y only; UV may be absent or subsampled.
      if (of_mode == OF_Multiply) src[1] = src[2] = src[0];
      Check(k->process_yuv(&config, {Read(dst[0]), Read(dst[1]), Read(dst[2])}, {src[0], src[1], src[2]},
                           mask ? &m : nullptr, {dst[0], dst[1], dst[2]}, rows), env);
      return;
    }
    cp_plane_config config{};
    config.format = format;
    config.opacity = Opacity(opacity_f);
    config.operation = of_mode == OF_Add ? CP_ADD : of_mode == OF_Subtract ? CP_SUBTRACT
                     : of_mode == OF_Lighten ? CP_SELECT_LIGHTER : of_mode == OF_Darken ? CP_SELECT_DARKER : CP_MIX;
    config.inclusive = 1;
    const bool guided = of_mode == OF_Lighten || of_mode == OF_Darken;
    const auto bg = Read(dst[0]);
    // Y is last: chroma decisions use the original base luminance.
    for (int i = 0; i < count; ++i) {
      const int c = count == 1 ? 0 : (i + 1) % 3;
      if ((of_mode == OF_Luma && c != 0) || (of_mode == OF_Chroma && c == 0)) continue;
      const int width = rows.width >> base->xSubSamplingShifts[c];
      const int height = rows.height >> base->ySubSamplingShifts[c];
      if (!width || !height) continue;
      const cp_rows part{width, height, 0, height};
      if (of_mode == OF_Blend_Compat && bits_per_pixel != 32)
        Check(k->blend_compat(format, Read(dst[c]), src[c], mask ? &masks[c] : nullptr, dst[c], part, opacity), env);
      else
        Check(k->process_plane(&config, Read(dst[c]), src[c], mask ? &masks[c] : nullptr,
                              guided ? &bg : nullptr, guided ? &src[0] : nullptr, dst[c], part), env);
    }
  }
 public:
  void DoBlendImage(ImageOverlayInternal* base, ImageOverlayInternal* source) override { Blend(base, source, nullptr); }
  void DoBlendImageMask(ImageOverlayInternal* base, ImageOverlayInternal* source, ImageOverlayInternal* mask) override {
    Blend(base, source, mask);
  }
};
} // namespace
OverlayFunction* CreateCompositeOverlay() { return new CompositeOverlay(); }
