#include "audio_convert_script.h"
#include "audio_convert/factory.h"

namespace avs_audio_convert {

AVSValue __cdecl CreateAny(AVSValue args, void*, IScriptEnvironment*) {
  return EnsureAudioFormat(args[0].AsClip(), args[1].AsInt(), args[2].AsInt());
}

AVSValue __cdecl Create8(AVSValue args, void*, IScriptEnvironment*) {
  return EnsureAudioFormat(args[0].AsClip(), SAMPLE_INT8, SAMPLE_INT8);
}

AVSValue __cdecl Create16(AVSValue args, void*, IScriptEnvironment*) {
  return EnsureAudioFormat(args[0].AsClip(), SAMPLE_INT16, SAMPLE_INT16);
}

AVSValue __cdecl Create24(AVSValue args, void*, IScriptEnvironment*) {
  return EnsureAudioFormat(args[0].AsClip(), SAMPLE_INT24, SAMPLE_INT24);
}

AVSValue __cdecl Create32(AVSValue args, void*, IScriptEnvironment*) {
  return EnsureAudioFormat(args[0].AsClip(), SAMPLE_INT32, SAMPLE_INT32);
}

AVSValue __cdecl CreateFloat(AVSValue args, void*, IScriptEnvironment*) {
  return EnsureAudioFormat(args[0].AsClip(), SAMPLE_FLOAT, SAMPLE_FLOAT);
}

}  // namespace avs_audio_convert
