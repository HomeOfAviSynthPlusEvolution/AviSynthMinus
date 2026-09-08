// Compile the existing C API caller as C, not as C++ or the CX protocol.
#include <avisynth_c.h>

int cx_test_c_acquire(AVS_ScriptEnvironment *env, const char *name) {
  return avs_acquire_global_lock(env, name);
}

void cx_test_c_release(AVS_ScriptEnvironment *env, const char *name) {
  avs_release_global_lock(env, name);
}
