#include "c64-device.h"
#include "c64-file.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool c64_debug_logging = false;

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition))                                                                                              \
            return 1;                                                                                                  \
    } while (0)

static bool no_password_in_settings(const char *directory)
{
    DIR *dir = opendir(directory);
    if (!dir)
        return false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strstr(entry->d_name, ".ini"))
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        FILE *file = fopen(path, "r");
        if (!file) {
            closedir(dir);
            return false;
        }
        char line[512];
        while (fgets(line, sizeof(line), file)) {
            if (strstr(line, "password")) {
                fclose(file);
                closedir(dir);
                return false;
            }
        }
        fclose(file);
    }
    closedir(dir);
    return true;
}

int main(void)
{
    char root[] = "/tmp/c64-device-test-XXXXXX";
    CHECK(mkdtemp(root));
    CHECK(setenv("XDG_DOCUMENTS_DIR", root, 1) == 0);
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
    CHECK(c64_device_registry_get(selected));
    CHECK(c64_device_registry_delete(selected));
    CHECK(!c64_device_registry_get(selected));
    obs_data_release(settings);
    return 0;
}
