#include <avisynth_c.h>
int v12_c_probe(void) {
  AVS_ScriptEnvironment *env = avs_create_script_environment(12);
  if (!env) return 0;
  int64_t full = avs_get_cpu_flags_ex(env);
  int ok = (uint32_t)full == (uint32_t)avs_get_cpu_flags(env);
  avs_get_env_property(env, AVS_AEP_CACHESIZE_L2);
  ok = ok && avs_acquire_global_lock(env, "v12-c-probe");
  avs_release_global_lock(env, "v12-c-probe");
  avs_delete_script_environment(env);
  return ok;
}
