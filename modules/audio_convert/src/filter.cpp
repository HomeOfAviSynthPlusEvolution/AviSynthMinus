// Avisynth v2.5.  Copyright 2002 Ben Rudiak-Gould et al.
// http://avisynth.nl
//
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

// ConvertAudio classes
// Copyright (c) Klaus Post 2001 - 2004
// Copyright (c) Ian Brabham 2005
// Copyright (c) 2020 Xinyue Lu
// Copyright (c) 2021 pinterf

#include <avisynth.h>
#include <avs/alignment.h>
#include "audio_convert/factory.h"
#include "audio_convert/audio_convert.h"
#include "avs_simd/target_policy.h"

#if defined(AVS_BSD) || defined(AVS_MACOS)
  #include <stdlib.h>
#else
  #include <malloc.h>
#endif
#include <limits>

namespace {

int ToLibraryFormat(int format) {
  switch (format) {
    case SAMPLE_INT8: return AC_U8;
    case SAMPLE_INT16: return AC_S16;
    case SAMPLE_INT24: return AC_S24;
    case SAMPLE_INT32: return AC_S32;
    case SAMPLE_FLOAT: return AC_F32;
    default: return 0;
  }
}

class ConvertAudio : public GenericVideoFilter {
public:
  ConvertAudio(PClip _clip, int prefered_format);
  virtual ~ConvertAudio();
  void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env) override;
  int __stdcall SetCacheHints(int cachehints, int frame_range) override;

private:
  int src_format;
  int dst_format;
  int src_bps;
  int tempbuffer_size {0};
  char *tempbuffer {nullptr};

  ac_convert_fn convert {nullptr};
};

int __stdcall ConvertAudio::SetCacheHints(int cachehints, int frame_range) {
  // We do pass cache requests upwards, to the next filter.
  return child->SetCacheHints(cachehints, frame_range);
}

ConvertAudio::ConvertAudio(PClip _clip, int _sample_type)
    : GenericVideoFilter(_clip) {
  dst_format = _sample_type;
  src_format = vi.SampleType();
  src_bps = vi.BytesPerChannelSample(); // Store old size
  vi.sample_type = dst_format;
  tempbuffer_size = 0;
}

ConvertAudio::~ConvertAudio() {
  if (tempbuffer_size) {
    avs_free(tempbuffer);
    tempbuffer_size = 0;
  }
}

void __stdcall ConvertAudio::GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) {
  if (src_format == dst_format) {
    // Shouldn't happen, but just in case
    child->GetAudio(buf, start, count, env);
    return;
  }

  if (count <= 0) return;
  const int channels = vi.AudioChannels();
  // The C API counts individual channel values in an int, not audio frames.
  if (channels <= 0 || count > std::numeric_limits<int>::max() / channels)
    env->ThrowError("ConvertAudio: audio count is too large.");

  if (tempbuffer_size < count) {
    const size_t sample_count = static_cast<size_t>(count);
    const size_t bytes_per_sample = static_cast<size_t>(src_bps) * static_cast<size_t>(channels);

    if (bytes_per_sample == 0 || sample_count > std::numeric_limits<size_t>::max() / bytes_per_sample)
      env->ThrowError("ConvertAudio: audio buffer size overflow.");

    const size_t buffer_size = sample_count * bytes_per_sample;
    char* new_tempbuffer = static_cast<char*>(avs_malloc(buffer_size, 16));

    if (!new_tempbuffer)
      env->ThrowError("ConvertAudio: insufficient memory.");

    avs_free(tempbuffer);
    tempbuffer = new_tempbuffer;
    tempbuffer_size = static_cast<int>(count);
  }

  child->GetAudio(tempbuffer, start, count, env);

  if (convert == nullptr) {
    const int64_t target = avs_simd::ChooseTarget(
        env->GetCPUFlagsEx(), ac_compiled_targets());
    convert = ac_get_converter(ToLibraryFormat(src_format), ToLibraryFormat(dst_format), target);
    if (!convert)
      env->ThrowError("ConvertAudio: unsupported audio conversion.");
  }

  convert(tempbuffer, buf, static_cast<int>(count * channels));
}

}  // namespace

namespace avs_audio_convert {

// There are two type parameters. Acceptable sample types and a prefered sample type.
// If the current clip is already one of the defined types in sampletype, this will be returned.
// If not, the current clip will be converted to the prefered type.
PClip EnsureAudioFormat(PClip clip, int accepted_formats, int preferred_format) {
  if ((!clip->GetVideoInfo().HasAudio()) || (clip->GetVideoInfo().SampleType() & (accepted_formats | preferred_format))) {
    // Sample type is already ok!
    return clip;
  } else {
    return new ConvertAudio(clip, preferred_format);
  }
}

}  // namespace avs_audio_convert
