// C ABI environment forwarding; compiled only in the plugin.
void __stdcall PushContext(int level = 0) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PUSHCONTEXT;
  q.integers[0] = static_cast<int64_t>(level);
  Dispatch(q);
}
void __stdcall PopContext() override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_POPCONTEXT;
  Dispatch(q);
}
int __stdcall SetMemoryMax(int mem) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_SETMEMORYMAX;
  q.integers[0] = static_cast<int64_t>(mem);
  return (int)Dispatch(q).integer;
}
int __stdcall SetWorkingDir(const char *newdir) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_SETWORKINGDIR;
  q.pointers[0] = newdir;
  return (int)Dispatch(q).integer;
}
bool __stdcall PlanarChromaAlignment(PlanarChromaAlignmentMode key) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PLANARCHROMAALIGNMENT;
  q.integers[0] = static_cast<int64_t>(key);
  return (bool)Dispatch(q).integer;
}
int __stdcall propNumKeys(const AVSMap *map) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPNUMKEYS;
  q.pointers[0] = map;
  return (int)Dispatch(q).integer;
}
const char *__stdcall propGetKey(const AVSMap *map, int index) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETKEY;
  q.pointers[0] = map;
  q.integers[0] = static_cast<int64_t>(index);
  return (const char *)Dispatch(q).pointer;
}
int __stdcall propNumElements(const AVSMap *map, const char *key) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPNUMELEMENTS;
  q.pointers[0] = map;
  q.pointers[1] = key;
  return (int)Dispatch(q).integer;
}
char __stdcall propGetType(const AVSMap *map, const char *key) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETTYPE;
  q.pointers[0] = map;
  q.pointers[1] = key;
  return (char)Dispatch(q).integer;
}
int64_t __stdcall propGetInt(const AVSMap *map, const char *key, int index, int *error) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETINT;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.integers[0] = static_cast<int64_t>(index);
  q.pointers[2] = error;
  return (int64_t)Dispatch(q).integer;
}
double __stdcall propGetFloat(const AVSMap *map, const char *key, int index, int *error) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETFLOAT;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.integers[0] = static_cast<int64_t>(index);
  q.pointers[2] = error;
  return (double)Dispatch(q).real;
}
const char *__stdcall propGetData(const AVSMap *map, const char *key, int index,
                                  int *error) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETDATA;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.integers[0] = static_cast<int64_t>(index);
  q.pointers[2] = error;
  return (const char *)Dispatch(q).pointer;
}
int __stdcall propGetDataSize(const AVSMap *map, const char *key, int index, int *error) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETDATASIZE;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.integers[0] = static_cast<int64_t>(index);
  q.pointers[2] = error;
  return (int)Dispatch(q).integer;
}
int __stdcall propDeleteKey(AVSMap *map, const char *key) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPDELETEKEY;
  q.pointers[0] = map;
  q.pointers[1] = key;
  return (int)Dispatch(q).integer;
}
int __stdcall propSetInt(AVSMap *map, const char *key, int64_t i, int append) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPSETINT;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.integers[0] = static_cast<int64_t>(i);
  q.integers[1] = static_cast<int64_t>(append);
  return (int)Dispatch(q).integer;
}
int __stdcall propSetFloat(AVSMap *map, const char *key, double d, int append) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPSETFLOAT;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.real = d;
  q.integers[0] = static_cast<int64_t>(append);
  return (int)Dispatch(q).integer;
}
int __stdcall propSetData(AVSMap *map, const char *key, const char *d, int length,
                          int append) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPSETDATA;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.pointers[2] = d;
  q.integers[0] = static_cast<int64_t>(length);
  q.integers[1] = static_cast<int64_t>(append);
  return (int)Dispatch(q).integer;
}
const int64_t *__stdcall propGetIntArray(const AVSMap *map, const char *key, int *error) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETINTARRAY;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.pointers[2] = error;
  return (const int64_t *)Dispatch(q).pointer;
}
const double *__stdcall propGetFloatArray(const AVSMap *map, const char *key, int *error) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETFLOATARRAY;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.pointers[2] = error;
  return (const double *)Dispatch(q).pointer;
}
int __stdcall propSetIntArray(AVSMap *map, const char *key, const int64_t *i, int size) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPSETINTARRAY;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.pointers[2] = i;
  q.integers[0] = static_cast<int64_t>(size);
  return (int)Dispatch(q).integer;
}
int __stdcall propSetFloatArray(AVSMap *map, const char *key, const double *d, int size) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPSETFLOATARRAY;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.pointers[2] = d;
  q.integers[0] = static_cast<int64_t>(size);
  return (int)Dispatch(q).integer;
}
AVSMap *__stdcall createMap() override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_CREATEMAP;
  return (AVSMap *)Dispatch(q).pointer;
}
void __stdcall freeMap(AVSMap *map) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_FREEMAP;
  q.pointers[0] = map;
  Dispatch(q);
}
void __stdcall clearMap(AVSMap *map) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_CLEARMAP;
  q.pointers[0] = map;
  Dispatch(q);
}
void *__stdcall Allocate(size_t nBytes, size_t alignment, AvsAllocType type) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_ALLOCATE;
  q.integers[0] = static_cast<int64_t>(nBytes);
  q.integers[1] = static_cast<int64_t>(alignment);
  q.integers[2] = static_cast<int64_t>(type);
  return (void *)Dispatch(q).pointer;
}
void __stdcall Free(void *ptr) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_FREE;
  q.pointers[0] = ptr;
  Dispatch(q);
}
int __stdcall propGetIntSaturated(const AVSMap *map, const char *key, int index,
                                  int *error) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETINTSATURATED;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.integers[0] = static_cast<int64_t>(index);
  q.pointers[2] = error;
  return (int)Dispatch(q).integer;
}
float __stdcall propGetFloatSaturated(const AVSMap *map, const char *key, int index,
                                      int *error) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETFLOATSATURATED;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.integers[0] = static_cast<int64_t>(index);
  q.pointers[2] = error;
  return (float)Dispatch(q).real;
}
int __stdcall propGetDataTypeHint(const AVSMap *map, const char *key, int index,
                                  int *error) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPGETDATATYPEHINT;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.integers[0] = static_cast<int64_t>(index);
  q.pointers[2] = error;
  return (int)Dispatch(q).integer;
}
int __stdcall propSetDataH(AVSMap *map, const char *key, const char *d, int length, int type,
                           int append) override {
  avs_cx_sdk_request_v1 q{};
  q.operation = AVS_CX_SDK_PROPSETDATAH;
  q.pointers[0] = map;
  q.pointers[1] = key;
  q.pointers[2] = d;
  q.integers[0] = static_cast<int64_t>(length);
  q.integers[1] = static_cast<int64_t>(type);
  q.integers[2] = static_cast<int64_t>(append);
  return (int)Dispatch(q).integer;
}
