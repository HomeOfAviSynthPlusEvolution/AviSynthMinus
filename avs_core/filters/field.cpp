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


#include "field.h"
#include "resample.h"
#include <avs/minmax.h>
#include "../core/internal.h"
#include "../convert/convert_helper.h"
#include <vector>


/**** Factory methods ****/

static AVSValue __cdecl Create_DoubleWeave(AVSValue args, void*, IScriptEnvironment* env);
static AVSValue __cdecl Create_Weave(AVSValue args, void*, IScriptEnvironment* env);
static AVSValue __cdecl Create_Pulldown(AVSValue args, void*, IScriptEnvironment* env);
static AVSValue __cdecl Create_SwapFields(AVSValue args, void*, IScriptEnvironment* env);
static AVSValue __cdecl Create_Bob(AVSValue args, void*, IScriptEnvironment* env);


/********************************************************************
***** Declare index of new filters for Avisynth's filter engine *****
********************************************************************/

extern const AVSFunction Field_filters[] = {
  { "ComplementParity", BUILTIN_FUNC_PREFIX, "c", ComplementParity::Create },
  { "AssumeTFF",        BUILTIN_FUNC_PREFIX, "c", AssumeParity::Create, (void*)true },
  { "AssumeBFF",        BUILTIN_FUNC_PREFIX, "c", AssumeParity::Create, (void*)false },
  { "AssumeFieldBased", BUILTIN_FUNC_PREFIX, "c", AssumeFieldBased::Create },
  { "AssumeFrameBased", BUILTIN_FUNC_PREFIX, "c", AssumeFrameBased::Create },




  { "SeparateFields",   BUILTIN_FUNC_PREFIX, "c", SeparateFields::Create },
  { "Weave",            BUILTIN_FUNC_PREFIX, "c", Create_Weave },
  { "DoubleWeave",      BUILTIN_FUNC_PREFIX, "c", Create_DoubleWeave },
  { "Pulldown",         BUILTIN_FUNC_PREFIX, "cii", Create_Pulldown },
  { "SelectEvery",      BUILTIN_FUNC_PREFIX, "cii*", SelectEvery::Create },
  { "SelectEven",       BUILTIN_FUNC_PREFIX, "c", SelectEvery::Create_SelectEven },
  { "SelectOdd",        BUILTIN_FUNC_PREFIX, "c", SelectEvery::Create_SelectOdd },
  { "Interleave",       BUILTIN_FUNC_PREFIX, "c+", Interleave::Create },
  { "SwapFields",       BUILTIN_FUNC_PREFIX, "c", Create_SwapFields },
  { "Bob",              BUILTIN_FUNC_PREFIX, "c[b]f[c]f[height]i", Create_Bob },
  { "SelectRangeEvery", BUILTIN_FUNC_PREFIX, "c[every]i[length]i[offset]i[audio]b", SelectRangeEvery::Create},
  { NULL }
};





/*********************************
 *******   SeparateColumns  ******
 *********************************/

SeparateFields::SeparateFields(PClip _child, IScriptEnvironment* env)
 : NonCachedGenericVideoFilter(_child)
{
  if (vi.height & 1)
    env->ThrowError("SeparateFields: height must be even");
  if (vi.Is420() && vi.height & 3)
    env->ThrowError("SeparateFields: YUV420 height must be multiple of 4");
  vi.height >>= 1;
  vi.MulDivFPS(2, 1);
  vi.num_frames *= 2;

  if (vi.num_frames < 0)
    env->ThrowError("SeparateFields: Maximum number of frames exceeded.");

  vi.SetFieldBased(true);
}


PVideoFrame SeparateFields::GetFrame(int n, IScriptEnvironment* env)
{
#ifdef CACHE_GROWTH_INFINITELY_TEST
  // FIXME: debug for Issue #270
  // See other occurencies of this define
  // When filter is combined with non-SeparateFielded frames
  // the cache can grow infinitely, behaves like a memory leak.
  // Tried putting n or (n-1) or (n*2) instead of n >> 1 then the problem does not occur.
  _RPT2(0, "SeparateFields::GetFrame before %d, >>1: %d\n", n, n >> 1);
#endif
  PVideoFrame frame = child->GetFrame(n>>1, env);
#ifdef CACHE_GROWTH_INFINITELY_TEST
  _RPT2(0, "SeparateFields::GetFrame after %d, >>1: %d\n", n, n >> 1);
#endif
  if (vi.IsPlanar()) {
    const bool topfield = GetParity(n);

    int plane0 = vi.IsRGB() ? PLANAR_G : PLANAR_Y;
    int plane1 = vi.IsRGB() ? PLANAR_B : PLANAR_U;
    const int Ypitch   = frame->GetPitch(plane0);
    const int UVpitch  = frame->GetPitch(plane1);
    const int UVoffset = !topfield ? UVpitch : 0;
    const int Yoffset = !topfield ? Ypitch : 0;

    if (vi.NumComponents() == 4) {
      int Aoffset = !topfield ? frame->GetPitch(PLANAR_A) : 0;

      return env->SubframePlanarA(frame, Yoffset, frame->GetPitch() * 2, frame->GetRowSize(), frame->GetHeight() >> 1,
        UVoffset, UVoffset, frame->GetPitch(PLANAR_U) * 2, Aoffset);
    }
    else {
      return env->SubframePlanar(frame, Yoffset, frame->GetPitch() * 2, frame->GetRowSize(), frame->GetHeight() >> 1,
        UVoffset, UVoffset, frame->GetPitch(PLANAR_U) * 2);
    }
  }
  return env->Subframe(frame,(GetParity(n) ^ vi.IsYUY2()) ? frame->GetPitch() : 0,
                         frame->GetPitch()*2, frame->GetRowSize(), frame->GetHeight()>>1);
}


AVSValue __cdecl SeparateFields::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  PClip clip = args[0].AsClip();
  if (clip->GetVideoInfo().IsFieldBased())
    env->ThrowError("SeparateFields: SeparateFields should be applied on frame-based material: use AssumeFrameBased() beforehand");

  return new SeparateFields(clip, env);
}







/******************************
 *******   Interleave   *******
 ******************************/

Interleave::Interleave(const std::vector<PClip>&& _child_array, IScriptEnvironment* env)
  : num_children((int)_child_array.size()), child_array(std::move(_child_array))
{
  vi = child_array[0]->GetVideoInfo();
  vi.MulDivFPS(num_children, 1);
  vi.num_frames = (vi.num_frames - 1) * num_children + 1;
  child_devs = GetDeviceTypes(child_array[0]);
  for (int i=1; i<num_children; ++i)
  {
    const VideoInfo& vi2 = child_array[i]->GetVideoInfo();
    if (vi.width != vi2.width || vi.height != vi2.height)
      env->ThrowError("Interleave: videos must be of the same size.");
    if (!vi.IsSameColorspace(vi2))
      env->ThrowError("Interleave: video formats don't match");

    vi.num_frames = max(vi.num_frames, (vi2.num_frames - 1) * num_children + i + 1);

    child_devs &= GetDeviceTypes(child_array[i]);
    if (child_devs == 0)
      env->ThrowError("Interleave: device types don't match");
  }
  if (vi.num_frames < 0)
    env->ThrowError("Interleave: Maximum number of frames exceeded.");

}

int __stdcall Interleave::SetCacheHints(int cachehints,int frame_range)
{
  AVS_UNUSED(frame_range);
  switch (cachehints)
  {
  case CACHE_DONT_CACHE_ME:
    return 1;
  case CACHE_GET_MTMODE:
    return MT_NICE_FILTER;
  case CACHE_GET_DEV_TYPE:
    return child_devs;
  default:
    return 0;
  }
}

AVSValue __cdecl Interleave::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  args = args[0];
  const int num_args = args.ArraySize();
  if (num_args == 1)
    return args[0];

  std::vector<PClip> children(num_args);

  for (int i = 0; i < (int)children.size(); ++i)
    children[i] = args[i].AsClip();

  return new Interleave(std::move(children), env);
}






/*********************************
 ********   SelectEvery    *******
 *********************************/


SelectEvery::SelectEvery(PClip _child, int _every, int _from, IScriptEnvironment* env)
: NonCachedGenericVideoFilter(_child), every(_every), from(_from)
{
  if (_every <= 0)
    env->ThrowError("Parameter 'every' of SelectEvery must be greater than zero.");
  if (_from < 0 || _from >= _every || _from >= _child->GetVideoInfo().num_frames)
    env->ThrowError("Parameter 'from' of SelectEvery must be less than 'every' and the number of frames in the source clip.");

  vi.MulDivFPS(1, every);
  vi.num_frames = (vi.num_frames-1-from) / every + 1;
}


AVSValue __cdecl SelectEvery::Create(AVSValue args, void*, IScriptEnvironment* env)
{
  const int num_vals = args[2].ArraySize();
  if (num_vals <= 1)
  return new SelectEvery(args[0].AsClip(), args[1].AsInt(), num_vals>0 ? args[2][0].AsInt() : 0, env);
  else {
    std::vector<PClip> children(num_vals);

    for (int i = 0; i < (int)children.size(); ++i)
      children[i] = new SelectEvery(args[0].AsClip(), args[1].AsInt(), args[2][i].AsInt(), env);

    return new Interleave(std::move(children), env);
  }
}








/**************************************
 ********   DoubleWeaveFields   *******
 *************************************/

DoubleWeaveFields::DoubleWeaveFields(PClip _child)
  : GenericVideoFilter(_child)
{
  vi.height *= 2;
  vi.SetFieldBased(false);
}


void copy_field(const PVideoFrame& dst, const PVideoFrame& src, bool yuv, bool planarRGB, bool parity, IScriptEnvironment* env)
{
  bool noTopBottom = yuv || planarRGB;

  int plane1 = planarRGB ? PLANAR_B : PLANAR_U;
  int plane2 = planarRGB ? PLANAR_R : PLANAR_V;

  const int add_pitch = dst->GetPitch() * (parity ^ noTopBottom);
  const int add_pitchUV = dst->GetPitch(plane1) * (parity ^ noTopBottom);
  const int add_pitchA = dst->GetPitch(PLANAR_A) * (parity ^ noTopBottom);

  env->BitBlt(dst->GetWritePtr()         + add_pitch, dst->GetPitch()*2,
    src->GetReadPtr(), src->GetPitch(),
    src->GetRowSize(), src->GetHeight());

  env->BitBlt(dst->GetWritePtr(plane1) + add_pitchUV, dst->GetPitch(plane1)*2,
    src->GetReadPtr(plane1), src->GetPitch(plane1),
    src->GetRowSize(plane1), src->GetHeight(plane1));

  env->BitBlt(dst->GetWritePtr(plane2) + add_pitchUV, dst->GetPitch(plane2)*2,
    src->GetReadPtr(plane2), src->GetPitch(plane2),
    src->GetRowSize(plane2), src->GetHeight(plane2));

  env->BitBlt(dst->GetWritePtr(PLANAR_A) + add_pitchA, dst->GetPitch(PLANAR_A)*2,
    src->GetReadPtr(PLANAR_A), src->GetPitch(PLANAR_A),
    src->GetRowSize(PLANAR_A), src->GetHeight(PLANAR_A));
}


PVideoFrame DoubleWeaveFields::GetFrame(int n, IScriptEnvironment* env)
{
  PVideoFrame a = child->GetFrame(n, env);
  const int last_frame = child->GetVideoInfo().num_frames - 1;
  PVideoFrame b = child->GetFrame(min(n + 1, last_frame), env);

  PVideoFrame result = env->NewVideoFrameP(vi, &a);

  const bool parity = child->GetParity(n);

  copy_field(result, a, vi.IsYUV() || vi.IsYUVA(), vi.IsPlanarRGB() || vi.IsPlanarRGBA(), parity, env);
  copy_field(result, b, vi.IsYUV() || vi.IsYUVA(), vi.IsPlanarRGB() || vi.IsPlanarRGBA(), !parity, env);

  return result;
}



/**************************************
 ********   DoubleWeaveFrames   *******
 *************************************/

DoubleWeaveFrames::DoubleWeaveFrames(PClip _child)
  : GenericVideoFilter(_child)
{
  vi.num_frames *= 2;
  if (vi.num_frames < 0)
    vi.num_frames = 0x7FFFFFFF; // MAXINT

  vi.MulDivFPS(2, 1);
}

void copy_alternate_lines(const PVideoFrame& dst, const PVideoFrame& src, bool yuv, bool planarRGB, bool parity, IScriptEnvironment* env)
{
  bool noTopBottom = yuv || planarRGB;

  int plane1 = planarRGB ? PLANAR_B : PLANAR_U;
  int plane2 = planarRGB ? PLANAR_R : PLANAR_V;

  const int src_add_pitch = src->GetPitch()         * (parity ^ noTopBottom);
  const int src_add_pitchUV = src->GetPitch(plane1) * (parity ^ noTopBottom);
  const int src_add_pitchA = src->GetPitch(PLANAR_A) * (parity ^ noTopBottom);

  const int dst_add_pitch = dst->GetPitch()         * (parity ^ noTopBottom);
  const int dst_add_pitchUV = dst->GetPitch(plane1) * (parity ^ noTopBottom);
  const int dst_add_pitchA = dst->GetPitch(PLANAR_A) * (parity ^ noTopBottom);

  env->BitBlt(dst->GetWritePtr()         + dst_add_pitch, dst->GetPitch()*2,
    src->GetReadPtr()          + src_add_pitch, src->GetPitch()*2,
    src->GetRowSize(), src->GetHeight()>>1);

  env->BitBlt(dst->GetWritePtr(plane1) + dst_add_pitchUV, dst->GetPitch(plane1)*2,
    src->GetReadPtr(plane1)  + src_add_pitchUV, src->GetPitch(plane1)*2,
    src->GetRowSize(plane1), src->GetHeight(plane1)>>1);

  env->BitBlt(dst->GetWritePtr(plane2) + dst_add_pitchUV, dst->GetPitch(plane2)*2,
    src->GetReadPtr(plane2)  + src_add_pitchUV, src->GetPitch(plane2)*2,
    src->GetRowSize(plane2), src->GetHeight(plane2)>>1);

  env->BitBlt(dst->GetWritePtr(PLANAR_A) + dst_add_pitchA, dst->GetPitch(PLANAR_A)*2,
    src->GetReadPtr(PLANAR_A)  + src_add_pitchA, src->GetPitch(PLANAR_A)*2,
    src->GetRowSize(PLANAR_A), src->GetHeight(PLANAR_A)>>1);
}


PVideoFrame DoubleWeaveFrames::GetFrame(int n, IScriptEnvironment* env)
{
  if (!(n&1))
  {
    return child->GetFrame(n>>1, env);
  }
  else {
    PVideoFrame a = child->GetFrame(n>>1, env);
    const int last_frame = child->GetVideoInfo().num_frames - 1;
    PVideoFrame b = child->GetFrame(min((n + 1) >> 1, last_frame), env);
    bool parity = this->GetParity(n);

    if (a->IsWritable()) {
      copy_alternate_lines(a, b,  vi.IsYUV() || vi.IsYUVA(), vi.IsPlanarRGB() || vi.IsPlanarRGBA(), !parity, env);
      return a;
    }
    else if (b->IsWritable()) {
      copy_alternate_lines(b, a,  vi.IsYUV() || vi.IsYUVA(), vi.IsPlanarRGB() || vi.IsPlanarRGBA(), parity, env);
      return b;
    }
    else {
      PVideoFrame result = env->NewVideoFrameP(vi, &a);
      copy_alternate_lines(result, a, vi.IsYUV() || vi.IsYUVA(), vi.IsPlanarRGB() || vi.IsPlanarRGBA(), parity, env);
      copy_alternate_lines(result, b, vi.IsYUV() || vi.IsYUVA(), vi.IsPlanarRGB() || vi.IsPlanarRGBA(), !parity, env);
      return result;
    }
  }
}





/*******************************
 ********   Bob Filter   *******
 *******************************/

Fieldwise::Fieldwise(PClip _child1, PClip _child2)
: NonCachedGenericVideoFilter(_child1), child2(_child2)
  { vi.SetFieldBased(false); } // Make FrameBased, leave IT_BFF and IT_TFF alone


PVideoFrame __stdcall Fieldwise::GetFrame(int n, IScriptEnvironment* env)
{
  return (child->GetParity(n) ? child2 : child)->GetFrame(n, env);
}


bool __stdcall Fieldwise::GetParity(int n)
{
  return child->GetParity(n) ^ (n&1); // ^ = XOR
}







/************************************
 ********   Factory Methods   *******
 ***********************************/

static AVSValue __cdecl Create_DoubleWeave(AVSValue args, void*, IScriptEnvironment* env)
{
  AVS_UNUSED(env);
  PClip clip = args[0].AsClip();
  if (clip->GetVideoInfo().IsFieldBased())
    return new DoubleWeaveFields(clip);
  else
    return new DoubleWeaveFrames(clip);
}


static AVSValue __cdecl Create_Weave(AVSValue args, void*, IScriptEnvironment* env)
{
  PClip clip = args[0].AsClip();
  if (!clip->GetVideoInfo().IsFieldBased())
    env->ThrowError("Weave: Weave should be applied on field-based material: use AssumeFieldBased() beforehand");
  return new SelectEvery(Create_DoubleWeave(args, 0, env).AsClip(), 2, 0, env);
}


static AVSValue __cdecl Create_Pulldown(AVSValue args, void*, IScriptEnvironment* env)
{
  PClip clip = args[0].AsClip();
  std::vector<PClip> children(2);
  children[0] = new SelectEvery(clip, 5, args[1].AsInt() % 5, env);
  children[1] = new SelectEvery(clip, 5, args[2].AsInt() % 5, env);
  return new AssumeFrameBased(new Interleave(std::move(children), env));
}


static AVSValue __cdecl Create_SwapFields(AVSValue args, void*, IScriptEnvironment* env)
{
  return new SelectEvery(new DoubleWeaveFields(new ComplementParity(
    new SeparateFields(args[0].AsClip(), env))), 2, 0, env);
}


static AVSValue __cdecl Create_Bob(AVSValue args, void*, IScriptEnvironment* env)
{
  PClip clip = args[0].AsClip();
  if (!clip->GetVideoInfo().IsFieldBased())
    clip = new SeparateFields(clip, env);

  const VideoInfo& vi = clip->GetVideoInfo();

  bool preserve_center = true; // default Avisynth
  int chroma_placement = ChromaLocation_e::AVS_CHROMA_UNUSED; // default

  const double b = args[1].AsDblDef(1./3.);
  const double c = args[2].AsDblDef(1./3.);
  const int new_height = args[3].AsInt(vi.height*2);
  const vc_filter_spec filter{VC_BICUBIC, {b, c}};
  return new Fieldwise(FilteredResize::CreateResizeV(clip, -0.25, vi.height,
                                           new_height, true, filter, preserve_center, chroma_placement, env),
                       FilteredResize::CreateResizeV(clip, +0.25, vi.height,
                                           new_height, true, filter, preserve_center, chroma_placement, env));
}


SelectRangeEvery::SelectRangeEvery(PClip _child, int _every, int _length, int _offset, bool _audio, IScriptEnvironment* env)
: NonCachedGenericVideoFilter(_child), audio(_audio), achild(_child)
{
  const int64_t num_audio_samples = vi.num_audio_samples;

  AVSValue trimargs[3] = { _child, _offset, 0};
  PClip c = env->Invoke("Trim",AVSValue(trimargs,3)).AsClip();
  child = c;
  vi = c->GetVideoInfo();

  every = clamp(_every,1,vi.num_frames);
  length = clamp(_length,1,every);

  const int n = vi.num_frames;
  vi.num_frames = (n/every)*length+(n%every<length?n%every:length);

  if (audio && vi.HasAudio()) {
    vi.num_audio_samples = vi.AudioSamplesFromFrames(vi.num_frames);
  } else {
    vi.num_audio_samples = num_audio_samples; // Undo Trim's work!
  }
}


PVideoFrame __stdcall SelectRangeEvery::GetFrame(int n, IScriptEnvironment* env)
{
  return child->GetFrame((n/length)*every+(n%length), env);
}


bool __stdcall SelectRangeEvery::GetParity(int n)
{
  return child->GetParity((n/length)*every+(n%length));
}


void __stdcall SelectRangeEvery::GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env)
{
  if (!audio) {
  // Use original unTrim'd child
    achild->GetAudio(buf, start, count, env);
    return;
  }

  int64_t samples_filled = 0;
  BYTE* samples = (BYTE*)buf;
  const int bps = vi.BytesPerAudioSample();
  int startframe = vi.FramesFromAudioSamples(start);
  int64_t general_offset = start - vi.AudioSamplesFromFrames(startframe);  // General compensation for startframe rounding.

  while (samples_filled < count) {
    const int iteration = startframe / length;                    // Which iteration is this.
    const int iteration_into = startframe % length;               // How far, in frames are we into this iteration.
    const int iteration_left = length - iteration_into;           // How many frames is left of this iteration.

    const int64_t iteration_left_samples = vi.AudioSamplesFromFrames(iteration_left);
    // This is the number of samples we can get without either having to skip, or being finished.
    const int64_t getsamples = min(iteration_left_samples, count-samples_filled);
    const int64_t start_offset = vi.AudioSamplesFromFrames(iteration * every + iteration_into) + general_offset;

    child->GetAudio(&samples[samples_filled*bps], start_offset, getsamples, env);
    samples_filled += getsamples;
    startframe = (iteration+1) * length;
    general_offset = 0; // On the following loops, general offset should be 0, as we are either skipping.
  }
}

AVSValue __cdecl SelectRangeEvery::Create(AVSValue args, void* user_data, IScriptEnvironment* env) {
  AVS_UNUSED(user_data);
  return new SelectRangeEvery(args[0].AsClip(), args[1].AsInt(1500), args[2].AsInt(50), args[3].AsInt(0), args[4].AsBool(true), env);
}
