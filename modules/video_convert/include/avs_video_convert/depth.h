// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#pragma once
#include <avisynth.h>
#include <video_convert/depth.h>
#include <array>
#include <memory>
namespace avs_video_convert {
// Both input ranges are bound once; execution chooses locally from frame metadata.
class DepthPlans {
public:
  DepthPlans(int source_bits, int destination_bits, bool destination_full, IScriptEnvironment* env);
  void Convert(const PVideoFrame& source, PVideoFrame& destination, const VideoInfo& output_info, bool source_full,
               IScriptEnvironment* env) const;

  void ConvertAlpha(const PVideoFrame& source, PVideoFrame& destination, IScriptEnvironment* env) const;

private:
  struct Destroy {
    void operator()(vc_depth_plan* plan) const { vc_depth_destroy(plan); }
  };
  using Plan = std::unique_ptr<vc_depth_plan, Destroy>;
  std::array<Plan, 2> luma_, chroma_;
  Plan alpha_;
  int source_bytes_, destination_bytes_;
  const vc_layout_functions* layout_;
};
} // namespace avs_video_convert
