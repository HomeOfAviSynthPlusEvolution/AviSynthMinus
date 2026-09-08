#include <avs/cx/feature_keys.h>
#include <avs/cx/registry.h>
static avs_cx_status AVS_CX_CALL answer(void *p, const avs_cx_call_context_v1 *c,
                                        const avs_cx_value_v1 *a, uint32_t n, avs_cx_value_v1 *r,
                                        avs_cx_error_v1 *e) {
  (void)p;
  (void)c;
  (void)a;
  (void)n;
  (void)e;
  r->type = AVS_CX_VALUE_INT;
  r->value.integer = 4242;
  return AVS_CX_STATUS_OK;
}
AVS_CX_EXPORT avs_cx_status AVS_CX_CALL AvisynthPluginInitCX1(const avs_cx_host_v1 *host) {
  const void *table = 0;
  avs_cx_status status =
      host->query_feature(host->host_context, AVS_CX_FEATURE_REGISTRY, AVS_CX_ABI_VERSION_1,
                          sizeof(avs_cx_registry_feature_v1), &table);
  const avs_cx_registry_feature_v1 *registry = (const avs_cx_registry_feature_v1 *)table;
  const avs_cx_string_view_v1 name = {"CXPureCAnswer", 13, 0}, parameters = {"", 0, 0};
  if (status != AVS_CX_STATUS_OK)
    return status;
  return registry->register_function(registry->context, &name, &parameters, &answer, 0);
}
