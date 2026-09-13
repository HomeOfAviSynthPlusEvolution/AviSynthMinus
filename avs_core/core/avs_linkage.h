// Licensed under GPL version 2 or later with the inherited AviSynth linking exception.
#pragma once

struct AVS_Linkage;

// Core callers must not read the public, interposable AVS_linkage variable.
#if defined(__GNUC__) && !defined(_WIN32)
__attribute__((visibility("hidden")))
#endif
const AVS_Linkage* GetCoreAVSLinkage() noexcept;
