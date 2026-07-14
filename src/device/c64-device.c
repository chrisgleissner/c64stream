#include "c64-device.h"
#include "c64-file.h"
#include "c64-logging.h"

#include <util/platform.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEVICE_LOG_PREFIX "DEVICE:"

static c64_device_t devices[C64_DEVICE_MAX];
static size_t device_count;
static bool initialized;

static bool valid_id(const char *id);
static bool read_entry(const char *key, const char *value, void *opaque);

static bool load_device_file(const char *path)
{
    c64_device_t device = {0};
    if (!c64_ini_foreach(path, read_entry, &device) || !valid_id(device.id) || !device.host[0] ||
        device_count == C64_DEVICE_MAX) {
        return false;
    }
    devices[device_count++] = device;
    return true;
}

static bool valid_id(const char *id)
{
    return id && id[0] && !strstr(id, "..") && !strchr(id, '/') && !strchr(id, '\\');
}

static bool device_path(char *out, size_t size, const char *id)
{
    char directory[512];
    return valid_id(id) && c64_get_user_dir(C64_USER_DIR_SETTINGS, directory, sizeof(directory)) &&
           snprintf(out, size, "%s/device-%s.ini", directory, id) > 0;
}

void c64_device_password_key(char *out, size_t out_size, const char *id)
{
    if (!out || !out_size)
        return;
    snprintf(out, out_size, "device_password.%s", valid_id(id) ? id : "");
}

bool c64_device_id_from_host(char *out, size_t out_size, const char *unique_id, const char *host)
{
    const char *source = (unique_id && unique_id[0]) ? unique_id : host;
    if (!out || !out_size || !source || !source[0])
        return false;
    size_t n = 0;
    for (; *source && n + 1 < out_size; source++) {
        unsigned char ch = (unsigned char)*source;
        if (isalnum(ch))
            out[n++] = (char)tolower(ch);
        else if (n && out[n - 1] != '-')
            out[n++] = '-';
    }
    while (n && out[n - 1] == '-')
        n--;
    out[n] = '\0';
    return n != 0;
}

static bool read_entry(const char *key, const char *value, void *opaque)
{
    c64_device_t *device = opaque;
    if (!strcmp(key, "id"))
        snprintf(device->id, sizeof(device->id), "%s", value);
    else if (!strcmp(key, "name"))
        snprintf(device->name, sizeof(device->name), "%s", value);
    else if (!strcmp(key, "host"))
        snprintf(device->host, sizeof(device->host), "%s", value);
    else if (!strcmp(key, "dns_server_ip"))
        snprintf(device->dns_server_ip, sizeof(device->dns_server_ip), "%s", value);
    else if (!strcmp(key, "video_port"))
        device->video_port = (uint32_t)strtoul(value, NULL, 10);
    else if (!strcmp(key, "audio_port"))
        device->audio_port = (uint32_t)strtoul(value, NULL, 10);
    else if (!strcmp(key, "control_port"))
        device->control_port = (uint32_t)strtoul(value, NULL, 10);
    return true;
}

static bool save_device(const c64_device_t *device)
{
    char path[640];
    if (!device_path(path, sizeof(path), device->id))
        return false;
    FILE *file = fopen(path, "w");
    if (!file)
        return false;
    /* Passwords must never appear in these network-only files. */
    int result = fprintf(file,
                         "id=%s\nname=%s\nhost=%s\ndns_server_ip=%s\nvideo_port=%u\naudio_port=%u\ncontrol_port=%u\n",
                         device->id, device->name, device->host, device->dns_server_ip, device->video_port,
                         device->audio_port, device->control_port);
    fclose(file);
    return result >= 0;
}

bool c64_device_registry_init(void)
{
    if (initialized)
        return true;
    char directory[512];
    if (!c64_get_user_dir(C64_USER_DIR_SETTINGS, directory, sizeof(directory)))
        return false;
    initialized = true;
    os_dir_t *dir = os_opendir(directory);
    if (!dir)
        return true;
    struct os_dirent *entry;
    while ((entry = os_readdir(dir)) != NULL) {
        if (entry->directory)
            continue;
        if (strncmp(entry->d_name, "device-", 7) || !strstr(entry->d_name, ".ini"))
            continue;
        char path[640];
        if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) > 0)
            load_device_file(path);
    }
    os_closedir(dir);
    return true;
}
void c64_device_registry_cleanup(void)
{
    memset(devices, 0, sizeof(devices));
    device_count = 0;
    initialized = false;
}

const c64_device_t *c64_device_registry_get(const char *id)
{
    for (size_t i = 0; i < device_count; i++)
        if (!strcmp(devices[i].id, id))
            return &devices[i];
    return NULL;
}

const c64_device_t *c64_device_registry_get_at(size_t index)
{
    return index < device_count ? &devices[index] : NULL;
}

size_t c64_device_registry_count(void)
{
    return device_count;
}

bool c64_device_registry_upsert(const c64_device_t *device)
{
    if (!initialized)
        c64_device_registry_init();
    if (!device || !valid_id(device->id) || !device->host[0])
        return false;
    size_t index = device_count;
    for (size_t i = 0; i < device_count; i++)
        if (!strcmp(devices[i].id, device->id)) {
            index = i;
            break;
        }
    if (index == device_count && device_count == C64_DEVICE_MAX)
        return false;
    c64_device_t copy = *device;
    if (!copy.name[0])
        snprintf(copy.name, sizeof(copy.name), "%s", copy.host);
    if (!save_device(&copy))
        return false;
    devices[index] = copy;
    if (index == device_count)
        device_count++;
    return true;
}

bool c64_device_registry_delete(const char *id)
{
    char path[640];
    size_t index;
    for (index = 0; index < device_count && strcmp(devices[index].id, id); index++) {
    }
    if (index == device_count || !device_path(path, sizeof(path), id))
        return false;
    if (remove(path) != 0)
        return false;
    memmove(&devices[index], &devices[index + 1], (device_count - index - 1) * sizeof(devices[0]));
    device_count--;
    return true;
}

void c64_device_registry_populate_list(obs_property_t *property)
{
    if (!property)
        return;
    obs_property_list_clear(property);
    for (size_t i = 0; i < device_count; i++)
        obs_property_list_add_string(property, devices[i].name, devices[i].id);
}

bool c64_device_registry_migrate_legacy(obs_data_t *settings)
{
    if (!settings || device_count)
        return false;
    const char *host = obs_data_get_string(settings, "c64_host");
    if (!host || !host[0] || !strcmp(host, "0.0.0.0"))
        return false;
    c64_device_t device = {0};
    if (!c64_device_id_from_host(device.id, sizeof(device.id), NULL, host))
        return false;
    snprintf(device.name, sizeof(device.name), "Default");
    snprintf(device.host, sizeof(device.host), "%s", host);
    snprintf(device.dns_server_ip, sizeof(device.dns_server_ip), "%s", obs_data_get_string(settings, "dns_server_ip"));
    device.video_port = (uint32_t)obs_data_get_int(settings, "video_port");
    device.audio_port = (uint32_t)obs_data_get_int(settings, "audio_port");
    device.control_port = (uint32_t)obs_data_get_int(settings, "control_port");
    if (!c64_device_registry_upsert(&device))
        return false;
    char password_key[96];
    c64_device_password_key(password_key, sizeof(password_key), device.id);
    obs_data_set_string(settings, password_key, obs_data_get_string(settings, "c64_password"));
    obs_data_set_string(settings, "c64_device", device.id);
    /* Keep the legacy key for a compatibility release. The selected-device
     * password is authoritative; no registry file receives either value. */
    C64_LOG_INFO("%s migrated legacy host to device '%s'", DEVICE_LOG_PREFIX, device.id);
    return true;
}

bool c64_device_registry_apply_selected(obs_data_t *settings)
{
    if (!settings)
        return false;
    const c64_device_t *device = c64_device_registry_get(obs_data_get_string(settings, "c64_device"));
    if (!device)
        return false;
    obs_data_set_string(settings, "c64_host", device->host);
    obs_data_set_string(settings, "dns_server_ip", device->dns_server_ip);
    obs_data_set_int(settings, "video_port", device->video_port);
    obs_data_set_int(settings, "audio_port", device->audio_port);
    obs_data_set_int(settings, "control_port", device->control_port);
    char password_key[96];
    c64_device_password_key(password_key, sizeof(password_key), device->id);
    obs_data_set_string(settings, "c64_password", obs_data_get_string(settings, password_key));
    return true;
}
