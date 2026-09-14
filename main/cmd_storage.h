#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* `storage df|ls|bench` for the LittleFS volume `partition_label` mounted at `mount_path`.
 * Both strings must outlive the console. */
void register_storage(const char *mount_path, const char *partition_label);

#ifdef __cplusplus
}
#endif
