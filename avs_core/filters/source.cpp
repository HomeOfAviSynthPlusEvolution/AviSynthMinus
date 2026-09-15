// Avisynth v2.5.  Copyright 2002 Ben Rudiak-Gould et al.
// http://avisynth.nl

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


#include "../core/internal.h"
#include "../convert/convert_matrix.h"
#include "../convert/convert_helper.h"
#ifdef AVS_WINDOWS
#include "AviSource/avi_source.h"
#endif
#include "../convert/convert_planar.h"

#define PI 3.1415926535897932384626433832795
#include <ctime>
#include <cmath>
#include <new>
#include <memory>
#include <cassert>
#include <stdint.h>
#include <algorithm>

/********************************************************************
********************************************************************/

enum {
    COLOR_MODE_RGB = 0,
    COLOR_MODE_YUV
};

class StaticImage : public IClip {
  const VideoInfo vi;
  const PVideoFrame frame;
  bool parity;

public:
  StaticImage(const VideoInfo& _vi, const PVideoFrame& _frame, bool _parity)
    : vi(_vi), frame(_frame), parity(_parity) {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) {
    AVS_UNUSED(n);
    AVS_UNUSED(env);
    return frame;
  }
  void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env) {
    AVS_UNUSED(start);
    AVS_UNUSED(env);
    memset(buf, 0, (size_t)vi.BytesFromAudioSamples(count));
  }
  const VideoInfo& __stdcall GetVideoInfo() { return vi; }
  bool __stdcall GetParity(int n) { return (vi.IsFieldBased() ? (n&1) : false) ^ parity; }
  int __stdcall SetCacheHints(int cachehints,int frame_range)
  {
    AVS_UNUSED(frame_range);
    switch (cachehints)
    {
    case CACHE_DONT_CACHE_ME:
      return 1;
    case CACHE_GET_MTMODE:
      return MT_NICE_FILTER;
    case CACHE_GET_DEV_TYPE:
       return DEV_TYPE_CPU;
    case CACHE_GET_CHILD_DEV_TYPE:
       return DEV_TYPE_ANY; // any type is ok because this clip does not require child's frames.
    default:
      return 0;
    }
  }
};

// For any frame number, this clip returns the first frame of a child clip .
// This clip makes cache effective and reduce unnecessary frame transfer.
class SingleFrame : public GenericVideoFilter {
public:
  SingleFrame(PClip _child) : GenericVideoFilter(_child) {}
  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) { return child->GetFrame(0, env); }
  int __stdcall SetCacheHints(int cachehints, int frame_range)
  {
    switch (cachehints)
    {
    case CACHE_DONT_CACHE_ME:
      return 1;
    case CACHE_GET_MTMODE:
      return MT_NICE_FILTER;
    case CACHE_GET_DEV_TYPE:
      return (child->GetVersion() >= 5) ? child->SetCacheHints(CACHE_GET_DEV_TYPE, 0) : 0;
    default:
      return 0;
    }
  }
};


static PVideoFrame CreateBlankFrame(const VideoInfo& vi, int color, int mode, const int *colors, const float *colors_f, bool color_is_array, IScriptEnvironment* env) {

  if (!vi.HasVideo()) return 0;

  PVideoFrame frame = env->NewVideoFrame(vi);
  // no frame property origin

  // but we set Rec601 (ST170) if YUV
  auto props = env->getFramePropsRW(frame);
  int theMatrix = vi.IsRGB() ? Matrix_e::AVS_MATRIX_RGB : Matrix_e::AVS_MATRIX_ST170_M;
  int theColorRange = vi.IsRGB() ? ColorRange_e::AVS_RANGE_FULL : ColorRange_e::AVS_RANGE_LIMITED;
  update_Matrix_and_ColorRange(props, theMatrix, theColorRange, env);

  // RGB 8->16 bit: not << 8 like YUV but 0..255 -> 0..65535 or 0..1023 for 10 bit
  int pixelsize = vi.ComponentSize();
  int bits_per_pixel = vi.BitsPerComponent();
  int max_pixel_value = pixelsize == 4 ? 1 : (1 << bits_per_pixel) - 1;
  auto rgbcolor8to16 = [](uint8_t color8, int max_pixel_value) { return (uint16_t)(color8 * max_pixel_value / 255); };

  // int color holds the "old" 8 bit color values that are scaled automatically to the right bitmap
  // new in avs+: if color_is_array, color values are filled as-is, no conversion or any shift occurs

  if (vi.IsPlanar()) {

    bool isyuvlike = vi.IsYUV() || vi.IsYUVA();

    if (color_is_array) {
          // works from colors or colors_f: as-is
          // color order in the array: RGBA or YUVA
      int planes_y[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
      int planes_r[4] = { PLANAR_R, PLANAR_G, PLANAR_B, PLANAR_A };
      int *planes = isyuvlike ? planes_y : planes_r;

      for (int p = 0; p < vi.NumComponents(); p++)
      {
        int plane = planes[p];
        BYTE *dstp = frame->GetWritePtr(plane);
        int rowsize = frame->GetRowSize(plane);
        int pitch = frame->GetPitch(plane);
        int height = frame->GetHeight(plane);
        switch (pixelsize) {
        case 1: fill_plane<uint8_t>(dstp, height, rowsize, pitch, clamp(colors[p], 0, 0xFF)); break;
        case 2: fill_plane<uint16_t>(dstp, height, rowsize, pitch, clamp(colors[p], 0, (1 << vi.BitsPerComponent()) - 1)); break;
        case 4: fill_plane<float>(dstp, height, rowsize, pitch, colors_f[p]); break;
        }
      }
    }
    else {
      int color_yuv = (mode == COLOR_MODE_YUV) ? color : RGB2YUV_Rec601(color);

      int val_i = 0;

      int planes_y[4] = { PLANAR_Y, PLANAR_U, PLANAR_V, PLANAR_A };
      int planes_r[4] = { PLANAR_G, PLANAR_B, PLANAR_R, PLANAR_A };
      int *planes = isyuvlike ? planes_y : planes_r;

      for (int p = 0; p < vi.NumComponents(); p++)
      {
        int plane = planes[p];
        // color order: ARGB or AYUV
        // (8)-8-8-8 bit color from int parameter
        if (isyuvlike) {
          switch (plane) {
          case PLANAR_A: val_i = (color >> 24) & 0xff; break;
          case PLANAR_Y: val_i = (color_yuv >> 16) & 0xff; break;
          case PLANAR_U: val_i = (color_yuv >> 8) & 0xff; break;
          case PLANAR_V: val_i = color_yuv & 0xff; break;
          }
          if (bits_per_pixel != 32)
            val_i = val_i << (bits_per_pixel - 8);
        }
        else {
         // planar RGB
          switch (plane) {
          case PLANAR_A: val_i = (color >> 24) & 0xff; break;
          case PLANAR_R: val_i = (color >> 16) & 0xff; break;
          case PLANAR_G: val_i = (color >> 8) & 0xff; break;
          case PLANAR_B: val_i = color & 0xff; break;
          }
          if (bits_per_pixel != 32)
            val_i = rgbcolor8to16(val_i, max_pixel_value);
        }


        BYTE *dstp = frame->GetWritePtr(plane);
        int size = frame->GetPitch(plane) * frame->GetHeight(plane);

        switch (pixelsize) {
        case 1:
          memset(dstp, val_i, size);
          break;
        case 2:
          val_i = clamp(val_i, 0, (1 << vi.BitsPerComponent()) - 1);
          std::fill_n((uint16_t *)dstp, size / sizeof(uint16_t), (uint16_t)val_i);
          break; // 2 pixels at a time
        default: // case 4:
          float val_f;
          if (plane == PLANAR_U || plane == PLANAR_V) {
#ifdef FLOAT_CHROMA_IS_HALF_CENTERED
            float shift = 0.5f;
#else
            float shift = 0.0f;
#endif
            val_f = float(val_i - 128) / 255.0f + shift; // 32 bit float chroma 128=0.5
          }
          else {
            val_f = float(val_i) / 255.0f;
          }
          std::fill_n((float *)dstp, size / sizeof(float), val_f);
        }
      }
    }
    return frame;
  } // if planar

  BYTE* p = frame->GetWritePtr();
  int size = frame->GetPitch() * frame->GetHeight();

  if (vi.IsYUY2()) {
    int color_yuv =(mode == COLOR_MODE_YUV) ? color : RGB2YUV_Rec601(color);
    if (color_is_array) {
      color_yuv = (clamp(colors[0], 0, max_pixel_value) << 16) | (clamp(colors[1], 0, max_pixel_value) << 8) | (clamp(colors[2], 0, max_pixel_value));
    }
    uint32_t d = ((color_yuv>>16)&255) * 0x010001u + ((color_yuv>>8)&255) * 0x0100u + (color_yuv&255) * 0x01000000u;
    for (int i=0; i<size; i+=4)
      *(uint32_t *)(p+i) = d;
  } else if (vi.IsRGB24()) {
    const uint8_t color_b = color_is_array ? clamp(colors[2], 0, max_pixel_value) : (uint8_t)(color & 0xFF);
    const uint8_t color_g = color_is_array ? clamp(colors[1], 0, max_pixel_value) : (uint8_t)(color >> 8);
    const uint8_t color_r = color_is_array ? clamp(colors[0], 0, max_pixel_value) : (uint8_t)(color >> 16);
    const int rowsize = frame->GetRowSize();
    const int pitch = frame->GetPitch();
    for (int y=frame->GetHeight();y>0;y--) {
      for (int i=0; i<rowsize; i+=3) {
        p[i] = color_b;
        p[i+1] = color_g;
        p[i+2] = color_r;
      }
      p+=pitch;
    }
  } else if (vi.IsRGB32()) {
    uint32_t c;
    c = color;
    if (color_is_array) {
      uint32_t r = clamp(colors[0], 0, max_pixel_value);
      uint32_t g = clamp(colors[1], 0, max_pixel_value);
      uint32_t b = clamp(colors[2], 0, max_pixel_value);
      uint32_t a = clamp(colors[3], 0, max_pixel_value);
      c = (a << 24) + (r << 16) + (g << 8) + (b);
    }
    std::fill_n((uint32_t *)p, size / 4, c);
    //for (int i=0; i<size; i+=4)
    //  *(unsigned*)(p+i) = color;
  } else if (vi.IsRGB48()) {
      const uint16_t color_b  = color_is_array ? clamp(colors[2], 0, max_pixel_value) : rgbcolor8to16(color & 0xFF, max_pixel_value);
      uint16_t r = color_is_array ? clamp(colors[0], 0, max_pixel_value) : rgbcolor8to16((color >> 16) & 0xFF, max_pixel_value);
      uint16_t g = color_is_array ? clamp(colors[1], 0, max_pixel_value) : rgbcolor8to16((color >> 8 ) & 0xFF, max_pixel_value);
      const int rowsize = frame->GetRowSize() / sizeof(uint16_t);
      const int pitch = frame->GetPitch() / sizeof(uint16_t);
      uint16_t* p16 = reinterpret_cast<uint16_t*>(p);
      for (int y=frame->GetHeight();y>0;y--) {
          for (int i=0; i<rowsize; i+=3) {
              p16[i] = color_b;   // b
              p16[i+1] = g;
              p16[i+2] = r;
          }
          p16 += pitch;
      }
  } else if (vi.IsRGB64()) {
    uint64_t r, g, b, a;
    r = color_is_array ? clamp(colors[0], 0, max_pixel_value) : rgbcolor8to16((color >> 16) & 0xFF, max_pixel_value);
    g = color_is_array ? clamp(colors[1], 0, max_pixel_value) : rgbcolor8to16((color >> 8 ) & 0xFF, max_pixel_value);
    b = color_is_array ? clamp(colors[2], 0, max_pixel_value) : rgbcolor8to16((color      ) & 0xFF, max_pixel_value);
    a = color_is_array ? clamp(colors[3], 0, max_pixel_value) : rgbcolor8to16((color >> 24) & 0xFF, max_pixel_value);
    uint64_t color64 = (a << 48) + (r << 32) + (g << 16) + (b);
    std::fill_n(reinterpret_cast<uint64_t*>(p), size / sizeof(uint64_t), color64);
  }
  return frame;
}





/********************************************************************
********************************************************************/

#if defined(AVS_WINDOWS) && !defined(NO_WIN_GDI)
// in text-overlay.cpp
extern bool GetTextBoundingBox(const char* text, const char* fontname,
  int size, bool bold, bool italic, int align, int* width, int* height);
#endif

extern bool GetTextBoundingBoxFixed(const char* text, const char* fontname, int size, bool bold,
  bool italic, int align, int& width, int& height, bool utf8);


PClip Create_MessageClip(const char* message, int width, int height, int pixel_type, bool shrink,
                         int textcolor, int halocolor, int bgcolor,
                         int fps_numerator, int fps_denominator, int num_frames,
                         IScriptEnvironment* env) {
  int size;
#if defined(AVS_WINDOWS) && !defined(NO_WIN_GDI)
  // MessageClip produces a clip containing a text message.Used internally for error reporting.
  // The font face is "Arial".
  // The font size is between 24 points and 9 points - chosen to fit, if possible,
  // in the width by height clip.
  // The pixeltype is RGB32.

  for (size = 24*8; /*size>=9*8*/; size-=4) {
    int text_width, text_height;
    GetTextBoundingBox(message, "Arial", size, true, false, TA_TOP | TA_CENTER, &text_width, &text_height);
    text_width = ((text_width>>3)+8+7) & ~7; // mod 8
    text_height = ((text_height>>3)+8+1) & ~1; // mod 2
    if (size <= 9 * 8 || ((width <= 0 || text_width <= width) && (height <= 0 || text_height <= height))) {
      if (width <= 0 || (shrink && width > text_width))
        width = text_width;
      if (height <= 0 || (shrink && height > text_height))
        height = text_height;
      break;
    }
  }
#else
  constexpr int MAX_SIZE = 24; // Terminus 12,14,16,18,20,22,24,28,32
  constexpr int MIN_SIZE = 12;
  for (size = MAX_SIZE; /*size>=9*/; size -= 2) {
    int text_width, text_height;
#ifdef AVS_POSIX
    bool utf8 = true;
#else
    bool utf8 = false;
#endif
    GetTextBoundingBoxFixed(message, "Terminus", size, true, false, 0 /* align */, text_width, text_height, utf8);
    text_width = (text_width + 8 + 7) & ~7; // mod 8
    text_height = (text_height + 8 + 1) & ~1; // mod 2
    if (size <= MIN_SIZE || ((width <= 0 || text_width <= width) && (height <= 0 || text_height <= height))) {
      if (width <= 0 || (shrink && width > text_width))
        width = text_width;
      if (height <= 0 || (shrink && height > text_height))
        height = text_height;
      break;
    }
  }

  size *= 8; // back to GDI units
#endif

  VideoInfo vi;
  memset(&vi, 0, sizeof(vi));
  vi.width = width;
  vi.height = height;
  vi.pixel_type = pixel_type;
  vi.fps_numerator = fps_numerator > 0 ? fps_numerator : 24;
  vi.fps_denominator = fps_denominator > 0 ? fps_denominator : 1;
  vi.num_frames = num_frames > 0 ? num_frames : 240;

  PVideoFrame frame = CreateBlankFrame(vi, bgcolor, COLOR_MODE_RGB, nullptr, nullptr, false, env);
  env->ApplyMessage(&frame, vi, message, size, textcolor, halocolor, bgcolor);
  PClip clip = new StaticImage(vi, frame, false);

  // wrap in OnCPU to support multi devices
  AVSValue args[2]{ clip, 1 }; // prefetch=1: enable cache but not thread
  return new SingleFrame(env->Invoke("OnCPU", AVSValue(args, 2)).AsClip());
};

AVSValue __cdecl Create_MessageClip(AVSValue args, void*, IScriptEnvironment* env) {
  return Create_MessageClip(args[0].AsString(), args[1].AsInt(-1),
      args[2].AsInt(-1), VideoInfo::CS_BGR32, args[3].AsBool(false),
      args[4].AsInt(0xFFFFFF), args[5].AsInt(0), args[6].AsInt(0),
      -1, -1, -1, // fps_numerator, fps_denominator, num_frames: auto
    env);
}


/********************************************************************
********************************************************************/


#ifdef AVS_WINDOWS
// AviSource is Windows-only, because it explicitly relies on Video for Windows
AVSValue __cdecl Create_SegmentedSource(AVSValue args, void* use_directshow, IScriptEnvironment* env) {
  bool bAudio = !use_directshow && args[1].AsBool(true);
  const char* pixel_type = 0;
  const char* fourCC = 0;
  int vtrack = 0;
  int atrack = 0;
  bool utf8;
  const int inv_args_count = args.ArraySize();
  AVSValue inv_args[9];
  if (!use_directshow) {
    pixel_type = args[2].AsString("");
    fourCC = args[3].AsString("");
    vtrack = args[4].AsInt(0);
    atrack = args[5].AsInt(0);
    utf8 = args[6].AsBool(false);
  }
  else {
    for (int i=1; i<inv_args_count ;i++)
      inv_args[i] = args[i];
  }
  args = args[0];
  PClip result = 0;
  const char* error_msg=0;
  for (int i = 0; i < args.ArraySize(); ++i) {
    char basename[260];
    strcpy(basename, args[i].AsString());
    char* extension = strrchr(basename, '.');
    if (extension)
      *extension++ = 0;
    else
      extension[0] = 0;
    for (int j = 0; j < 100; ++j) {
      char filename[260];
      wsprintf(filename, "%s.%02d.%s", basename, j, extension);
      if (GetFileAttributes(filename) != (DWORD)-1) {   // check if file exists
          PClip clip;
        try {
          if (use_directshow) {
            inv_args[0] = filename;
            clip = env->Invoke("DirectShowSource",AVSValue(inv_args, inv_args_count)).AsClip(); // no utf8 yet
          } else {
            clip =  (IClip*)(new AVISource(filename, bAudio, pixel_type, fourCC, vtrack, atrack, AVISource::MODE_NORMAL, utf8, env));
          }
          AVSValue arg[3] = { result, clip, 0 };
          result = !result ? clip : env->Invoke("UnalignedSplice", AVSValue(arg, 3)).AsClip();
        } catch (const AvisynthError &e) {
          error_msg=e.msg;
        }
      }
    }
  }
  if (!result) {
    if (!error_msg) {
      env->ThrowError("Segmented%sSource: no files found!", use_directshow ? "DirectShow" : "AVI");
    } else {
      env->ThrowError("Segmented%sSource: decompressor returned error:\n%s!", use_directshow ? "DirectShow" : "AVI",error_msg);
    }
  }
  return result;
}
#endif

/**********************************************************
 *                         TONE                           *
 **********************************************************/
class SampleGenerator {
public:
  SampleGenerator() {}
  virtual ~SampleGenerator() = default;
  virtual SFLOAT getValueAt(double where) {
    AVS_UNUSED(where);
    return 0.0f;}
};

class SineGenerator : public SampleGenerator {
public:
  SineGenerator() {}
  SFLOAT getValueAt(double where) {return (SFLOAT)sin(PI * where * 2.0);}
};


class NoiseGenerator : public SampleGenerator {
public:
  NoiseGenerator() {
    srand( (unsigned)time( NULL ) );
  }

  SFLOAT getValueAt(double where) {
    AVS_UNUSED(where);
    return (float) rand()*(2.0f/RAND_MAX) -1.0f;}
};

class SquareGenerator : public SampleGenerator {
public:
  SquareGenerator() {}

  SFLOAT getValueAt(double where) {
    if (where<=0.5) {
      return 1.0f;
    } else {
      return -1.0f;
    }
  }
};

class TriangleGenerator : public SampleGenerator {
public:
  TriangleGenerator() {}

  SFLOAT getValueAt(double where) {
    if (where<=0.25) {
      return (SFLOAT)(where*4.0);
    } else if (where<=0.75) {
      return (SFLOAT)((-4.0*(where-0.50)));
    } else {
      return (SFLOAT)((4.0*(where-1.00)));
    }
  }
};

class SawtoothGenerator : public SampleGenerator {
public:
  SawtoothGenerator() {}

  SFLOAT getValueAt(double where) {
    return (SFLOAT)(2.0*(where-0.5));
  }
};


class Tone : public IClip {
  VideoInfo vi;
  std::unique_ptr<SampleGenerator> s;
  const double freq;            // Frequency in Hz
  const double samplerate;      // Samples per second
  const int ch;                 // Number of channels
  const double add_per_sample;  // How much should we add per sample in seconds
  const float level;

public:

  Tone(double _length, double _freq, int _samplerate, int _ch, const char* _type, float _level, IScriptEnvironment* env):
             freq(_freq), samplerate(_samplerate), ch(_ch), add_per_sample(_freq/_samplerate), level(_level) {
    memset(&vi, 0, sizeof(VideoInfo));
    vi.sample_type = SAMPLE_FLOAT;
    vi.nchannels = _ch;
    vi.audio_samples_per_second = _samplerate;
    vi.num_audio_samples=(int64_t)(_length*vi.audio_samples_per_second+0.5);

    if (!lstrcmpi(_type, "Sine"))
      s = std::make_unique<SineGenerator>();
    else if (!lstrcmpi(_type, "Noise"))
      s = std::make_unique<NoiseGenerator>();
    else if (!lstrcmpi(_type, "Square"))
      s = std::make_unique<SquareGenerator>();
    else if (!lstrcmpi(_type, "Triangle"))
      s = std::make_unique<TriangleGenerator>();
    else if (!lstrcmpi(_type, "Sawtooth"))
      s = std::make_unique<SawtoothGenerator>();
    else if (!lstrcmpi(_type, "Silence"))
      s = std::make_unique<SampleGenerator>();
    else
      env->ThrowError("Tone: Type was not recognized!");
  }

  void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env) {
    AVS_UNUSED(env);
    // Where in the cycle are we in?
    const double cycle = (freq * start) / samplerate;
    double period_place = cycle - floor(cycle);

    SFLOAT* samples = (SFLOAT*)buf;

    for (int i=0;i<count;i++) {
      SFLOAT v = s->getValueAt(period_place) * level;
      for (int o=0;o<ch;o++) {
        samples[o+i*ch] = v;
      }
      period_place += add_per_sample;
      if (period_place >= 1.0)
        period_place -= floor(period_place);
    }
  }

  static AVSValue __cdecl Create(AVSValue args, void*, IScriptEnvironment* env)
  {
    int samplerate = args[2].AsInt(48000);
    int channels = args[3].AsInt(2);
    if (samplerate <= 0)
      env->ThrowError("Tone: samplerate must be greater than zero");
    if (channels <= 0)
      env->ThrowError("Tone: channels must be greater than zero");
    return new Tone(args[0].AsFloat(10.0f), args[1].AsFloat(440.0f), samplerate,
          channels, args[4].AsString("Sine"), args[5].AsFloatf(1.0f), env);
  }

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) {
    AVS_UNUSED(n);
    AVS_UNUSED(env);
    return NULL; }
  const VideoInfo& __stdcall GetVideoInfo() { return vi; }
  bool __stdcall GetParity(int n) {
    AVS_UNUSED(n);
    return false; }
  int __stdcall SetCacheHints(int cachehints,int frame_range) {
    AVS_UNUSED(cachehints);
    AVS_UNUSED(frame_range);
    return 0; };

};


AVSValue __cdecl Create_Version(AVSValue args, void*, IScriptEnvironment* env) {
  //     0       1       2           3         4
  // [length]i[width]i[height]i[pixel_type]s[clip]c
  VideoInfo vi_default;

  int i_pixel_type = VideoInfo::CS_BGR24;

  const bool has_clip = args[4].Defined();
  if (has_clip) {
    // clip overrides
    vi_default = args[4].AsClip()->GetVideoInfo();
    i_pixel_type = vi_default.pixel_type;
  }

  if (args[3].Defined()) {
    i_pixel_type = GetPixelTypeFromName(args[3].AsString());
    if (i_pixel_type == VideoInfo::CS_UNKNOWN)
      env->ThrowError("Version: invalid 'pixel_type'");
  }

  int num_frames = args[0].AsInt(has_clip ? vi_default.num_frames : -1); // auto (240)
  int w = args[1].AsInt(has_clip ? vi_default.width : -1); // auto
  int h = args[2].AsInt(has_clip ? vi_default.height : -1); // auto
  const bool shrink = false;
  const int textcolor = 0xECF2BF;
  const int halocolor = 0;
  const int bgcolor = 0x404040;

  const int fps_numerator = has_clip ? vi_default.fps_numerator :-1; // auto
  const int fps_denominator = has_clip ? vi_default.fps_denominator : -1; // auto

  return Create_MessageClip(
#ifdef AVS_POSIX
    AVS_FULLVERSION AVS_VERSION_DATE AVS_COPYRIGHT_UTF8
#else
    AVS_FULLVERSION AVS_VERSION_DATE AVS_COPYRIGHT
#endif
    , w, h, i_pixel_type, shrink, textcolor, halocolor, bgcolor, fps_numerator, fps_denominator, num_frames, env);
}


extern const AVSFunction Source_filters[] = {
#ifdef AVS_WINDOWS
  { "AVISource",     BUILTIN_FUNC_PREFIX, "s+[audio]b[pixel_type]s[fourCC]s[vtrack]i[atrack]i[utf8]b", AVISource::Create, (void*) AVISource::MODE_NORMAL },
  { "AVIFileSource", BUILTIN_FUNC_PREFIX, "s+[audio]b[pixel_type]s[fourCC]s[vtrack]i[atrack]i[utf8]b", AVISource::Create, (void*) AVISource::MODE_AVIFILE },
  { "WAVSource",     BUILTIN_FUNC_PREFIX, "s+[utf8]b", AVISource::Create, (void*) AVISource::MODE_WAV },
  { "OpenDMLSource", BUILTIN_FUNC_PREFIX, "s+[audio]b[pixel_type]s[fourCC]s[vtrack]i[atrack]i[utf8]b", AVISource::Create, (void*) AVISource::MODE_OPENDML },
  { "SegmentedAVISource", BUILTIN_FUNC_PREFIX, "s+[audio]b[pixel_type]s[fourCC]s[vtrack]i[atrack]i[utf8]b", Create_SegmentedSource, (void*)0 },
  { "SegmentedDirectShowSource", BUILTIN_FUNC_PREFIX,
// args               0      1      2       3       4            5          6         7            8
                     "s+[fps]f[seek]b[audio]b[video]b[convertfps]b[seekzero]b[timeout]i[pixel_type]s",
                     Create_SegmentedSource, (void*)1 },
// args             0         1       2        3            4     5                 6            7        8             9       10          11     12
#endif






  { "MessageClip", BUILTIN_FUNC_PREFIX, "s[width]i[height]i[shrink]b[text_color]i[halo_color]i[bg_color]i", Create_MessageClip },


  { "Tone", BUILTIN_FUNC_PREFIX, "[length]f[frequency]f[samplerate]i[channels]i[type]s[level]f", Tone::Create },

  { "Version", BUILTIN_FUNC_PREFIX, "[length]i[width]i[height]i[pixel_type]s[clip]c", Create_Version },

  { NULL }
};
