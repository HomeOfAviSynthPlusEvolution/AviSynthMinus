#pragma once
#include <avisynth.h>
#include <video_convert/resample.h>
namespace avs_video_convert {
PClip CreateResizeAxis(PClip source, int axis, double crop_start, double crop_size, int target_size,
                       const vc_filter_spec& filter, double luma_center, double chroma_center, IScriptEnvironment* env);
}
