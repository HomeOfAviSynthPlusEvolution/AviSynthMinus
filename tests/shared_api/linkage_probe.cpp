// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#include <avisynth.h>
#include <stdexcept>

extern "C" __declspec(dllexport) const char* __stdcall
AvisynthPluginInit3(IScriptEnvironment*, const AVS_Linkage* linkage) {
  if (!linkage || linkage->Size < sizeof(AVS_Linkage))
    throw std::runtime_error("Core passed an invalid linkage table to the plugin");
  return "Shared linkage probe";
}
