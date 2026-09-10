// Plugin-local member pointers. Never pass this table across CX.
static const AVS_Linkage cx_sdk_linkage = {
    // struct AVS_Linkage {

    sizeof(AVS_Linkage), //   int Size;

    /***************************************************************************************************************/
    // struct VideoInfo
    &VideoInfo::CX_HasVideo,        //   bool    (VideoInfo::*HasVideo)() const;
    &VideoInfo::CX_HasAudio,        //   bool    (VideoInfo::*HasAudio)() const;
    &VideoInfo::CX_IsRGB,           //   bool    (VideoInfo::*IsRGB)() const;
    &VideoInfo::CX_IsRGB24,         //   bool    (VideoInfo::*IsRGB24)() const;
    &VideoInfo::CX_IsRGB32,         //   bool    (VideoInfo::*IsRGB32)() const;
    &VideoInfo::CX_IsYUV,           //   bool    (VideoInfo::*IsYUV)() const;
    &VideoInfo::CX_IsYUY2,          //   bool    (VideoInfo::*IsYUY2)() const;
    &VideoInfo::CX_IsYV24,          //   bool    (VideoInfo::*IsYV24)()  const;
    &VideoInfo::CX_IsYV16,          //   bool    (VideoInfo::*IsYV16)()  const;
    &VideoInfo::CX_IsYV12,          //   bool    (VideoInfo::*IsYV12)()  const;
    &VideoInfo::CX_IsYV411,         //   bool    (VideoInfo::*IsYV411)() const;
    &VideoInfo::CX_IsY8,            //   bool    (VideoInfo::*IsY8)()    const;
    &VideoInfo::CX_IsColorSpace,    //   bool    (VideoInfo::*IsColorSpace)(int c_space) const;
    &VideoInfo::CX_Is,              //   bool    (VideoInfo::*Is)(int property) const;
    &VideoInfo::CX_IsPlanar,        //   bool    (VideoInfo::*IsPlanar)() const;
    &VideoInfo::CX_IsFieldBased,    //   bool    (VideoInfo::*IsFieldBased)() const;
    &VideoInfo::CX_IsParityKnown,   //   bool    (VideoInfo::*IsParityKnown)() const;
    &VideoInfo::CX_IsBFF,           //   bool    (VideoInfo::*IsBFF)() const;
    &VideoInfo::CX_IsTFF,           //   bool    (VideoInfo::*IsTFF)() const;
    &VideoInfo::CX_IsVPlaneFirst,   //   bool    (VideoInfo::*IsVPlaneFirst)() const;
    &VideoInfo::CX_BytesFromPixels, //   int     (VideoInfo::*BytesFromPixels)(int pixels) const;
    &VideoInfo::CX_RowSize,         //   int     (VideoInfo::*RowSize)(int plane) const;
    &VideoInfo::CX_BMPSize,         //   int     (VideoInfo::*BMPSize)() const;
    &VideoInfo::CX_AudioSamplesFromFrames, //   int64_t (VideoInfo::*AudioSamplesFromFrames)(int
                                           //   frames) const;
    &VideoInfo::CX_FramesFromAudioSamples, //   int     (VideoInfo::*FramesFromAudioSamples)(int64_t
                                           //   samples) const;
    &VideoInfo::CX_AudioSamplesFromBytes,  //   int64_t (VideoInfo::*AudioSamplesFromBytes)(int64_t
                                           //   bytes) const;
    &VideoInfo::CX_BytesFromAudioSamples,  //   int64_t (VideoInfo::*BytesFromAudioSamples)(int64_t
                                           //   samples) const;
    &VideoInfo::CX_AudioChannels,          //   int     (VideoInfo::*AudioChannels)() const;
    &VideoInfo::CX_SampleType,             //   int     (VideoInfo::*SampleType)() const;
    &VideoInfo::CX_IsSampleType,        //   bool    (VideoInfo::*IsSampleType)(int testtype) const;
    &VideoInfo::CX_SamplesPerSecond,    //   int     (VideoInfo::*SamplesPerSecond)() const;
    &VideoInfo::CX_BytesPerAudioSample, //   int     (VideoInfo::*BytesPerAudioSample)() const;
    &VideoInfo::CX_SetFieldBased,       //   void    (VideoInfo::*SetFieldBased)(bool isfieldbased);
    &VideoInfo::CX_Set,                 //   void    (VideoInfo::*Set)(int property);
    &VideoInfo::CX_Clear,               //   void    (VideoInfo::*Clear)(int property);
    &VideoInfo::CX_GetPlaneWidthSubsampling, //   int     (VideoInfo::*GetPlaneWidthSubsampling)(int
                                             //   plane) const;
    &VideoInfo::CX_GetPlaneHeightSubsampling, //   int (VideoInfo::*GetPlaneHeightSubsampling)(int
                                              //   plane) const;
    &VideoInfo::CX_BitsPerPixel,          //   int     (VideoInfo::*BitsPerPixel)() const;
    &VideoInfo::CX_BytesPerChannelSample, //   int     (VideoInfo::*BytesPerChannelSample)() const;
    &VideoInfo::CX_SetFPS,    //   void    (VideoInfo::*SetFPS)(unsigned numerator, unsigned
                              //   denominator)
    &VideoInfo::CX_MulDivFPS, //   void    (VideoInfo::*MulDivFPS)(unsigned multiplier, unsigned
                              //   divisor)
    &VideoInfo::CX_IsSameColorspace, //   bool    (VideoInfo::*IsSameColorspace)(const VideoInfo&
                                     //   vi) const;
                                     // end struct VideoInfo
    /***************************************************************************************************************/
    // class VideoFrameBuffer
    &VideoFrameBuffer::CX_GetReadPtr,  //   const BYTE* (VideoFrameBuffer::*VFBGetReadPtr)() const;
    &VideoFrameBuffer::CX_GetWritePtr, //   BYTE*       (VideoFrameBuffer::*VFBGetWritePtr)();
    &VideoFrameBuffer::CX_GetDataSize, //   int         (VideoFrameBuffer::*GetDataSize)() const;
    &VideoFrameBuffer::CX_GetSequenceNumber, //   int (VideoFrameBuffer::*GetSequenceNumber)()
                                             //   const;
    &VideoFrameBuffer::CX_GetRefcount, //   int         (VideoFrameBuffer::*GetRefcount)() const;
                                       // end class VideoFrameBuffer
    /***************************************************************************************************************/
    // class VideoFrame
    &VideoFrame::CX_GetPitch,   //   int               (VideoFrame::*GetPitch)(int plane) const;
    &VideoFrame::CX_GetRowSize, //   int               (VideoFrame::*GetRowSize)(int plane) const;
    &VideoFrame::CX_GetHeight,  //   int               (VideoFrame::*GetHeight)(int plane) const;
    &VideoFrame::CX_GetFrameBuffer, //   VideoFrameBuffer* (VideoFrame::*GetFrameBuffer)() const;
    &VideoFrame::CX_GetOffset,  //   int               (VideoFrame::*GetOffset)(int plane) const;
    &VideoFrame::CX_GetReadPtr, //   const BYTE*       (VideoFrame::*VFGetReadPtr)(int plane) const;
    &VideoFrame::CX_IsWritable, //   bool              (VideoFrame::*IsWritable)() const;
    &VideoFrame::CX_GetWritePtr, //   BYTE*             (VideoFrame::*VFGetWritePtr)(int plane)
                                 //   const;
    &VideoFrame::CX_DESTRUCTOR,  //   void              (VideoFrame::*VideoFrame_DESTRUCTOR)();
                                 // end class VideoFrame
    /***************************************************************************************************************/
    // class IClip
    //   /* nothing */
    // end class IClip
    /***************************************************************************************************************/
    // class PClip
    &PClip::CX_CONSTRUCTOR0,     //   void (PClip::*PClip_CONSTRUCTOR0)();
    &PClip::CX_CONSTRUCTOR1,     //   void (PClip::*PClip_CONSTRUCTOR1)(const PClip& x);
    &PClip::CX_CONSTRUCTOR2,     //   void (PClip::*PClip_CONSTRUCTOR2)(IClip* x);
    &PClip::CX_OPERATOR_ASSIGN0, //   void (PClip::*PClip_OPERATOR_ASSIGN0)(IClip* x);
    &PClip::CX_OPERATOR_ASSIGN1, //   void (PClip::*PClip_OPERATOR_ASSIGN1)(const PClip& x);
    &PClip::CX_DESTRUCTOR,       //   void (PClip::*PClip_DESTRUCTOR)();
                                 // end class PClip
    /***************************************************************************************************************/
    // class PVideoFrame
    &PVideoFrame::CX_CONSTRUCTOR0, //   void (PVideoFrame::*PVideoFrame_CONSTRUCTOR0)();
    &PVideoFrame::CX_CONSTRUCTOR1, //   void (PVideoFrame::*PVideoFrame_CONSTRUCTOR1)(const
                                   //   PVideoFrame& x);
    &PVideoFrame::CX_CONSTRUCTOR2, //   void (PVideoFrame::*PVideoFrame_CONSTRUCTOR2)(VideoFrame*
                                   //   x);
    &PVideoFrame::
        CX_OPERATOR_ASSIGN0, //   void (PVideoFrame::*PVideoFrame_OPERATOR_ASSIGN0(VideoFrame* x);
    &PVideoFrame::CX_OPERATOR_ASSIGN1, //   void (PVideoFrame::*PVideoFrame_OPERATOR_ASSIGN1(const
                                       //   PVideoFrame& x);
    &PVideoFrame::CX_DESTRUCTOR,       //   void (PVideoFrame::*PVideoFrame_DESTRUCTOR)();
                                       // end class PVideoFrame
    /***************************************************************************************************************/
    // class AVSValue
    &AVSValue::CX_CONSTRUCTOR0, //   void            (AVSValue::*AVSValue_CONSTRUCTOR0)();
    &AVSValue::CX_CONSTRUCTOR1, //   void            (AVSValue::*AVSValue_CONSTRUCTOR1)(IClip* c);
    &AVSValue::CX_CONSTRUCTOR2, //   void            (AVSValue::*AVSValue_CONSTRUCTOR2)(const PClip&
                                //   c);
    &AVSValue::CX_CONSTRUCTOR3, //   void            (AVSValue::*AVSValue_CONSTRUCTOR3)(bool b);
    &AVSValue::CX_CONSTRUCTOR4, //   void            (AVSValue::*AVSValue_CONSTRUCTOR4)(int i);
    &AVSValue::CX_CONSTRUCTOR5, //   void            (AVSValue::*AVSValue_CONSTRUCTOR5)(float f);
    &AVSValue::CX_CONSTRUCTOR6, //   void            (AVSValue::*AVSValue_CONSTRUCTOR6)(double f);
    &AVSValue::CX_CONSTRUCTOR7, //   void            (AVSValue::*AVSValue_CONSTRUCTOR7)(const char*
                                //   s);
    &AVSValue::CX_CONSTRUCTOR8, //   void            (AVSValue::*AVSValue_CONSTRUCTOR8)(const
                                //   AVSValue* a, int
    &AVSValue::CX_CONSTRUCTOR9, //   void            (AVSValue::*AVSValue_CONSTRUCTOR9)(const
                                //   AVSValue& v);
    &AVSValue::CX_DESTRUCTOR,   //   void            (AVSValue::*AVSValue_DESTRUCTOR)();
    &AVSValue::CX_OPERATOR_ASSIGN, //   AVSValue&       (AVSValue::*AVSValue_OPERATOR_ASSIGN)(const
                                   //   AVSValue& v);
    &AVSValue::CX_OPERATOR_INDEX,  //   const AVSValue& (AVSValue::*AVSValue_OPERATOR_INDEX)(int
                                   //   index) const;
    &AVSValue::CX_Defined,         //   bool            (AVSValue::*Defined)() const;
    &AVSValue::CX_IsClip,          //   bool            (AVSValue::*IsClip)() const;
    &AVSValue::CX_IsBool,          //   bool            (AVSValue::*IsBool)() const;
    &AVSValue::CX_IsInt,           //   bool            (AVSValue::*IsInt)() const;
    &AVSValue::CX_IsFloat,         //   bool            (AVSValue::*IsFloat)() const;
    &AVSValue::CX_IsString,        //   bool            (AVSValue::*IsString)() const;
    &AVSValue::CX_IsArray,         //   bool            (AVSValue::*IsArray)() const;
    &AVSValue::CX_AsClip,          //   PClip           (AVSValue::*AsClip)() const;
    &AVSValue::CX_AsBool1,         //   bool            (AVSValue::*AsBool1)() const;
    &AVSValue::CX_AsInt1,          //   int             (AVSValue::*AsInt1)() const;
    &AVSValue::CX_AsString1,       //   const char*     (AVSValue::*AsString1)() const;
    &AVSValue::CX_AsFloat1,        //   double          (AVSValue::*AsFloat1)() const; // AsDouble1
    &AVSValue::CX_AsBool2,         //   bool            (AVSValue::*AsBool2)(bool def) const;
    &AVSValue::CX_AsInt2,          //   int             (AVSValue::*AsInt2)(int def) const;
    &AVSValue::CX_AsDblDef,        //   double          (AVSValue::*AsDblDef)(double def) const; //
                                   //   AsDouble2
    &AVSValue::CX_AsFloat2,        //   double          (AVSValue::*AsFloat2)(float def) const;
    &AVSValue::CX_AsString2,    //   const char*     (AVSValue::*AsString2)(const char* def) const;
    &AVSValue::CX_ArraySize,    //   int             (AVSValue::*ArraySize)() const;
    &AVSValue::CX_IsLongStrict, //   bool            (AVSValue::*IsLongStrict)() const; // v11
    &AVSValue::CX_IsFloatfStrict, //   bool            (AVSValue::*IsFloatfStrict() const; // v11
    &AVSValue::CX_AsLong1,        //   int64_t         (AVSValue::*AsLong1)() const; // v11
    &AVSValue::CX_AsLong2, //   int64_t         (AVSValue::*AsLong2)(int64_t def) const; // v11
    &AVSValue::CX_CONSTRUCTOR12, //   void            (AVSValue::*AVSValue_CONSTRUCTOR12)(int64_ l);
                                 //   // v11
    // end class AVSValue
    /**********************************************************************/
    // a single { nullptr } initializes the whole placeholder array
    {nullptr}, // void    (VideoInfo::* reserved[27])(); // reserved for AVS classic
               /**********************************************************************/
    // AviSynth+ additions
    &VideoInfo::CX_NumComponents,    //   int     (VideoInfo::*NumChannels)() const;
    &VideoInfo::CX_ComponentSize,    //   int     (VideoInfo::*ComponentSize)() const;
    &VideoInfo::CX_BitsPerComponent, //   int     (VideoInfo::*BitsPerComponent)() const;
    &VideoInfo::CX_Is444,            //   bool    (VideoInfo::*Is444)()  const;
    &VideoInfo::CX_Is422,            //   bool    (VideoInfo::*Is422)()  const;
    &VideoInfo::CX_Is420,            //   bool    (VideoInfo::*Is420)()  const;
    &VideoInfo::CX_IsY,              //   bool    (VideoInfo::*IsY)()    const;
    &VideoInfo::CX_IsRGB48,          //   bool    (VideoInfo::*IsRGB48)()  const;
    &VideoInfo::CX_IsRGB64,          //   bool    (VideoInfo::*IsRGB64)()  const;
    &VideoInfo::CX_IsYUVA,           //   bool    (VideoInfo::*IsYUVA)()  const;
    &VideoInfo::CX_IsPlanarRGB,      //   bool    (VideoInfo::*IsPlanarRGB)()  const;
    &VideoInfo::CX_IsPlanarRGBA,     //   bool    (VideoInfo::*IsPlanarRGBA)()  const;
    /**********************************************************************/
    // Frame properties
    &VideoFrame::CX_getProperties,      // AVSMap& (VideoFrame::* getProperties)();
    &VideoFrame::CX_getConstProperties, // const AVSMap& (VideoFrame::* getConstProperties)();
    &VideoFrame::CX_setProperties, // void (VideoFrame::* setProperties)(const AVSMap& properties);

    // PFunction (Neo)
    &AVSValue::CX_CONSTRUCTOR11,
    &AVSValue::CX_IsFunction,
    &PFunction::CX_CONSTRUCTOR0,
    &PFunction::CX_CONSTRUCTOR1,
    &PFunction::CX_CONSTRUCTOR2,
    &PFunction::CX_OPERATOR_ASSIGN0,
    &PFunction::CX_OPERATOR_ASSIGN1,
    &PFunction::CX_DESTRUCTOR,
    // end PFunction

    // VideoFrame extras (Neo)
    &VideoFrame::CX_CheckMemory,
    &VideoFrame::CX_GetDevice,

    // class PDevice (Neo)
    &PDevice::CX_CONSTRUCTOR0,
    &PDevice::CX_CONSTRUCTOR1,
    &PDevice::CX_CONSTRUCTOR2,
    &PDevice::CX_OPERATOR_ASSIGN0,
    &PDevice::CX_OPERATOR_ASSIGN1,
    &PDevice::CX_DESTRUCTOR,
    &PDevice::CX_GetType,
    &PDevice::CX_GetId,
    &PDevice::CX_GetIndex,
    &PDevice::CX_GetName,
    // end class PDevice

    // V9
    &VideoFrame::CX_IsPropertyWritable,

    // V10
    &VideoFrame::CX_GetPixelType, //   int          (VideoFrame::*VideoFrame_GetPixelType)() const;
    &VideoFrame::CX_AmendPixelType,   //   void         (VideoFrame::*VideoFrame_AmendPixelType)();
    &VideoFrameBuffer::CX_DESTRUCTOR, //   void (VideoFrameBuffer::*VideoFrameBuffer_DESTRUCTOR)();
    &AVSValue::CX_GetType,            //   AvsValueType (AVSValue::*AVSValue_GetType)() const;

    // V10.1
    &VideoInfo::CX_IsChannelMaskKnown, //   bool    (VideoInfo::*IsChannelMaskKnown)()  const;
    &VideoInfo::CX_SetChannelMask,     //   void    (VideoInfo::*SetChannelMask)();
    &VideoInfo::CX_GetChannelMask,     //   int     (VideoInfo::*GetChannelMask)()  const;

    // a single { nullptr } initializes the whole placeholder array
    {nullptr}, // void          (VideoInfo::* reserved2[64 - 31])(); // Reserve pointer space for
               // Avisynth+

    /**********************************************************************/
    // AviSynth Neo additions
    &CxGetNeoEnv
    // Most Neo linkage entries are moved to standard avs+ place.
    // frame property logic has been replaced entirely

    // this part should be identical with struct AVS_Linkage in avisynth.h

    /**********************************************************************/
}; // }
