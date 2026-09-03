#ifndef _AVS_MTGUARD_H
#define _AVS_MTGUARD_H

// #define OLD_PREFETCH

#include "internal.h"
#include <vector>
#include <memory>
#include <mutex>

class InternalEnvironment;

class FilterConstructor;
struct MTGuardChildFilter;
class MTGuard : public IClip
{
private:
  IScriptEnvironment2* Env;

	std::unique_ptr<MTGuardChildFilter[]> ChildFilters;
  size_t nThreads;
#ifndef OLD_PREFETCH
  bool mt_enabled;
#endif
  VideoInfo vi;

  std::unique_ptr<const FilterConstructor> FilterCtor;

#ifdef AVS_WINDOWS
  std::wstring CurrentDirectory;
#else
  std::string CurrentDirectory;
#endif

  const MtMode MTMode;

public:
  ~MTGuard();
#ifdef AVS_WINDOWS
  MTGuard(PClip firstChild, MtMode mtmode, std::unique_ptr<const FilterConstructor>&& funcCtor, const wchar_t* current_directory, InternalEnvironment* env);
#else
  MTGuard(PClip firstChild, MtMode mtmode, std::unique_ptr<const FilterConstructor>&& funcCtor, const char* current_directory, InternalEnvironment* env);
#endif
  void EnableMT(size_t nThreads);

  PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
  void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env);
  const VideoInfo& __stdcall GetVideoInfo();
  bool __stdcall GetParity(int n);
  int __stdcall SetCacheHints(int cachehints,int frame_range);


  static bool __stdcall IsMTGuard(const PClip& p);

#ifdef AVS_WINDOWS
  static PClip Create(MtMode mode, PClip filterInstance, std::unique_ptr<const FilterConstructor> funcCtor, const wchar_t* current_directory, InternalEnvironment* env);
#else
  static PClip Create(MtMode mode, PClip filterInstance, std::unique_ptr<const FilterConstructor> funcCtor, const char* current_directory, InternalEnvironment* env);
#endif
};

#ifdef USE_MT_GUARDEXIT
class MTGuardExit : public NonCachedGenericVideoFilter
{
private:
    MTGuard *guard = nullptr;
    const char *name;

public:
    MTGuardExit(const PClip &clip, const char *_name);
    void Activate(PClip &with_guard);

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    void __stdcall GetAudio(void* buf, int64_t start, int64_t count, IScriptEnvironment* env);
};
#endif

#endif // _AVS_MTGUARD_H
