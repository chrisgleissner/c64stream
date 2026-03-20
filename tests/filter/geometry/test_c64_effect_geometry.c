/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-effect-geometry.h"

#include <assert.h>
#include <stdio.h>

#define TEST(name) static void name(void)
#define RUN_TEST(name)                                                                                                 \
    do {                                                                                                               \
        printf("Running test: %s ... ", #name);                                                                        \
        name();                                                                                                        \
        printf("OK\n");                                                                                                \
    } while (0)

TEST(geometry_preserve_size_ntsc)
{
    struct c64_effect_geometry geometry;
    c64_effect_geometry_init(&geometry, 384, 240, 1.2f, 1.1f, 1.0f, true);

    assert(geometry.logical_width == 384);
    assert(geometry.logical_height == 240);
    assert(geometry.virtual_width == 1843);
    assert(geometry.virtual_height == 1056);
    assert(geometry.reported_width == 384);
    assert(geometry.reported_height == 240);
    assert(geometry.draw_width == 384);
    assert(geometry.draw_height == 240);
    assert(geometry.preserve_size);
}

TEST(geometry_legacy_mode_pal)
{
    struct c64_effect_geometry geometry;
    c64_effect_geometry_init(&geometry, 384, 272, 1.0f, 1.0f, 0.5f, false);

    assert(geometry.virtual_width == 1152);
    assert(geometry.virtual_height == 816);
    assert(geometry.reported_width == 1152);
    assert(geometry.reported_height == 816);
    assert(geometry.draw_width == 1152);
    assert(geometry.draw_height == 816);
    assert(!geometry.preserve_size);
}

TEST(geometry_non_integer_scale)
{
    struct c64_effect_geometry geometry;
    c64_effect_geometry_init(&geometry, 384, 240, 1.25f, 1.75f, 0.0f, false);

    assert(geometry.virtual_width == 480);
    assert(geometry.virtual_height == 420);
    assert(geometry.reported_width == 480);
    assert(geometry.reported_height == 420);
}

TEST(migration_new_instance_defaults_on)
{
    const char *const saved_keys[] = {"c64_host"};
    obs_data_t *settings = obs_data_create();
    assert(settings != NULL);

    const bool preserve_size =
        c64_effect_settings_resolve_preserve_size(settings, saved_keys, sizeof(saved_keys) / sizeof(saved_keys[0]));

    assert(preserve_size);
    assert(obs_data_has_user_value(settings, "preserve_size"));
    assert(obs_data_get_bool(settings, "preserve_size"));
    obs_data_release(settings);
}

TEST(migration_existing_instance_missing_key_defaults_off)
{
    const char *const saved_keys[] = {"c64_host"};
    obs_data_t *settings = obs_data_create();
    assert(settings != NULL);
    obs_data_set_string(settings, "c64_host", "192.168.1.64");

    const bool preserve_size =
        c64_effect_settings_resolve_preserve_size(settings, saved_keys, sizeof(saved_keys) / sizeof(saved_keys[0]));

    assert(!preserve_size);
    assert(obs_data_has_user_value(settings, "preserve_size"));
    assert(!obs_data_get_bool(settings, "preserve_size"));
    obs_data_release(settings);
}

TEST(migration_explicit_true)
{
    obs_data_t *settings = obs_data_create();
    assert(settings != NULL);
    obs_data_set_bool(settings, "preserve_size", true);

    const bool preserve_size = c64_effect_settings_resolve_preserve_size(settings, NULL, 0);

    assert(preserve_size);
    assert(obs_data_get_bool(settings, "preserve_size"));
    obs_data_release(settings);
}

TEST(migration_explicit_false)
{
    obs_data_t *settings = obs_data_create();
    assert(settings != NULL);
    obs_data_set_bool(settings, "preserve_size", false);

    const bool preserve_size = c64_effect_settings_resolve_preserve_size(settings, NULL, 0);

    assert(!preserve_size);
    assert(!obs_data_get_bool(settings, "preserve_size"));
    obs_data_release(settings);
}

int main(void)
{
    RUN_TEST(geometry_preserve_size_ntsc);
    RUN_TEST(geometry_legacy_mode_pal);
    RUN_TEST(geometry_non_integer_scale);
    RUN_TEST(migration_new_instance_defaults_on);
    RUN_TEST(migration_existing_instance_missing_key_defaults_off);
    RUN_TEST(migration_explicit_true);
    RUN_TEST(migration_explicit_false);
    return 0;
}
