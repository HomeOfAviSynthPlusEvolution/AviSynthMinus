// Derived from core/interface.cpp; same AviSynth license and linking exception.
bool VideoInfo::CX_HasVideo() const { return (width != 0); }

bool VideoInfo::CX_HasAudio() const { return (audio_samples_per_second != 0); }

bool VideoInfo::CX_IsRGB() const { return !!(pixel_type & CS_BGR); }

bool VideoInfo::CX_IsRGB24() const {
  return ((pixel_type & CS_BGR24) == CS_BGR24) &&
         ((pixel_type & CS_Sample_Bits_Mask) == CS_Sample_Bits_8);
}

bool VideoInfo::CX_IsRGB32() const {
  return ((pixel_type & CS_BGR32) == CS_BGR32) &&
         ((pixel_type & CS_Sample_Bits_Mask) == CS_Sample_Bits_8);
}

bool VideoInfo::CX_IsYUV() const { return !!(pixel_type & CS_YUV); }

bool VideoInfo::CX_IsYUY2() const { return (pixel_type & CS_YUY2) == CS_YUY2; }

bool VideoInfo::CX_IsYV24() const {
  return (pixel_type & CS_PLANAR_MASK) == (CS_YV24 & CS_PLANAR_FILTER);
}

bool VideoInfo::CX_IsYV16() const {
  return (pixel_type & CS_PLANAR_MASK) == (CS_YV16 & CS_PLANAR_FILTER);
}

bool VideoInfo::CX_IsYV12() const {
  return (pixel_type & CS_PLANAR_MASK) == (CS_YV12 & CS_PLANAR_FILTER);
}

bool VideoInfo::CX_IsY8() const {
  return (pixel_type & CS_PLANAR_MASK) == (CS_Y8 & CS_PLANAR_FILTER);
}

bool VideoInfo::CX_IsYV411() const {
  return (pixel_type & CS_PLANAR_MASK) == (CS_YV411 & CS_PLANAR_FILTER);
}

// bool VideoInfo::CX_IsYUV9()  const { return (pixel_type & CS_PLANAR_MASK) == (CS_YUV9  &
// CS_PLANAR_FILTER); }

bool VideoInfo::CX_IsColorSpace(int c_space) const {
  // This function will check if the colorspace (VideoInfo.pixel_type) is the same as given c_space
  // (or more general it checks for a Colorspace property (see avisynth.h)). So it's not only for
  // exact colorspace comparison but checking properties also.
  return CX_IsPlanar() ? ((pixel_type & CS_PLANAR_MASK) == (c_space & CS_PLANAR_FILTER)) :
                       // support exact individual flag checking
             c_space == CS_YUVA ? ((c_space & CS_YUVA) == CS_YUVA)
         : c_space == CS_BGR    ? ((c_space & CS_BGR) == CS_BGR)
         : c_space == CS_YUV    ? ((c_space & CS_YUV) == CS_YUV)
         : c_space == CS_INTERLEAVED
             ? ((c_space & CS_INTERLEAVED) == CS_INTERLEAVED)
             :
             // RGB got sample bits:
             // In order to work: RGB64.CX_IsColorSpace(RGB32) => false, or else we get true (RGB64
             // & RGB32 == RGB32) Simple ((pixel_type & c_space) == c_space) would not work, does
             // not take into account bit depth
             (((pixel_type & ~CS_Sample_Bits_Mask & c_space) == (c_space & ~CS_Sample_Bits_Mask)) &&
              ((pixel_type & CS_Sample_Bits_Mask) == (c_space & CS_Sample_Bits_Mask)));
}

bool VideoInfo::CX_Is(int property) const { return ((image_type & property) == property); }

bool VideoInfo::CX_IsPlanar() const { return !!(pixel_type & CS_PLANAR); }

bool VideoInfo::CX_IsFieldBased() const { return !!(image_type & IT_FIELDBASED); }

bool VideoInfo::CX_IsParityKnown() const {
  return ((image_type & IT_FIELDBASED) && (image_type & (IT_BFF | IT_TFF)));
}

bool VideoInfo::CX_IsBFF() const { return !!(image_type & IT_BFF); }

bool VideoInfo::CX_IsTFF() const { return !!(image_type & IT_TFF); }

int64_t VideoInfo::CX_AudioSamplesFromFrames(int frames) const {
  return (fps_numerator && CX_HasVideo())
             ? ((int64_t)(frames)*audio_samples_per_second * fps_denominator / fps_numerator)
             : 0;
}

int VideoInfo::CX_FramesFromAudioSamples(int64_t samples) const {
  return (fps_denominator && CX_HasAudio())
             ? (int)((samples * fps_numerator) /
                     ((int64_t)fps_denominator * audio_samples_per_second))
             : 0;
}

int64_t VideoInfo::CX_AudioSamplesFromBytes(int64_t bytes) const {
  return CX_HasAudio() ? bytes / CX_BytesPerAudioSample() : 0;
}

int64_t VideoInfo::CX_BytesFromAudioSamples(int64_t samples) const {
  return samples * CX_BytesPerAudioSample();
}

int VideoInfo::CX_AudioChannels() const { return CX_HasAudio() ? nchannels : 0; }

int VideoInfo::CX_SampleType() const { return sample_type; }

bool VideoInfo::CX_IsSampleType(int testtype) const { return !!(sample_type & testtype); }

int VideoInfo::CX_SamplesPerSecond() const { return audio_samples_per_second; }

int VideoInfo::CX_BytesPerAudioSample() const { return nchannels * CX_BytesPerChannelSample(); }

void VideoInfo::CX_SetFieldBased(bool isfieldbased) {
  if (isfieldbased)
    image_type |= IT_FIELDBASED;
  else
    image_type &= ~IT_FIELDBASED;
}

void VideoInfo::CX_Set(int property) { image_type |= property; }

void VideoInfo::CX_Clear(int property) { image_type &= ~property; }

int VideoInfo::CX_BytesPerChannelSample() const {
  switch (sample_type) {
  case SAMPLE_INT8:
    return sizeof(unsigned char);
  case SAMPLE_INT16:
    return sizeof(signed short);
  case SAMPLE_INT24:
    return 3;
  case SAMPLE_INT32:
    return sizeof(signed int);
  case SAMPLE_FLOAT:
    return sizeof(SFLOAT);
  default:
    _ASSERTE("Audio sample type not recognized!");
    return 0;
  }
}

bool VideoInfo::CX_IsVPlaneFirst() const {
  return (CX_NumComponents() > 1) && CX_IsPlanar() &&
         (pixel_type & (CS_VPlaneFirst | CS_UPlaneFirst)) == CS_VPlaneFirst; // Shouldn't use this
}

int VideoInfo::CX_BytesFromPixels(int pixels) const {
  const int componentSizes[8] = {1, 2, 4, 0, 0, 2, 2, 2};
  return (CX_NumComponents() > 1) && CX_IsPlanar()
             ? pixels * componentSizes[(pixel_type >> CS_Shift_Sample_Bits) & 7]
             : pixels * (CX_BitsPerPixel() >> 3);
  // For planar images, will return luma plane
}

int VideoInfo::CX_RowSize(int plane) const {
  const int rowsize = CX_BytesFromPixels(width);
  switch (plane) {
  case PLANAR_U:
  case PLANAR_V:
    return ((CX_NumComponents() > 1) && CX_IsPlanar() && !CX_IsPlanarRGB() && !CX_IsPlanarRGBA())
               ? rowsize >> CX_GetPlaneWidthSubsampling(plane)
               : 0;

  case PLANAR_U_ALIGNED:
  case PLANAR_V_ALIGNED:
    return ((CX_NumComponents() > 1) && CX_IsPlanar() && !CX_IsPlanarRGB() && !CX_IsPlanarRGBA())
               ? ((rowsize >> CX_GetPlaneWidthSubsampling(plane)) + FRAME_ALIGN - 1) &
                     (~(FRAME_ALIGN - 1))
               : 0; // Aligned rowsize

  case PLANAR_Y_ALIGNED:
    return (rowsize + FRAME_ALIGN - 1) & (~(FRAME_ALIGN - 1)); // Aligned rowsize

  case PLANAR_R:
  case PLANAR_G:
  case PLANAR_B:
    return ((CX_NumComponents() > 1) && (CX_IsPlanarRGB() || CX_IsPlanarRGBA())) ? rowsize : 0;

  case PLANAR_R_ALIGNED:
  case PLANAR_G_ALIGNED:
  case PLANAR_B_ALIGNED:
    return CX_IsPlanarRGB() || CX_IsPlanarRGBA()
               ? (rowsize + FRAME_ALIGN - 1) & (~(FRAME_ALIGN - 1))
               : 0; // Aligned rowsize

  case PLANAR_A:
    return ((CX_NumComponents() == 4) && CX_IsPlanar()) ? rowsize : 0;

  case PLANAR_A_ALIGNED:
    return ((CX_NumComponents() == 4) && CX_IsPlanar())
               ? (rowsize + FRAME_ALIGN - 1) & (~(FRAME_ALIGN - 1))
               : 0; // Aligned rowsize
  }
  return rowsize;
}

int VideoInfo::CX_BMPSize() const {
  if ((CX_NumComponents() > 1) && CX_IsPlanar()) {
    if (CX_IsPlanarRGB() || CX_IsPlanarRGBA()) {
      return ((CX_RowSize(PLANAR_G) + 3) & ~3) * height *
             CX_NumComponents(); // does it make sense for planar rgb?
    }
    // Y plane
    const int Ybytes = ((CX_RowSize(PLANAR_Y) + 3) & ~3) * height;
    const int UVbytes =
        Ybytes >> (CX_GetPlaneWidthSubsampling(PLANAR_U) + CX_GetPlaneHeightSubsampling(PLANAR_U));
    return Ybytes + UVbytes * 2;
  }
  return height * ((CX_RowSize() + 3) & ~3);
}

int VideoInfo::CX_GetPlaneWidthSubsampling(int plane) const { // Subsampling in bitshifts!
  if (plane == PLANAR_Y || plane == PLANAR_R || plane == PLANAR_G || plane == PLANAR_B ||
      plane == PLANAR_A) // No subsampling
    return 0;
  if (CX_NumComponents() == 1)
    throw AvisynthError(
        "Filter error: GetPlaneWidthSubsampling not available on greyscale pixel type.");
  if (plane == PLANAR_U || plane == PLANAR_V) {
    if (CX_IsYUY2())
      return 1;
    else if (CX_IsPlanar())
      return ((pixel_type >> CS_Shift_Sub_Width) + 1) & 3;
    else
      throw AvisynthError(
          "Filter error: GetPlaneWidthSubsampling called with unsupported pixel type.");
  }
  throw AvisynthError("Filter error: GetPlaneWidthSubsampling called with unsupported plane.");
}

int VideoInfo::CX_GetPlaneHeightSubsampling(int plane) const { // Subsampling in bitshifts!
  if (plane == PLANAR_Y || plane == PLANAR_R || plane == PLANAR_G || plane == PLANAR_B ||
      plane == PLANAR_A) // No subsampling
    return 0;
  if (CX_NumComponents() == 1)
    throw AvisynthError(
        "Filter error: GetPlaneHeightSubsampling not available on greyscale pixel type.");
  if (plane == PLANAR_U || plane == PLANAR_V) {
    if (CX_IsYUY2())
      return 0;
    else if (CX_IsPlanar())
      return ((pixel_type >> CS_Shift_Sub_Height) + 1) & 3;
    else
      throw AvisynthError(
          "Filter error: GetPlaneHeightSubsampling called with unsupported pixel type.");
  }
  throw AvisynthError("Filter error: GetPlaneHeightSubsampling called with unsupported plane.");
}

int VideoInfo::CX_BitsPerPixel() const {
  // Lookup Interleaved, calculate PLANAR's
  // Remark:
  // - total bitsize for interleaved/packed types, e.g. RGB48 = 48 = 3x16
  // - byte size corrected with U-V subsampling factor for Planar YUV/YUVA
  // - external softwares may use it for calculating buffer size
  // - use BitsPerComponent instead for returning the pixel component format 8/10/12/14/16/32 bits
  switch (pixel_type) {
  case CS_BGR24:
    return 24;
  case CS_BGR32:
    return 32;
  case CS_YUY2:
    return 16;
  case CS_Y8:
    return 8;
  case CS_Y10:
  case CS_Y12:
  case CS_Y14:
  case CS_Y16: // AVS16
    return 16;
  case CS_Y32:
    return 32;
  case CS_BGR48:
    return 48;
  case CS_BGR64:
    return 64;
  }
  if (CX_IsPlanar()) {
    const int componentSizes[8] = {1, 2, 4, 0, 0, 2, 2, 2};
    const int S = (CX_IsYUV() || CX_IsYUVA()) ? CX_GetPlaneWidthSubsampling(PLANAR_U) +
                                                    CX_GetPlaneHeightSubsampling(PLANAR_U)
                                              : 0; // planar RGBA: no subsampling
    const int fullSizePlaneCount =
        CX_IsYUVA() || CX_IsPlanarRGBA() ? 2 : 1; // alpha plane is always full size
    return (((fullSizePlaneCount << S) + 2) *
            (componentSizes[(pixel_type >> CS_Shift_Sample_Bits) & 7]) * 8) >>
           S;
  }
  return 0;
}

void VideoInfo::CX_SetFPS(unsigned numerator, unsigned denominator) {
  if ((numerator == 0) || (denominator == 0)) {
    fps_numerator = 0;
    fps_denominator = 1;
  } else {
    unsigned x = numerator, y = denominator;
    while (y) { // find gcd
      unsigned t = x % y;
      x = y;
      y = t;
    }
    fps_numerator = numerator / x;
    fps_denominator = denominator / x;
  }
}

void VideoInfo::CX_MulDivFPS(unsigned multiplier, unsigned divisor) {
  uint64_t numerator = UInt32x32To64(fps_numerator, multiplier);
  uint64_t denominator = UInt32x32To64(fps_denominator, divisor);

  uint64_t x = numerator, y = denominator;
  while (y) { // find gcd
    uint64_t t = x % y;
    x = y;
    y = t;
  }
  numerator /= x; // normalize
  denominator /= x;

  uint64_t temp = numerator | denominator; // Just looking top bit
  unsigned u = 0;
  while (temp & 0xffffffff80000000) { // or perhaps > 16777216*2
    temp = Int64ShrlMod32(temp, 1);
    u++;
  }
  if (u) { // Scale to fit
    const unsigned round = 1 << (u - 1);
    CX_SetFPS((unsigned)Int64ShrlMod32(numerator + round, u),
              (unsigned)Int64ShrlMod32(denominator + round, u));
  } else {
    fps_numerator = (unsigned)numerator;
    fps_denominator = (unsigned)denominator;
  }
}

bool VideoInfo::CX_IsSameColorspace(const VideoInfo &vi) const {
  if (vi.pixel_type == pixel_type)
    return TRUE;
  if (CX_IsYV12() && vi.CX_IsYV12())
    return TRUE;
  return FALSE;
}

int VideoInfo::CX_NumComponents() const {

  switch (pixel_type) {
  case CS_UNKNOWN:
    return 0;
  case CS_RAW32:
  case CS_Y8:
  case CS_Y10:
  case CS_Y12:
  case CS_Y14:
  case CS_Y16:
  case CS_Y32:
    return 1;
  case CS_BGR32:
  case CS_BGR64:
    return 4; // these are not planar but return the count
  default:
    return (CX_IsYUVA() || CX_IsPlanarRGBA()) ? 4 : 3;
    // 3: YUV, planar RGB, 24/48 bit RGB
    // 4: YUVA and PlanarRGBA
  }
}

int VideoInfo::CX_ComponentSize() const {
  // occupied bytes per one pixel component
  if (CX_IsPlanar()) {
    const int componentSizes[8] = {1, 2, 4, 0, 0, 2, 2, 2};
    return componentSizes[(pixel_type >> CS_Shift_Sample_Bits) & 7];
  }
  switch (pixel_type) {
  case CS_UNKNOWN:
    return 0;
  case CS_RAW32:
    return 4;
  case CS_BGR48:
  case CS_BGR64:
    return 2;
  default: // YUY2, packed 8 bit BGRs
    return 1;
  }
}

int VideoInfo::CX_BitsPerComponent() const {
  // unlike BitsPerPixel, this returns the real
  // component size as 8/10/12/14/16/32 bit
  if (pixel_type == CS_YUY2)
    return 8;
  else if (pixel_type == CS_RAW32)
    return 32;
  // planar YUV/RGB and packed RGB
  const int componentBitSizes[8] = {8, 16, 32, 0, 0, 10, 12, 14};
  return componentBitSizes[(pixel_type >> CS_Shift_Sample_Bits) & 7];
}

bool VideoInfo::CX_Is444() const {
  return ((pixel_type & CS_PLANAR_MASK & ~CS_Sample_Bits_Mask) ==
          (CS_GENERIC_YUV444 & CS_PLANAR_FILTER)) ||
         ((pixel_type & CS_PLANAR_MASK & ~CS_Sample_Bits_Mask) ==
          (CS_GENERIC_YUVA444 & CS_PLANAR_FILTER));
}

bool VideoInfo::CX_Is422() const {
  return ((pixel_type & CS_PLANAR_MASK & ~CS_Sample_Bits_Mask) ==
          (CS_GENERIC_YUV422 & CS_PLANAR_FILTER)) ||
         ((pixel_type & CS_PLANAR_MASK & ~CS_Sample_Bits_Mask) ==
          (CS_GENERIC_YUVA422 & CS_PLANAR_FILTER));
}

bool VideoInfo::CX_Is420() const {
  return ((pixel_type & CS_PLANAR_MASK & ~CS_Sample_Bits_Mask) ==
          (CS_GENERIC_YUV420 & CS_PLANAR_FILTER)) ||
         ((pixel_type & CS_PLANAR_MASK & ~CS_Sample_Bits_Mask) ==
          (CS_GENERIC_YUVA420 & CS_PLANAR_FILTER));
}

bool VideoInfo::CX_IsY() const {
  return (pixel_type & CS_PLANAR_MASK & ~CS_Sample_Bits_Mask) == (CS_GENERIC_Y & CS_PLANAR_FILTER);
}

bool VideoInfo::CX_IsRGB48() const {
  return ((pixel_type & CS_BGR24) == CS_BGR24) &&
         ((pixel_type & CS_Sample_Bits_Mask) == CS_Sample_Bits_16);
}

bool VideoInfo::CX_IsRGB64() const {
  return ((pixel_type & CS_BGR32) == CS_BGR32) &&
         ((pixel_type & CS_Sample_Bits_Mask) == CS_Sample_Bits_16);
}

bool VideoInfo::CX_IsYUVA() const { return !!(pixel_type & CS_YUVA); }

bool VideoInfo::CX_IsPlanarRGB() const {
  return !!(pixel_type & CS_PLANAR) && !!(pixel_type & CS_BGR) && !!(pixel_type & CS_RGB_TYPE);
}

bool VideoInfo::CX_IsPlanarRGBA() const {
  return !!(pixel_type & CS_PLANAR) && !!(pixel_type & CS_BGR) && !!(pixel_type & CS_RGBA_TYPE);
}

bool VideoInfo::CX_IsChannelMaskKnown() const { return !!(image_type & IT_HAS_CHANNELMASK); }

void VideoInfo::CX_SetChannelMask(bool isChannelMaskKnown, unsigned int dwChannelMask) {
  if (isChannelMaskKnown)
    image_type |= IT_HAS_CHANNELMASK;
  else
    image_type &= ~IT_HAS_CHANNELMASK;
  if (!isChannelMaskKnown)
    dwChannelMask = 0;
  if (dwChannelMask &
      AvsChannelMask::MASK_SPEAKER_ALL) // special mapping due to lack of bits in image_type
    dwChannelMask = AvsImageTypeFlags::IT_SPEAKER_ALL;
  else {
    dwChannelMask &= AvsChannelMask::MASK_SPEAKER_DEFINED;
    dwChannelMask <<= 4;
  }
  image_type &= ~AvsImageTypeFlags::IT_SPEAKER_BITS_MASK;
  image_type |= dwChannelMask;
}

unsigned int VideoInfo::CX_GetChannelMask() const {
  // returns zero if not defined
  if (!CX_IsChannelMaskKnown())
    return 0;
  unsigned int dwChannelMask = image_type & AvsImageTypeFlags::IT_SPEAKER_BITS_MASK;
  // returns the original SPEAKER_ALL constant
  if (dwChannelMask == AvsImageTypeFlags::IT_SPEAKER_ALL)
    return AvsChannelMask::MASK_SPEAKER_ALL;
  // other SPEAKER bits were simply shifted by 4 bits
  return dwChannelMask >> 4;
}

IClip *PClip::CX_GetPointerWithAddRef() const {
  if (p)
    AvsCxSdkAccess::Retain(p);
  return p;
}

void PClip::CX_Init(IClip *x) {
  if (x)
    AvsCxSdkAccess::Retain(x);
  p = x;
}

void PClip::CX_Set(IClip *x) {
  if (x)
    AvsCxSdkAccess::Retain(x);
  if (p)
    AvsCxSdkAccess::Release(p);
  p = x;
}

void PClip::CX_CONSTRUCTOR0() { p = 0; }

void PClip::CX_CONSTRUCTOR1(const PClip &x) { CX_Init(x.p); }

void PClip::CX_CONSTRUCTOR2(IClip *x) { CX_Init(x); }

void PClip::CX_OPERATOR_ASSIGN0(IClip *x) { CX_Set(x); }

void PClip::CX_OPERATOR_ASSIGN1(const PClip &x) { CX_Set(x.p); }

void PClip::CX_DESTRUCTOR() {
  if (p)
    AvsCxSdkAccess::Release(p);
}

void PVideoFrame::CX_Init(VideoFrame *x) {
  if (x)
    AvsCxSdkAccess::Retain(x);
  p = x;
}

void PVideoFrame::CX_Set(VideoFrame *x) {
  if (x)
    AvsCxSdkAccess::Retain(x);
  if (p)
    AvsCxSdkAccess::Release(p);
  p = x;
}

void PVideoFrame::CX_CONSTRUCTOR0() { p = 0; }

void PVideoFrame::CX_CONSTRUCTOR1(const PVideoFrame &x) { CX_Init(x.p); }

void PVideoFrame::CX_CONSTRUCTOR2(VideoFrame *x) { CX_Init(x); }

void PVideoFrame::CX_OPERATOR_ASSIGN0(VideoFrame *x) { CX_Set(x); }

void PVideoFrame::CX_OPERATOR_ASSIGN1(const PVideoFrame &x) { CX_Set(x.p); }

void PVideoFrame::CX_DESTRUCTOR() {
  if (p)
    AvsCxSdkAccess::Release(p);
}

void AVSValue::CX_CONSTRUCTOR0() {
  type = 'v';
  array_size = 0;
  clip = NULL;
}

void AVSValue::CX_CONSTRUCTOR1(IClip *c) {
  type = 'c';
  array_size = 0;
  clip = c;
  if (c)
    AvsCxSdkAccess::Retain(c);
}

void AVSValue::CX_CONSTRUCTOR2(const PClip &c) {
  type = 'c';
  array_size = 0;
  clip = c.CX_GetPointerWithAddRef();
}

void AVSValue::CX_CONSTRUCTOR3(bool b) {
  type = 'b';
  array_size = 0;
  clip = NULL;
  boolean = b;
}

void AVSValue::CX_CONSTRUCTOR4(int i) {
  type = 'i';
  array_size = 0;
  clip = NULL;
  integer = i;
}

void AVSValue::CX_CONSTRUCTOR5(float f) {
  type = 'f';
  array_size = 0;
  clip = NULL;
  floating_pt = f;
}

void AVSValue::CX_CONSTRUCTOR6(double f) {
  type = 'd';
  array_size = 0;
  clip = NULL;
#if UINTPTR_MAX >= 0xffffffffffffffff
  // pre-v11: floating_pt = float(f);
  double_pt = f; // v11: real 64 bit double!
#else
  double_pt_ptr = new double;
  *double_pt_ptr = f;
#endif
}

void AVSValue::CX_CONSTRUCTOR7(const char *s) {
  type = 's';
  array_size = 0;
  string = s;
}

void AVSValue::CX_CONSTRUCTOR8(const AVSValue *a, int size) {
  type = 'a';
  array_size = (short)size;
  if (a == nullptr)
    size = 0;
  array = new AVSValue[size]; // even for size = 0 a valid ptr is given
  for (int i = 0; i < size; i++) {
    const_cast<AVSValue *>(array)[i].CX_Assign(&a[i], true); // init from source
  }
}

void AVSValue::CX_CONSTRUCTOR9(const AVSValue &v) { CX_Assign(&v, true); }

void AVSValue::CX_CONSTRUCTOR10(const AVSValue &v, bool no_deep_arrays) {
  CX_Assign2(&v, true, no_deep_arrays);
}

void AVSValue::CX_CONSTRUCTOR11(const PFunction &n) {
  type = 'n';
  array_size = 0;
  function = nullptr;
  throw AvisynthError("CX SDK: function values are not supported");
}

void AVSValue::CX_CONSTRUCTOR12(int64_t l) {
  type = 'l';
  array_size = 0;
  clip = NULL;
#if UINTPTR_MAX >= 0xffffffffffffffff
  longlong = l;
#else
  longlong_ptr = new int64_t;
  *longlong_ptr = l;
#endif
}

void AVSValue::CX_DESTRUCTOR() {
  if (CX_IsClip() && clip)
    AvsCxSdkAccess::Release(clip);
  if (CX_IsFunction() && function)
    AvsCxSdkAccess::Release(function);
#if !(UINTPTR_MAX >= 0xffffffffffffffff)
  // 32-bit systems support 64-bit data through dynamic allocation.
  if (type == 'l' && longlong_ptr) {
    delete longlong_ptr;
    longlong_ptr = nullptr;
  } else if (type == 'd' && double_pt_ptr) {
    delete double_pt_ptr;
    double_pt_ptr = nullptr;
  }
#endif
  if (CX_IsArray() && array) {
    delete[] array; // calls AVSValue destructors for all elements
    array = nullptr;
  }
}

void AVSValue::CX_MarkArrayAsNonDeepCopy() {
  // Convert AVSValue content to a neutral 'v' type
  // to prevent destructor from freeing up the allocated array
  // and its elements.
  // Set just before releasing AVSValue.
  if (type == 'a') {
    type = 'v';
    array_size = 0;
    array = nullptr;
  }
}

AVSValue &AVSValue::CX_OPERATOR_ASSIGN(const AVSValue &v) {
  CX_Assign(&v, false);
  return *this;
}

bool AVSValue::CX_Defined() const { return type != 'v'; }

bool AVSValue::CX_IsClip() const { return type == 'c'; }

bool AVSValue::CX_IsBool() const { return type == 'b'; }

bool AVSValue::CX_IsInt() const { return type == 'i' || type == 'l'; }

bool AVSValue::CX_IsLongStrict() const { return type == 'l'; }

bool AVSValue::CX_IsFloat() const {
  return type == 'd' || type == 'f' || type == 'i' || type == 'l';
}

bool AVSValue::CX_IsFloatfStrict() const { return type == 'f'; }

bool AVSValue::CX_IsString() const { return type == 's'; }

bool AVSValue::CX_IsArray() const { return type == 'a'; }

bool AVSValue::CX_IsFunction() const { return type == 'n'; }

PClip AVSValue::CX_AsClip() const {
  _ASSERTE(CX_IsClip());
  return CX_IsClip() ? clip : 0;
}

bool AVSValue::CX_AsBool1() const {
  _ASSERTE(CX_IsBool());
  return boolean;
}

bool AVSValue::CX_AsBool() const { return CX_AsBool1(); }

int AVSValue::CX_AsInt1() const {
  _ASSERTE(CX_IsInt());
  // simple typecast, no saturation
#if UINTPTR_MAX >= 0xffffffffffffffff
  return type == 'i' ? integer : (int)longlong;
#else
  return type == 'i' ? integer : (int)*longlong_ptr;
#endif
}

int AVSValue::CX_AsInt() const { return CX_AsInt1(); }

int64_t AVSValue::CX_AsLong1() const {
  _ASSERTE(CX_IsInt());
#if UINTPTR_MAX >= 0xffffffffffffffff
  return type == 'i' ? integer : longlong;
#else
  return type == 'i' ? integer : *longlong_ptr;
#endif
}

int64_t AVSValue::CX_AsLong() const { return CX_AsLong1(); }

const char *AVSValue::CX_AsString1() const {
  _ASSERTE(CX_IsString());
  return CX_IsString() ? string : 0;
}

const char *AVSValue::CX_AsString() const { return AVSValue::CX_AsString1(); }

double AVSValue::CX_AsFloat1() const {
#if UINTPTR_MAX >= 0xffffffffffffffff
  _ASSERTE(CX_IsFloat());
  return type == 'i'   ? (double)integer
         : type == 'l' ? (double)longlong
         : type == 'f' ? (double)floating_pt
                       : double_pt;
#else
  _ASSERTE(CX_IsFloat());
  return type == 'i'   ? (double)integer
         : type == 'l' ? (double)*longlong_ptr
         : type == 'f' ? (double)floating_pt
                       : *double_pt_ptr;
#endif
}

double AVSValue::CX_AsFloat() const { return CX_AsFloat1(); }

float AVSValue::CX_AsFloatf() const {
#if UINTPTR_MAX >= 0xffffffffffffffff
  _ASSERTE(CX_IsFloat());
  return type == 'i'   ? (float)integer
         : type == 'l' ? (float)longlong
         : type == 'f' ? floating_pt
                       : (float)double_pt;
#else
  _ASSERTE(CX_IsFloat());
  return type == 'i'   ? (float)integer
         : type == 'l' ? (float)*longlong_ptr
         : type == 'f' ? floating_pt
                       : (float)*double_pt_ptr;
#endif
}

bool AVSValue::CX_AsBool2(bool def) const {
  _ASSERTE(CX_IsBool() || !CX_Defined());
  return CX_IsBool() ? boolean : def;
}

bool AVSValue::CX_AsBool(bool def) const { return CX_AsBool2(def); }

int AVSValue::CX_AsInt2(int def) const {
  _ASSERTE(CX_IsInt() || !CX_Defined());
#if UINTPTR_MAX >= 0xffffffffffffffff
  return type == 'i' ? integer : type == 'l' ? (int)longlong : def;
#else
  return type == 'i' ? integer : type == 'l' ? (int)*longlong_ptr : def;
#endif
}

int AVSValue::CX_AsInt(int def) const { return CX_AsInt2(def); }

int64_t AVSValue::CX_AsLong2(int64_t def) const {
  _ASSERTE(CX_IsInt() || !CX_Defined());
#if UINTPTR_MAX >= 0xffffffffffffffff
  return type == 'i' ? integer : type == 'l' ? longlong : def;
#else
  return type == 'i' ? integer : type == 'l' ? *longlong_ptr : def;
#endif
}

int64_t AVSValue::CX_AsLong(int64_t def) const { return CX_AsLong2(def); }

double AVSValue::CX_AsDblDef(double def) const {
  _ASSERTE(CX_IsFloat() || !CX_Defined());
#if UINTPTR_MAX >= 0xffffffffffffffff
  return type == 'i'   ? (double)integer
         : type == 'l' ? (double)longlong
         : type == 'f' ? (double)floating_pt
         : type == 'd' ? double_pt
                       : def;
#else
  return type == 'i'   ? (double)integer
         : type == 'l' ? (double)*longlong_ptr
         : type == 'f' ? (double)floating_pt
         : type == 'd' ? *double_pt_ptr
                       : def;
#endif
}

double AVSValue::CX_AsFloat2(float def) const {
  _ASSERTE(CX_IsFloat() || !CX_Defined());
#if UINTPTR_MAX >= 0xffffffffffffffff
  return type == 'i'   ? integer
         : type == 'l' ? (double)longlong
         : type == 'f' ? (double)floating_pt
         : type == 'd' ? double_pt
                       : (double)def;
#else
  return type == 'i'   ? integer
         : type == 'l' ? (double)*longlong_ptr
         : type == 'f' ? (double)floating_pt
         : type == 'd' ? *double_pt_ptr
                       : (double)def;
#endif
}

double AVSValue::CX_AsFloat(float def) const { return CX_AsFloat2(def); }

float AVSValue::CX_AsFloatf(float def) const { return float(CX_AsFloat2(def)); }

const char *AVSValue::CX_AsString2(const char *def) const {
  _ASSERTE(CX_IsString() || !CX_Defined());
  return CX_IsString() ? string : def;
}

const char *AVSValue::CX_AsString(const char *def) const { return AVSValue::CX_AsString2(def); }

PFunction AVSValue::CX_AsFunction() const {
  _ASSERTE(CX_IsFunction());
  return CX_IsFunction() ? function : 0;
}

int AVSValue::CX_ArraySize() const {
  _ASSERTE(CX_IsArray());
  return CX_IsArray() ? array_size : 1;
}

const AVSValue &AVSValue::CX_OPERATOR_INDEX(int index) const {
  _ASSERTE(CX_IsArray() && index >= 0 && index < array_size);
  return (CX_IsArray() && index >= 0 && index < array_size) ? array[index] : *this;
}

void AVSValue::CX_Assign(const AVSValue *src, bool init) { CX_Assign2(src, init, false); }

void AVSValue::CX_Assign2(const AVSValue *src, bool init, bool no_deep_arrays) {
  if (src->CX_IsClip() && src->clip)
    AvsCxSdkAccess::Retain(src->clip);
  if (src->CX_IsFunction() && src->function)
    AvsCxSdkAccess::Retain(src->function);

  // The left side of the assignment ("this") will be overwritten by "src",
  // so it should be released or freed if necessary.
  // The relevant properties must be released/dereferenced at the end:
  // clip, array, function, double_pt_ptr and longlong_ptr
  // Since they share the same pointer, saving (void*)clip would do it;
  const bool shouldReleaseClip = !init && CX_IsClip() && clip;
  const bool shouldReleaseFunction = !init && CX_IsFunction() && function;
  const bool shouldReleaseArray = !init && CX_IsArray() && array && !no_deep_arrays;
#if !(UINTPTR_MAX >= 0xffffffffffffffff)
  const bool shouldReleaseDouble = !init && type == 'd' && double_pt_ptr;
  const bool shouldReleaseLong = !init && type == 'l' && longlong_ptr;
#endif
  void *prev_pointer_to_release = (void *)clip;

  // common fields
  this->type = src->type;
  this->array_size = src->array_size;

  // no_deep_arrays: compatibility for AVS2.5: don't free or deep copy array elements!
  if (src->CX_IsArray() && !no_deep_arrays) {
    // Delay releasing the previous array to avoid the args = args[0] case.
    // If an existing array were freed before this point, it would break the
    // value = value[x] case where the source value[x] is not an array.
    // Even for size == 0, this returns a valid pointer.
    AVSValue *tmp = new AVSValue[array_size];
    for (int i = 0; i < array_size; i++)
      tmp[i].CX_Assign(&src->array[i], true); // init from source
    array = tmp;
  }
#if !(UINTPTR_MAX >= 0xffffffffffffffff)
  // 32 bit Avisynth: new 64 bit types are specially treated
  else if (this->type == 'l') {
    const uint64_t l = *src->longlong_ptr;
    this->longlong_ptr = new int64_t;
    *this->longlong_ptr = l;
  } else if (this->type == 'd') {
    const double d = *src->double_pt_ptr;
    this->double_pt_ptr = new double;
    *this->double_pt_ptr = d;
  }
#endif
  else {
    this->clip = (IClip *)((void *)src->clip);
    // "clip" is the largest member of the union, making sure we copy everything
  }

  if (shouldReleaseClip)
    AvsCxSdkAccess::Release((IClip *)prev_pointer_to_release);
  if (shouldReleaseFunction)
    AvsCxSdkAccess::Release((IFunction *)prev_pointer_to_release);
  if (shouldReleaseArray)
    delete[] (AVSValue *)(prev_pointer_to_release);
  // deallocates former array memory + calls destructor of AVSValue elements
#if !(UINTPTR_MAX >= 0xffffffffffffffff)
  if (shouldReleaseDouble)
    delete (double *)prev_pointer_to_release;
  else if (shouldReleaseLong)
    delete (int64_t *)prev_pointer_to_release;
#endif
}

AvsValueType AVSValue::CX_GetType() const { return (AvsValueType)type; }
