#pragma once
#include <avisynth.h>
#include <video_convert/matrix.h>
#include <memory>
namespace avs_video_convert {
// AVS owns script/color metadata. This adapter owns an immutable numeric plan.
class MatrixPlan {
public:
  MatrixPlan(double kr, double kb, int depth, int precision, bool source_full, bool destination_full, bool rgb_to_yuv,
             IScriptEnvironment* env, bool luma_only = false);
  void Convert(const PVideoFrame& source, PVideoFrame& destination, int width, int height,
               IScriptEnvironment* env) const;

  void ConvertPacked(const PVideoFrame& source, PVideoFrame& destination, int width, int height, int components,
                     bool alpha, IScriptEnvironment* env) const;

  void ApplyGreyscale(PVideoFrame& frame, const VideoInfo& vi, IScriptEnvironment* env) const;

private:
  bool rgb_to_yuv_;
  bool luma_only_;
  int bytes_;
  const vc_layout_functions* layout_;
  std::unique_ptr<vc_matrix_plan, decltype(&vc_matrix_destroy)> plan_{nullptr, vc_matrix_destroy};
};
} // namespace avs_video_convert
