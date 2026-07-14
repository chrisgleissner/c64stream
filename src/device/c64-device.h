/* C64 Stream device registry.  Device profiles deliberately contain network
 * settings only; passwords remain in OBS source settings. */
#pragma once

#include <obs-module.h>
#include <stdbool.h>
#include <stdint.h>

#define C64_DEVICE_ID_MAX 64
#define C64_DEVICE_NAME_MAX 64
#define C64_DEVICE_HOST_MAX 64
#define C64_DEVICE_MAX 64

typedef struct c64_device {
    char id[C64_DEVICE_ID_MAX];
    char name[C64_DEVICE_NAME_MAX];
    char host[C64_DEVICE_HOST_MAX];
    char dns_server_ip[C64_DEVICE_HOST_MAX];
    uint32_t video_port;
    uint32_t audio_port;
    uint32_t control_port;
} c64_device_t;

bool c64_device_registry_init(void);
void c64_device_registry_cleanup(void);
const c64_device_t *c64_device_registry_get(const char *id);
const c64_device_t *c64_device_registry_get_at(size_t index);
size_t c64_device_registry_count(void);
bool c64_device_registry_upsert(const c64_device_t *device);
bool c64_device_registry_delete(const char *id);
void c64_device_registry_populate_list(obs_property_t *property);
bool c64_device_id_from_host(char *out, size_t out_size, const char *unique_id, const char *host);

/* First-load compatibility migration.  Password handling is intentionally
 * confined to OBS settings; this function never writes it to an INI file. */
bool c64_device_registry_migrate_legacy(obs_data_t *settings);
bool c64_device_registry_apply_selected(obs_data_t *settings);
void c64_device_password_key(char *out, size_t out_size, const char *id);
