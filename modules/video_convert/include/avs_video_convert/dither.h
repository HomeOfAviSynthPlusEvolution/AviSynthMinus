// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#ifndef AVS_VIDEO_CONVERT_DITHER_H
#define AVS_VIDEO_CONVERT_DITHER_H
#include <avisynth.h>
#include <video_convert/dither.h>
#include <array>
#include <memory>
namespace avs_video_convert {
class DitherPlans {
  struct Destroy {
    void operator()(vc_ordered_plan* p) const { vc_ordered_destroy(p); }
  };
  std::array<std::unique_ptr<vc_ordered_plan, Destroy>, 4> ordered_;
  std::array<vc_floyd_config, 4> configs_;
  int source_bytes_;
  bool ordered_mode_;

public:
  DitherPlans(int source_bits, int destination_bits, int quantization_bits, int mode, bool destination_full,
              IScriptEnvironment* env);
  void ConvertPlane(const PVideoFrame& source, PVideoFrame& destination, int plane, bool source_full,
                    IScriptEnvironment* env) const;
};
} // namespace avs_video_convert
#endif
