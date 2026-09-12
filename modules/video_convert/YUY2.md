# YUY2 conversion semantics

The migration follows the modern upstream composition model approved on
2026-09-06. YUY2 is packed 8-bit YUV422, not a separate matrix or resampling
algorithm family.

- YUY2/YV16 layout adapters preserve samples and frame properties without
  resampling. They bind the independent C/Highway layout table once using AVS
  CPU policy (`SetMaxCPU("none")` selects C).
- YUY2 to RGB first unpacks to YV16, then uses the existing planar chroma and
  matrix conversion pipeline. RGB and YUV to YUY2 first produce planar YUV422,
  convert to eight bits if necessary, strip alpha, and pack.
- ConvertToYV12 uses the general planar converter, including for YUY2 sources.
  The old fixed progressive/interlaced approximation is removed.
- ConvertBackToYUY2 forwards to ordinary ConvertToYUY2 with interlaced=false and
  its supplied matrix argument. It no longer selects only left-pixel chroma.
- Current chroma properties and explicit placement parameters follow the shared
  converter's precedence and documented defaults. No conversion-history metadata
  or lossless RGB roundtrip promise is introduced.

For positional script compatibility, ConvertToYUY2 keeps its existing parameter
order and appends ChromaOutPlacement after param3. Named calls are recommended
when selecting output placement. This host does not add upstream's newer bits
or quality parameters in this migration; the YUY2 output is always eight-bit.

Old fused rounding and chroma samples are intentionally not the acceptance golden.
Public tests check layout bytes, placement/property equivalence, field-aware
composition, RGB routes, neutral range mapping, and the compatibility alias.
Performance tuning and optional row fusion are separate from this semantic change.

Upstream reference:
https://github.com/AviSynth/AviSynthPlus/commit/390ef1d864d2634c63ad3e43c069bee651d28f29
