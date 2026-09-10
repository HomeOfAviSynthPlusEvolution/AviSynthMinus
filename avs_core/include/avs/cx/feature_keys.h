// Official AviSynth CX feature-key registry. Keys are append-only.
#ifndef AVS_CX_FEATURE_KEYS_H
#define AVS_CX_FEATURE_KEYS_H

#include <stdint.h>

// ASCII-derived values make diagnostics readable while remaining explicit.
// Never reuse a published key. A feature's v1 table is frozen; incompatible
// growth receives either a new key or a new exact version, and hosts continue
// serving the old table.
#define AVS_CX_FEATURE_REGISTRY UINT64_C(0x4156534358524547)    // AVSCXREG
#define AVS_CX_FEATURE_ENVIRONMENT UINT64_C(0x4156534358454E56) // AVSCXENV
#define AVS_CX_FEATURE_CLIP UINT64_C(0x4156534358434C50)        // AVSCXCLP
#define AVS_CX_FEATURE_FRAME UINT64_C(0x415653435846524D)       // AVSCXFRM

#define AVS_CX_FEATURE_MESSAGE UINT64_C(0x41565343584D5347) // AVSCXMSG

#endif // AVS_CX_FEATURE_KEYS_H
