#include "c64-device.h"
#include "c64-file.h"
#include <util/platform.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#endif

#include <string.h>

bool c64_debug_logging = false;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition))                                                                                              \
            return 1;                                                                                                  \
    } while (0)

static bool no_password_in_settings(const char *directory)
{
    os_dir_t *dir = os_opendir(directory);
    if (!dir)
        return false;
    struct os_dirent *entry;
    while ((entry = os_readdir(dir)) != NULL) {
        if (entry->directory)
            continue;
        if (!strstr(entry->d_name, ".ini"))
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        FILE *file = fopen(path, "r");
        if (!file) {
            os_closedir(dir);
            return false;
        }
        char line[512];
        while (fgets(line, sizeof(line), file)) {
            if (strstr(line, "password")) {
                fclose(file);
                os_closedir(dir);
                return false;
            }
        }
        fclose(file);
    }
    os_closedir(dir);
    return true;
}

int main(void)
{
#ifndef _WIN32
    char root[] = "/tmp/c64-device-test-XXXXXX";
    CHECK(mkdtemp(root));
    CHECK(setenv("XDG_DOCUMENTS_DIR", root, 1) == 0);
#endif
    CHECK(c64_device_registry_init());
    char id[64];
    CHECK(c64_device_id_from_host(id, sizeof(id), "5D4E12", "ignored"));
    CHECK(strcmp(id, "5d4e12") == 0);
    CHECK(c64_device_id_from_host(id, sizeof(id), NULL, "C64 U.local:80"));
    CHECK(strcmp(id, "c64-u-local-80") == 0);

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "c64_host", "192.168.1.64");
    obs_data_set_string(settings, "dns_server_ip", "192.168.1.1");
    obs_data_set_int(settings, "video_port", 11000);
    obs_data_set_int(settings, "audio_port", 11001);
    obs_data_set_int(settings, "control_port", 64);
    obs_data_set_string(settings, "c64_password", "secret");
    CHECK(c64_device_registry_migrate_legacy(settings));
    const char *selected = obs_data_get_string(settings, "c64_device");
    CHECK(selected && selected[0]);
    char password_key[96];
    c64_device_password_key(password_key, sizeof(password_key), selected);
    CHECK(strcmp(obs_data_get_string(settings, password_key), "secret") == 0);
    CHECK(strcmp(obs_data_get_string(settings, "c64_password"), "secret") == 0);
    CHECK(c64_device_registry_get(selected));
    CHECK(c64_device_registry_apply_selected(settings));
    CHECK(strcmp(obs_data_get_string(settings, "c64_password"), "secret") == 0);
    char settings_dir[512];
    CHECK(c64_get_user_dir(C64_USER_DIR_SETTINGS, settings_dir, sizeof(settings_dir)));
    CHECK(no_password_in_settings(settings_dir));
    c64_device_registry_cleanup();
    CHECK(c64_device_registry_init());
    const c64_device_t *device = c64_device_registry_get(selected);
    CHECK(device);
    CHECK(c64_device_registry_delete(selected));
    CHECK(strcmp(device->id, selected) == 0);
    CHECK(!c64_device_registry_get(selected));

    /* Deleting the last device must stick: the legacy host key is still in
     * settings, and re-running migration (as every c64_update does) must not
     * resurrect the entry the user just removed. */
    CHECK(!c64_device_registry_migrate_legacy(settings));
    CHECK(!c64_device_registry_get(selected));
    CHECK(c64_device_registry_count() == 0);

    obs_data_release(settings);
    return 0;
}
