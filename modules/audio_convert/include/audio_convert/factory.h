#pragma once

#include <avisynth.h>

namespace avs_audio_convert {

PClip EnsureAudioFormat(PClip clip, int accepted_formats, int preferred_format);

}  // namespace avs_audio_convert
