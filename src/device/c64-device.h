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
    // A verified second address of the same physical unit. Some C64 Ultimate
    // firmware versions accept control on one interface but source UDP from
    // another.
    char peer_host[C64_DEVICE_HOST_MAX];
    char dns_server_ip[C64_DEVICE_HOST_MAX];
    uint32_t video_port;
    uint32_t audio_port;
    uint32_t control_port;
} c64_device_t;

bool c64_device_registry_init(void);
void c64_device_registry_cleanup(void);
/* Returned pointers refer to a per-thread snapshot and remain valid until the
 * next registry get call on that thread. */
const c64_device_t *c64_device_registry_get(const char *id);
const c64_device_t *c64_device_registry_get_at(size_t index);
const c64_device_t *c64_device_registry_find_by_host(const char *host);
size_t c64_device_registry_count(void);
bool c64_device_registry_upsert(const c64_device_t *device);
/* Updates discovered addresses while preserving an existing user's profile. */
bool c64_device_registry_upsert_discovered(const c64_device_t *device);
bool c64_device_registry_delete(const char *id);
void c64_device_registry_populate_list(obs_property_t *property);
bool c64_device_id_from_host(char *out, size_t out_size, const char *unique_id, const char *host);
/* True when the profile is keyed by a hardware unique_id, i.e. its id is not
 * simply derived from its host or peer address (legacy migration, manual Save
 * of a new host, or a password-protected device discovered without its ID). */
bool c64_device_profile_is_identified(const c64_device_t *device);

/* First-load compatibility migration.  Password handling is intentionally
 * confined to OBS settings; this function never writes it to an INI file. */
bool c64_device_registry_migrate_legacy(obs_data_t *settings);
bool c64_device_registry_apply_selected(obs_data_t *settings);
void c64_device_password_key(char *out, size_t out_size, const char *id);

/* A multi-homed Ultimate can accept control traffic on Wi-Fi even though its
 * A/V streams only leave the wired LAN port. Promote a verified alternate
 * address only after a completed start has produced no video for the grace
 * interval, and never retry that promotion automatically. */
/* A started stream that has produced no video while the ingest filter keeps
 * rejecting one sender is checked once per sender (re-checked after
 * recheck_ns): that sender may be the selected device's wired port while the
 * source is configured with its Wi-Fi address. */
bool c64_device_sender_check_due(uint32_t rejected_ip, uint32_t expected_ip, bool alt_set, uint32_t alt_ip,
                                 bool learned_set, uint32_t learned_ip, uint32_t last_checked_ip,
                                 uint64_t last_checked_ns, uint64_t no_video_since_ns, uint64_t now_ns,
                                 uint64_t grace_ns, uint64_t recheck_ns);

bool c64_device_stream_failover_needed(bool alternate_available, bool already_attempted, uint64_t no_video_since_ns,
                                       uint64_t now_ns, uint64_t grace_ns);
