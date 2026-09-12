#pragma once

#include <avisynth.h>

namespace avs_audio_convert {

AVSValue __cdecl CreateAny(AVSValue args, void*, IScriptEnvironment*);
AVSValue __cdecl Create8(AVSValue args, void*, IScriptEnvironment*);
AVSValue __cdecl Create16(AVSValue args, void*, IScriptEnvironment*);
AVSValue __cdecl Create24(AVSValue args, void*, IScriptEnvironment*);
AVSValue __cdecl Create32(AVSValue args, void*, IScriptEnvironment*);
AVSValue __cdecl CreateFloat(AVSValue args, void*, IScriptEnvironment*);

}  // namespace avs_audio_convert
