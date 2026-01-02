/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-properties.h"
#include "c64-types.h"
#include "c64-version.h"
#include "c64-network.h"
#include "c64-protocol.h"
#include "c64-video.h"
#include "c64-logging.h" // For Windows snprintf compatibility
#include "c64-file.h"
#include "c64-presets.h"
#include "c64-source.h"
#include "c64-palette.h"
#include <obs-module.h>
#include <util/platform.h>

// Cross-platform strcasecmp
#ifdef _WIN32
#define strcasecmp _stricmp
#endif
#include <util/dstr.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Forward declaration of callbacks
static bool crt_preset_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool export_config_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool import_config_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static void trim_config_string(char *str);

// Palette callbacks
static bool palette_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool palette_path_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static bool palette_load_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool palette_save_clicked(obs_properties_t *props, obs_property_t *property, void *data);
static bool palette_color_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings);
static void update_palette_color_properties(obs_data_t *settings);

// Internal settings key: used to prevent re-applying presets when reopening the Properties UI.
// (OBS may rebuild the properties view and trigger "modified" callbacks without a real user change.)
static const char *C64_PRESET_LAST_APPLIED_KEY = "crt_preset_last_applied";
static const char *C64_CONFIG_EXPORT_PATH_KEY = "config_export_path";
static const char *C64_CONFIG_IMPORT_PATH_KEY = "config_import_path";
static const char *C64_PALETTE_KEY = "palette";
static const char *C64_PALETTE_PATH_KEY = "palette_path";

// Helpers: When enforce is true (CI), apply both default and direct values
static inline void c64_set_string(obs_data_t *settings, const char *key, const char *value, bool enforce)
{
    if (enforce) {
        obs_data_set_default_string(settings, key, value);
        obs_data_set_string(settings, key, value);
    } else {
        obs_data_set_default_string(settings, key, value);
    }
}

static inline void c64_set_int(obs_data_t *settings, const char *key, int value, bool enforce)
{
    if (enforce) {
        obs_data_set_default_int(settings, key, value);
        obs_data_set_int(settings, key, value);
    } else {
        obs_data_set_default_int(settings, key, value);
    }
}

static inline void c64_set_bool(obs_data_t *settings, const char *key, bool value, bool enforce)
{
    if (enforce) {
        obs_data_set_default_bool(settings, key, value);
        obs_data_set_bool(settings, key, value);
    } else {
        obs_data_set_default_bool(settings, key, value);
    }
}

obs_properties_t *c64_create_properties(void *data)
{
    struct c64_source *context = (struct c64_source *)data;
    obs_properties_t *props = obs_properties_create();

    // Plugin Information Group
    obs_property_t *info_group = obs_properties_add_group(props, "info_group", obs_module_text("PluginInformation"),
                                                          OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *info_props = obs_property_group_content(info_group);

    // Version information (read-only)
    obs_property_t *version_prop =
        obs_properties_add_text(info_props, "version_info", obs_module_text("Version"), OBS_TEXT_INFO);
    obs_property_set_long_description(version_prop, c64_get_build_info());
    obs_property_text_set_info_type(version_prop, OBS_TEXT_INFO_NORMAL);

    UNUSED_PARAMETER(context);

    // Network Configuration Group
    obs_property_t *network_group = obs_properties_add_group(
        props, "network_group", obs_module_text("NetworkConfiguration"), OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *network_props = obs_property_group_content(network_group);

    // DNS Server IP
    obs_property_t *dns_prop =
        obs_properties_add_text(network_props, "dns_server_ip", obs_module_text("DNSServerIP"), OBS_TEXT_DEFAULT);
    obs_property_set_long_description(dns_prop, obs_module_text("DNSServerIP.Description"));

    // C64 Ultimate Host (IP Address or Hostname)
    obs_property_t *host_prop =
        obs_properties_add_text(network_props, "c64_host", obs_module_text("C64UHost"), OBS_TEXT_DEFAULT);
    obs_property_set_long_description(host_prop, obs_module_text("C64UHost.Description"));

    // OBS IP Address
    obs_property_t *obs_ip_prop =
        obs_properties_add_text(network_props, "obs_ip_address", obs_module_text("OBSMachineIP"), OBS_TEXT_DEFAULT);
    obs_property_set_long_description(obs_ip_prop, obs_module_text("OBSMachineIP.Description"));

    // Auto-detect IP toggle
    obs_property_t *auto_ip_prop =
        obs_properties_add_bool(network_props, "auto_detect_ip", obs_module_text("AutoDetectOBSIP"));
    obs_property_set_long_description(auto_ip_prop, obs_module_text("AutoDetectOBSIP.Description"));

    // UDP Ports
    obs_property_t *video_port_prop =
        obs_properties_add_int(network_props, "video_port", obs_module_text("VideoPort"), 1024, 65535, 1);
    obs_property_set_long_description(video_port_prop, obs_module_text("VideoPort.Description"));

    obs_property_t *audio_port_prop =
        obs_properties_add_int(network_props, "audio_port", obs_module_text("AudioPort"), 1024, 65535, 1);
    obs_property_set_long_description(audio_port_prop, obs_module_text("AudioPort.Description"));

    // Control Port (TCP)
    obs_property_t *control_port_prop =
        obs_properties_add_int(network_props, "control_port", obs_module_text("ControlPort"), 64, 65535, 1);
    obs_property_set_long_description(control_port_prop, obs_module_text("ControlPort.Description"));

    // Buffer Delay
    obs_property_t *delay_prop =
        obs_properties_add_int_slider(network_props, "buffer_delay_ms", obs_module_text("BufferDelay"), 0, 500, 1);
    obs_property_set_long_description(delay_prop, obs_module_text("BufferDelay.Description"));

    // Recording Group
    obs_property_t *recording_group = obs_properties_add_group(props, "recording_group", obs_module_text("Recording"),
                                                               OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *recording_props = obs_property_group_content(recording_group);

    // Output Folder
    obs_property_t *save_folder_prop = obs_properties_add_path(
        recording_props, "save_folder", obs_module_text("OutputFolder"), OBS_PATH_DIRECTORY, NULL, NULL);
    obs_property_set_long_description(save_folder_prop, obs_module_text("OutputFolder.Description"));

    // Record Raw Video (AVI) and Audio (WAV)
    obs_property_t *record_video_prop =
        obs_properties_add_bool(recording_props, "record_video", obs_module_text("RecordRawVideo"));
    obs_property_set_long_description(record_video_prop, obs_module_text("RecordRawVideo.Description"));

    // Record Raw Frames (BMP)
    obs_property_t *record_frames_prop =
        obs_properties_add_bool(recording_props, "record_frames", obs_module_text("RecordRawFrames"));
    obs_property_set_long_description(record_frames_prop, obs_module_text("RecordRawFrames.Description"));

    // Record Network and Streaming Events (CSV)
    obs_property_t *record_csv_prop =
        obs_properties_add_bool(recording_props, "record_csv", obs_module_text("RecordNetworkEvents"));
    obs_property_set_long_description(record_csv_prop, obs_module_text("RecordNetworkEvents.Description"));

    // Show Debug Messages in OBS Logs
    obs_property_t *debug_prop =
        obs_properties_add_bool(recording_props, "debug_logging", obs_module_text("ShowDebugMessages"));
    obs_property_set_long_description(debug_prop, obs_module_text("ShowDebugMessages.Description"));

    // ═══════════════════════════════════════════════════════════════════════════
    // Palette Group (placed BEFORE Effects, as per spec)
    // ═══════════════════════════════════════════════════════════════════════════
    obs_property_t *palette_group = obs_properties_add_group(props, "palette_group", obs_module_text("Palette"),
                                                             OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *palette_props = obs_property_group_content(palette_group);

    // Palette dropdown
    obs_property_t *palette_prop = obs_properties_add_list(palette_props, C64_PALETTE_KEY,
                                                           obs_module_text("PaletteSelection"), OBS_COMBO_TYPE_LIST,
                                                           OBS_COMBO_FORMAT_STRING);

    // Set description based on currently active palette
    const char *active_id = c64_palette_get_active_id();
    const char *palette_desc = active_id ? c64_palette_get_description(active_id) : NULL;
    if (palette_desc) {
        obs_property_set_long_description(palette_prop, palette_desc);
    } else {
        obs_property_set_long_description(palette_prop, obs_module_text("PaletteSelection.Description"));
    }

    c64_palette_populate_list(palette_prop);
    obs_property_set_modified_callback(palette_prop, palette_changed);

    // Custom palette file path
    obs_property_t *path_prop = obs_properties_add_path(palette_props, C64_PALETTE_PATH_KEY,
                                                        obs_module_text("PalettePath"), OBS_PATH_FILE_SAVE,
                                                        "VPL Palette Files (*.vpl);;All Files (*.*)", NULL);
    obs_property_set_long_description(path_prop, obs_module_text("PalettePath.Description"));
    obs_property_set_modified_callback(path_prop, palette_path_changed);

    // Load and Save buttons
    obs_properties_add_button2(palette_props, "palette_load", obs_module_text("PaletteLoad"), palette_load_clicked,
                               data);
    obs_properties_add_button2(palette_props, "palette_save", obs_module_text("PaletteSave"), palette_save_clicked,
                               data);

    // Visual color editor (4x4 grid as 4 rows)
    // Row 0: Colors 0-3
    static const char *color_names[16] = {"Black",   "White",      "Red",       "Cyan",     "Purple", "Green",
                                          "Blue",    "Yellow",     "Orange",    "Brown",    "Pink",   "DarkGrey",
                                          "MedGrey", "LightGreen", "LightBlue", "LightGrey"};
    for (int i = 0; i < 16; i++) {
        char key[32];
        char label[64];
        snprintf(key, sizeof(key), "palette_color_%d", i);
        snprintf(label, sizeof(label), "%d: %s", i, color_names[i]);

        obs_property_t *color_prop = obs_properties_add_color(palette_props, key, label);
        obs_property_set_modified_callback2(color_prop, palette_color_changed, data);
    }

    // Effects Group
    obs_property_t *effects_group = obs_properties_add_group(props, "effects_group", obs_module_text("Effects"),
                                                             OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *effects_props = obs_property_group_content(effects_group);

    // Presets dropdown at the top
    obs_property_t *preset_prop = obs_properties_add_list(effects_props, "crt_preset", obs_module_text("Presets"),
                                                          OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_long_description(preset_prop, obs_module_text("Presets.Description"));

    // Populate presets from the loaded presets file
    c64_presets_populate_list(preset_prop);

    // Add modified callback to apply preset when selected
    obs_property_set_modified_callback(preset_prop, crt_preset_changed);

    // Scanlines
    obs_property_t *scanline_distance_prop = obs_properties_add_list(effects_props, "scan_line_distance",
                                                                     obs_module_text("ScanLineDistance"),
                                                                     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_FLOAT);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.None"), 0.0);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.Tight"), 0.25);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.Normal"), 0.50);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.Wide"), 1.0);
    obs_property_list_add_float(scanline_distance_prop, obs_module_text("ScanLineDistance.ExtraWide"), 2.0);
    obs_property_set_long_description(scanline_distance_prop, obs_module_text("ScanLineDistance.Description"));

    obs_property_t *scanline_strength_prop = obs_properties_add_float_slider(
        effects_props, "scan_line_strength", obs_module_text("ScanLineStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(scanline_strength_prop, obs_module_text("ScanLineStrength.Description"));

    // Pixel Geometry
    obs_property_t *pixel_width_prop =
        obs_properties_add_float_slider(effects_props, "pixel_width", obs_module_text("PixelWidth"), 0.5, 4.0, 0.1);
    obs_property_set_long_description(pixel_width_prop, obs_module_text("PixelWidth.Description"));

    obs_property_t *pixel_height_prop =
        obs_properties_add_float_slider(effects_props, "pixel_height", obs_module_text("PixelHeight"), 0.5, 4.0, 0.1);
    obs_property_set_long_description(pixel_height_prop, obs_module_text("PixelHeight.Description"));

    obs_property_t *blur_strength_prop = obs_properties_add_float_slider(
        effects_props, "blur_strength", obs_module_text("BlurStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(blur_strength_prop, obs_module_text("BlurStrength.Description"));

    // CRT Bloom
    obs_property_t *bloom_strength_prop = obs_properties_add_float_slider(
        effects_props, "bloom_strength", obs_module_text("BloomStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(bloom_strength_prop, obs_module_text("BloomStrength.Description"));

    // CRT Afterglow
    obs_property_t *afterglow_duration_prop = obs_properties_add_int_slider(
        effects_props, "afterglow_duration_ms", obs_module_text("AfterglowDuration"), 0, 250, 10);
    obs_property_set_long_description(afterglow_duration_prop, obs_module_text("AfterglowDuration.Description"));

    obs_property_t *afterglow_curve_prop = obs_properties_add_list(
        effects_props, "afterglow_curve", obs_module_text("AfterglowCurve"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.InstantFade"), 0);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.RapidFade"), 1);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.GradualFade"), 2);
    obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.LongTail"), 3);
    obs_property_set_long_description(afterglow_curve_prop, obs_module_text("AfterglowCurve.Description"));

    // Screen Tint
    obs_property_t *tint_mode_prop = obs_properties_add_list(effects_props, "tint_mode", obs_module_text("TintMode"),
                                                             OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.None"), 0);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.Amber"), 1);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.Green"), 2);
    obs_property_list_add_int(tint_mode_prop, obs_module_text("TintMode.Monochrome"), 3);
    obs_property_set_long_description(tint_mode_prop, obs_module_text("TintMode.Description"));

    obs_property_t *tint_strength_prop = obs_properties_add_float_slider(
        effects_props, "tint_strength", obs_module_text("TintStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(tint_strength_prop, obs_module_text("TintStrength.Description"));

    // Import/Export Group
    obs_property_t *importexport_group = obs_properties_add_group(
        props, "importexport_group", obs_module_text("ImportExport"), OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *importexport_props = obs_property_group_content(importexport_group);

    // Import configuration (INI)
    // OBS does not provide a generic "open file dialog" API for button callbacks in libobs,
    // so we pair each action button with an OBS path selector which provides the native chooser.
    obs_property_t *import_path_prop = obs_properties_add_path(importexport_props, C64_CONFIG_IMPORT_PATH_KEY,
                                                               obs_module_text("ImportConfigPath"), OBS_PATH_FILE,
                                                               "INI Files (*.ini);;All Files (*.*)", NULL);
    obs_property_set_long_description(import_path_prop, obs_module_text("ImportConfigPath.Description"));
    obs_properties_add_button(importexport_props, "import_config", obs_module_text("ImportConfig"),
                              import_config_clicked);

    // Export configuration (INI)
    obs_property_t *export_path_prop = obs_properties_add_path(importexport_props, C64_CONFIG_EXPORT_PATH_KEY,
                                                               obs_module_text("ExportConfigPath"), OBS_PATH_FILE_SAVE,
                                                               "INI Files (*.ini);;All Files (*.*)", NULL);
    obs_property_set_long_description(export_path_prop, obs_module_text("ExportConfigPath.Description"));
    obs_properties_add_button(importexport_props, "export_config", obs_module_text("ExportConfig"),
                              export_config_clicked);

    return props;
}

static void c64_default_export_ini_path(char *path, size_t path_size)
{
    if (!path || path_size < 32)
        return;

    char documents_path[256];
    if (c64_get_user_documents_path(documents_path, sizeof(documents_path))) {
#ifdef _WIN32
        snprintf(path, path_size, "%s\\c64stream-properties.ini", documents_path);
#else
        snprintf(path, path_size, "%s/c64stream-properties.ini", documents_path);
#endif
        return;
    }

    // Fallback: current directory.
    snprintf(path, path_size, "c64stream-properties.ini");
}

static bool c64_ensure_parent_dir_exists(const char *file_path)
{
    if (!file_path || file_path[0] == '\0')
        return false;

    char dir[1024];
    strncpy(dir, file_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    char *slash = strrchr(dir, '/');
    char *bslash = strrchr(dir, '\\');
    char *sep = slash;
    if (bslash && (!sep || bslash > sep))
        sep = bslash;

    if (!sep)
        return true; // No directory component

    *sep = '\0';
    if (dir[0] == '\0')
        return true;

    return c64_create_directory_recursive(dir);
}

static bool c64_export_settings_to_ini(obs_data_t *settings, const char *path)
{
    if (!settings || !path || path[0] == '\0')
        return false;

    if (!c64_ensure_parent_dir_exists(path)) {
        C64_LOG_WARNING("Export: failed to create parent directory for %s", path);
        return false;
    }

    FILE *f = os_fopen(path, "w");
    if (!f) {
        C64_LOG_WARNING("Export: failed to open %s for writing", path);
        return false;
    }

    const char *c64_host = obs_data_get_string(settings, "c64_host");
    const char *dns_server_ip = obs_data_get_string(settings, "dns_server_ip");
    const char *obs_ip_address = obs_data_get_string(settings, "obs_ip_address");
    const bool auto_detect_ip = obs_data_get_bool(settings, "auto_detect_ip");
    const int video_port = (int)obs_data_get_int(settings, "video_port");
    const int audio_port = (int)obs_data_get_int(settings, "audio_port");
    const int control_port = (int)obs_data_get_int(settings, "control_port");
    const int buffer_delay_ms = (int)obs_data_get_int(settings, "buffer_delay_ms");

    const char *save_folder = obs_data_get_string(settings, "save_folder");
    const bool record_frames = obs_data_get_bool(settings, "record_frames");
    const bool record_video = obs_data_get_bool(settings, "record_video");
    const bool record_csv = obs_data_get_bool(settings, "record_csv");

    const bool debug_logging = obs_data_get_bool(settings, "debug_logging");

    const char *crt_preset = obs_data_get_string(settings, "crt_preset");
    const double scan_line_distance = obs_data_get_double(settings, "scan_line_distance");
    const double scan_line_strength = obs_data_get_double(settings, "scan_line_strength");
    const double pixel_width = obs_data_get_double(settings, "pixel_width");
    const double pixel_height = obs_data_get_double(settings, "pixel_height");
    const double blur_strength = obs_data_get_double(settings, "blur_strength");
    const double bloom_strength = obs_data_get_double(settings, "bloom_strength");
    const int afterglow_duration_ms = (int)obs_data_get_int(settings, "afterglow_duration_ms");
    const int afterglow_curve = (int)obs_data_get_int(settings, "afterglow_curve");
    const int tint_mode = (int)obs_data_get_int(settings, "tint_mode");
    const double tint_strength = obs_data_get_double(settings, "tint_strength");

    // Get current timestamp for export
    time_t now = time(NULL);
    struct tm *utc_time = gmtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", utc_time);

    // Determine platform string
#if defined(_WIN32) || defined(_WIN64)
#if defined(_WIN64)
    const char *platform = "windows-x64";
#else
    const char *platform = "windows-x86";
#endif
#elif defined(__APPLE__)
    const char *platform = "macos";
#elif defined(__linux__)
#if defined(__x86_64__)
    const char *platform = "linux-x86_64";
#elif defined(__aarch64__)
    const char *platform = "linux-arm64";
#else
    const char *platform = "linux";
#endif
#else
    const char *platform = "unknown";
#endif

    fprintf(f, "# C64 Stream Properties Export\n");
    fprintf(f, "#\n");
    fprintf(f, "# This file can be imported via the C64 Stream source Properties window.\n");
    fprintf(f, "#\n");
    fprintf(f, "# --- System Information (for bug reports) ---\n");
    fprintf(f, "# Plugin:   %s\n", c64_get_build_info());
    fprintf(f, "# OBS:      %s\n", obs_get_version_string());
    fprintf(f, "# Platform: %s\n", platform);
    fprintf(f, "# Exported: %s\n", timestamp);
    fprintf(f, "#\n\n");

    fprintf(f, "[network]\n");
    fprintf(f, "c64_host=%s\n", c64_host ? c64_host : "");
    fprintf(f, "dns_server_ip=%s\n", dns_server_ip ? dns_server_ip : "");
    fprintf(f, "obs_ip_address=%s\n", obs_ip_address ? obs_ip_address : "");
    fprintf(f, "auto_detect_ip=%s\n", auto_detect_ip ? "true" : "false");
    fprintf(f, "video_port=%d\n", video_port);
    fprintf(f, "audio_port=%d\n", audio_port);
    fprintf(f, "control_port=%d\n", control_port);
    fprintf(f, "buffer_delay_ms=%d\n", buffer_delay_ms);
    fprintf(f, "\n");

    fprintf(f, "[recording]\n");
    fprintf(f, "save_folder=%s\n", save_folder ? save_folder : "");
    fprintf(f, "record_frames=%s\n", record_frames ? "true" : "false");
    fprintf(f, "record_video=%s\n", record_video ? "true" : "false");
    fprintf(f, "record_csv=%s\n", record_csv ? "true" : "false");
    fprintf(f, "\n");

    fprintf(f, "[debug]\n");
    fprintf(f, "debug_logging=%s\n", debug_logging ? "true" : "false");
    fprintf(f, "\n");

    fprintf(f, "[effects]\n");
    fprintf(f, "crt_preset=%s\n", crt_preset ? crt_preset : "");
    fprintf(f, "scan_line_distance=%.6f\n", scan_line_distance);
    fprintf(f, "scan_line_strength=%.6f\n", scan_line_strength);
    fprintf(f, "pixel_width=%.6f\n", pixel_width);
    fprintf(f, "pixel_height=%.6f\n", pixel_height);
    fprintf(f, "blur_strength=%.6f\n", blur_strength);
    fprintf(f, "bloom_strength=%.6f\n", bloom_strength);
    fprintf(f, "afterglow_duration_ms=%d\n", afterglow_duration_ms);
    fprintf(f, "afterglow_curve=%d\n", afterglow_curve);
    fprintf(f, "tint_mode=%d\n", tint_mode);
    fprintf(f, "tint_strength=%.6f\n", tint_strength);
    fprintf(f, "\n");

    fclose(f);
    return true;
}

static bool c64_parse_bool(const char *value, bool default_value)
{
    if (!value || value[0] == '\0')
        return default_value;
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0)
        return true;
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0)
        return false;
    return default_value;
}

static bool c64_apply_ini_to_settings(obs_data_t *settings, const char *path)
{
    if (!settings || !path || path[0] == '\0')
        return false;

    FILE *file = os_fopen(path, "r");
    if (!file) {
        C64_LOG_WARNING("Import: failed to open %s", path);
        return false;
    }

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        trim_config_string(line);

        if (line[0] == '\0' || line[0] == ';' || line[0] == '#')
            continue;

        if (line[0] == '[')
            continue; // section header

        char *equals = strchr(line, '=');
        if (!equals)
            continue;

        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        trim_config_string(key);
        trim_config_string(value);

        if (strcmp(key, "dns_server_ip") == 0) {
            obs_data_set_string(settings, "dns_server_ip", value);
        } else if (strcmp(key, "c64_host") == 0) {
            obs_data_set_string(settings, "c64_host", value);
        } else if (strcmp(key, "obs_ip_address") == 0) {
            // Allow empty string (means "leave as-is"/auto-detect default).
            if (value && value[0] != '\0')
                obs_data_set_string(settings, "obs_ip_address", value);
        } else if (strcmp(key, "auto_detect_ip") == 0) {
            obs_data_set_bool(settings, "auto_detect_ip", c64_parse_bool(value, true));
        } else if (strcmp(key, "video_port") == 0) {
            int port = atoi(value);
            if (port >= 1024 && port <= 65535)
                obs_data_set_int(settings, "video_port", port);
        } else if (strcmp(key, "audio_port") == 0) {
            int port = atoi(value);
            if (port >= 1024 && port <= 65535)
                obs_data_set_int(settings, "audio_port", port);
        } else if (strcmp(key, "control_port") == 0) {
            int port = atoi(value);
            if (port >= 64 && port <= 65535)
                obs_data_set_int(settings, "control_port", port);
        } else if (strcmp(key, "buffer_delay_ms") == 0) {
            int delay = atoi(value);
            if (delay >= 0 && delay <= 500)
                obs_data_set_int(settings, "buffer_delay_ms", delay);
        } else if (strcmp(key, "save_folder") == 0) {
            if (value && value[0] != '\0')
                obs_data_set_string(settings, "save_folder", value);
        } else if (strcmp(key, "record_frames") == 0) {
            obs_data_set_bool(settings, "record_frames", c64_parse_bool(value, false));
        } else if (strcmp(key, "record_video") == 0) {
            obs_data_set_bool(settings, "record_video", c64_parse_bool(value, false));
        } else if (strcmp(key, "record_csv") == 0) {
            obs_data_set_bool(settings, "record_csv", c64_parse_bool(value, false));
        } else if (strcmp(key, "debug_logging") == 0) {
            obs_data_set_bool(settings, "debug_logging", c64_parse_bool(value, false));
        } else if (strcmp(key, "crt_preset") == 0) {
            if (value && value[0] != '\0') {
                obs_data_set_string(settings, "crt_preset", value);
                // Prevent preset re-apply from overwriting imported tweaks when Properties UI rebuilds.
                obs_data_set_string(settings, C64_PRESET_LAST_APPLIED_KEY, value);
            }
        } else if (strcmp(key, "scan_line_distance") == 0) {
            // Validate range: 0.0 - 2.0 (Critical #3: import validation)
            double v = os_strtod(value);
            if (v >= 0.0 && v <= 2.0)
                obs_data_set_double(settings, "scan_line_distance", v);
        } else if (strcmp(key, "scan_line_strength") == 0) {
            // Validate range: 0.0 - 1.0
            double v = os_strtod(value);
            if (v >= 0.0 && v <= 1.0)
                obs_data_set_double(settings, "scan_line_strength", v);
        } else if (strcmp(key, "pixel_width") == 0) {
            // Validate range: 0.5 - 3.0
            double v = os_strtod(value);
            if (v >= 0.5 && v <= 3.0)
                obs_data_set_double(settings, "pixel_width", v);
        } else if (strcmp(key, "pixel_height") == 0) {
            // Validate range: 0.5 - 3.0
            double v = os_strtod(value);
            if (v >= 0.5 && v <= 3.0)
                obs_data_set_double(settings, "pixel_height", v);
        } else if (strcmp(key, "blur_strength") == 0) {
            // Validate range: 0.0 - 1.0
            double v = os_strtod(value);
            if (v >= 0.0 && v <= 1.0)
                obs_data_set_double(settings, "blur_strength", v);
        } else if (strcmp(key, "bloom_strength") == 0) {
            // Validate range: 0.0 - 1.0
            double v = os_strtod(value);
            if (v >= 0.0 && v <= 1.0)
                obs_data_set_double(settings, "bloom_strength", v);
        } else if (strcmp(key, "afterglow_duration_ms") == 0) {
            int ms = atoi(value);
            if (ms >= 0 && ms <= 3000)
                obs_data_set_int(settings, "afterglow_duration_ms", ms);
        } else if (strcmp(key, "afterglow_curve") == 0) {
            int curve = atoi(value);
            if (curve >= 0 && curve <= 3)
                obs_data_set_int(settings, "afterglow_curve", curve);
        } else if (strcmp(key, "tint_mode") == 0) {
            int mode = atoi(value);
            if (mode >= 0 && mode <= 3)
                obs_data_set_int(settings, "tint_mode", mode);
        } else if (strcmp(key, "tint_strength") == 0) {
            // Validate range: 0.0 - 1.0
            double v = os_strtod(value);
            if (v >= 0.0 && v <= 1.0)
                obs_data_set_double(settings, "tint_strength", v);
        }
    }

    fclose(file);
    return true;
}

// Callback for preset selection
static bool crt_preset_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    if (!settings)
        return false;

    const char *preset_name = obs_data_get_string(settings, "crt_preset");
    if (!preset_name || preset_name[0] == '\0')
        return false;

    const char *last_applied = obs_data_get_string(settings, C64_PRESET_LAST_APPLIED_KEY);
    if (last_applied && last_applied[0] != '\0' && strcmp(last_applied, preset_name) == 0) {
        // Already applied; don't overwrite user tweaks when reopening the Properties panel.
        return false;
    }

    // Apply the preset
    if (c64_presets_apply(settings, preset_name)) {
        C64_LOG_INFO("Applied CRT preset: %s", preset_name);
        obs_data_set_string(settings, C64_PRESET_LAST_APPLIED_KEY, preset_name);
        return true;
    }

    return false;
}

void c64_set_property_defaults(obs_data_t *settings)
{
    // Defaults initialization

    obs_data_set_default_bool(settings, "debug_logging", true);
    // Default: auto-detect OBS IP enabled (OBS "Defaults" button uses these defaults)
    obs_data_set_default_bool(settings, "auto_detect_ip", true);
    obs_data_set_default_string(settings, "dns_server_ip", "192.168.1.1");
    obs_data_set_default_string(settings, "c64_host", C64_DEFAULT_HOST);
    // Default OBS IP should be the dynamically detected local IP so OBS "Defaults" fills it in immediately.
    // If detection fails, fall back to empty and the runtime code will use localhost.
    {
        char detected_ip[64];
        if (c64_detect_local_ip(detected_ip, sizeof(detected_ip))) {
            obs_data_set_default_string(settings, "obs_ip_address", detected_ip);
        } else {
            obs_data_set_default_string(settings, "obs_ip_address", "");
        }
    }
    obs_data_set_default_int(settings, "video_port", C64_DEFAULT_VIDEO_PORT);
    obs_data_set_default_int(settings, "audio_port", C64_DEFAULT_AUDIO_PORT);
    obs_data_set_default_int(settings, "control_port", C64_CONTROL_PORT);
    obs_data_set_default_int(settings, "buffer_delay_ms", 10); // Default 10ms buffer delay

    // Frame saving defaults
    obs_data_set_default_bool(settings, "record_frames", false); // Disabled by default

    // Platform-specific default recording folder (absolute paths to avoid tilde expansion issues)
    char platform_path[512];
    char documents_path[256];

    if (c64_get_user_documents_path(documents_path, sizeof(documents_path))) {
        // Use user's Documents folder
#ifdef _WIN32
        snprintf(platform_path, sizeof(platform_path), "%s\\obs-studio\\c64stream\\recordings", documents_path);
#else
        snprintf(platform_path, sizeof(platform_path), "%s/obs-studio/c64stream/recordings", documents_path);
#endif
    } else {
        // Fallback to platform-specific defaults
#ifdef _WIN32
        strcpy(platform_path, "C:\\Users\\Public\\Documents\\obs-studio\\c64stream\\recordings");
#elif defined(__APPLE__)
        strcpy(platform_path, "/Users/user/Documents/obs-studio/c64stream/recordings");
#else // Linux and other Unix-like systems
        strcpy(platform_path, "/home/user/Documents/obs-studio/c64stream/recordings");
#endif
    }

    obs_data_set_default_string(settings, "save_folder", platform_path);

    // Default export/import path for sharing settings.
    {
        char ini_path[512];
        c64_default_export_ini_path(ini_path, sizeof(ini_path));
        obs_data_set_default_string(settings, C64_CONFIG_EXPORT_PATH_KEY, ini_path);
        obs_data_set_default_string(settings, C64_CONFIG_IMPORT_PATH_KEY, ini_path);
    }

    // Video recording defaults
    obs_data_set_default_bool(settings, "record_video", false); // Disabled by default

    // CSV recording defaults
    obs_data_set_default_bool(settings, "record_csv", false); // Disabled by default

    // CRT effects defaults
    obs_data_set_default_double(settings, "scan_line_distance", 0.0);
    obs_data_set_default_double(settings, "scan_line_strength", 0.0);
    obs_data_set_default_double(settings, "pixel_width", 1.0);
    obs_data_set_default_double(settings, "pixel_height", 1.0);
    obs_data_set_default_double(settings, "blur_strength", 0.0);
    obs_data_set_default_double(settings, "bloom_strength", 0.0);
    obs_data_set_default_int(settings, "afterglow_duration_ms", 0);
    obs_data_set_default_int(settings, "afterglow_curve", 2);
    obs_data_set_default_int(settings, "tint_mode", 0);
    obs_data_set_default_double(settings, "tint_strength", 0.0);

    // Palette defaults
    obs_data_set_default_string(settings, C64_PALETTE_KEY, "Default");
    // Set default palette path to user palette directory
    {
        char palette_dir[512];
        if (c64_palette_get_user_dir(palette_dir, sizeof(palette_dir))) {
#ifdef _WIN32
            char default_path[600];
            snprintf(default_path, sizeof(default_path), "%s\\MyPalette.vpl", palette_dir);
            obs_data_set_default_string(settings, C64_PALETTE_PATH_KEY, default_path);
#else
            char default_path[600];
            snprintf(default_path, sizeof(default_path), "%s/MyPalette.vpl", palette_dir);
            obs_data_set_default_string(settings, C64_PALETTE_PATH_KEY, default_path);
#endif
        }
    }
    // Initialize palette color properties from the current working palette
    update_palette_color_properties(settings);

    // Load configuration overrides from properties.ini if available
    c64_load_configuration(settings);
}

static bool export_config_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context || !context->source)
        return false;

    obs_data_t *settings = obs_source_get_settings(context->source);
    if (!settings)
        return false;

    const char *path = obs_data_get_string(settings, C64_CONFIG_EXPORT_PATH_KEY);
    char fallback_path[512];
    if (!path || path[0] == '\0') {
        c64_default_export_ini_path(fallback_path, sizeof(fallback_path));
        obs_data_set_string(settings, C64_CONFIG_EXPORT_PATH_KEY, fallback_path);
        path = fallback_path;
    }

    const bool ok = c64_export_settings_to_ini(settings, path);
    if (ok) {
        C64_LOG_INFO("Exported C64 Stream settings to %s", path);
    } else {
        C64_LOG_WARNING("Failed to export C64 Stream settings to %s", path ? path : "(null)");
    }

    obs_data_release(settings);
    return ok; // Refresh so the path field updates if we filled the fallback path.
}

static bool import_config_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context || !context->source)
        return false;

    obs_data_t *settings = obs_source_get_settings(context->source);
    if (!settings)
        return false;

    const char *path = obs_data_get_string(settings, C64_CONFIG_IMPORT_PATH_KEY);
    if (!path || path[0] == '\0') {
        C64_LOG_WARNING("Import: no configuration file selected");
        obs_data_release(settings);
        return false;
    }

    if (!os_file_exists(path)) {
        C64_LOG_WARNING("Import: file does not exist: %s", path);
        obs_data_release(settings);
        return false;
    }

    const bool ok = c64_apply_ini_to_settings(settings, path);
    if (ok) {
        obs_source_update(context->source, settings);
        C64_LOG_INFO("Imported C64 Stream settings from %s", path);
    } else {
        C64_LOG_WARNING("Failed to import C64 Stream settings from %s", path);
    }

    obs_data_release(settings);
    return true; // Always refresh so the UI reflects whatever happened.
}

// ============================================================================
// Palette UI callbacks
// ============================================================================

static void update_palette_color_properties(obs_data_t *settings)
{
    uint32_t *colors = c64_palette_get_working_colors();
    if (!colors) {
        return;
    }

    for (int i = 0; i < 16; i++) {
        char key[32];
        snprintf(key, sizeof(key), "palette_color_%d", i);

        // Convert BGRA to OBS color format (ABGR stored as int)
        uint32_t bgra = colors[i];
        // Extract components
        uint8_t b = (bgra >> 16) & 0xFF;
        uint8_t g = (bgra >> 8) & 0xFF;
        uint8_t r = bgra & 0xFF;
        // OBS color format is 0xAABBGGRR (same as ABGR)
        uint32_t obs_color = 0xFF000000 | (b << 16) | (g << 8) | r;
        obs_data_set_int(settings, key, (long long)obs_color);
    }
}

static bool palette_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);

    if (!settings) {
        return false;
    }

    const char *palette_id = obs_data_get_string(settings, C64_PALETTE_KEY);
    if (!palette_id || !palette_id[0]) {
        return false;
    }

    // Update tooltip with palette description
    const char *desc = c64_palette_get_description(palette_id);
    if (desc && property) {
        obs_property_set_long_description(property, desc);
    } else if (property) {
        obs_property_set_long_description(property, obs_module_text("PaletteSelection.Description"));
    }

    // Don't re-select if already active
    const char *current = c64_palette_get_active_id();
    if (current && strcmp(current, palette_id) == 0) {
        return true; // Refresh UI to show updated description
    }

    if (c64_palette_select(palette_id)) {
        // Update color picker values to reflect the new palette
        update_palette_color_properties(settings);
        return true; // Refresh UI
    }

    return false;
}

static bool palette_path_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(property);

    if (!settings) {
        return false;
    }

    const char *path = obs_data_get_string(settings, C64_PALETTE_PATH_KEY);
    if (!path || !*path) {
        return false;
    }

    // Check if this looks like a save operation (path ends with .vpl and contains a filename)
    const char *ext = strrchr(path, '.');
    const char *filename = strrchr(path, '/');
    if (!filename) {
        filename = strrchr(path, '\\'); // Windows path separator
    }
    filename = filename ? filename + 1 : path; // Skip the separator or use whole path

    // If it has a .vpl extension and a filename, assume this is a save operation
    if (ext && strcasecmp(ext, ".vpl") == 0 && strlen(filename) > 4) {
        // Save the working palette to the specified path
        if (c64_palette_save_as("Custom Palette", path)) {
            blog(LOG_INFO, "Palette saved to: %s", path);

            // Update the palette dropdown to include the new palette
            obs_property_t *palette_prop = obs_properties_get(props, C64_PALETTE_KEY);
            if (palette_prop) {
                c64_palette_populate_list(palette_prop);
            }

            // Update the active palette selection in settings
            obs_data_set_string(settings, C64_PALETTE_KEY, c64_palette_get_active_id());

            return true; // Refresh UI
        } else {
            blog(LOG_WARNING, "Failed to save palette to: %s", path);
        }
    }

    return false; // No need to refresh UI
}

static bool palette_load_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context || !context->source) {
        return false;
    }

    obs_data_t *settings = obs_source_get_settings(context->source);
    if (!settings) {
        return false;
    }

    const char *path = obs_data_get_string(settings, C64_PALETTE_PATH_KEY);
    if (!path || !path[0]) {
        C64_LOG_WARNING("Palette load: no file selected");
        obs_data_release(settings);
        return false;
    }

    if (!os_file_exists(path)) {
        C64_LOG_WARNING("Palette load: file does not exist: %s", path);
        obs_data_release(settings);
        return false;
    }

    bool ok = c64_palette_load_from_file(path);
    if (ok) {
        // Update palette dropdown selection
        obs_data_set_string(settings, C64_PALETTE_KEY, c64_palette_get_active_id());
        // Update color picker values
        update_palette_color_properties(settings);
        obs_source_update(context->source, settings);

        // Repopulate the palette dropdown to include the loaded palette
        obs_property_t *palette_prop = obs_properties_get(props, C64_PALETTE_KEY);
        if (palette_prop) {
            c64_palette_populate_list(palette_prop);
        }
    }

    obs_data_release(settings);
    return true; // Refresh UI
}

static bool palette_save_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
    UNUSED_PARAMETER(property);

    struct c64_source *context = (struct c64_source *)data;
    if (!context || !context->source) {
        return false;
    }

    obs_data_t *settings = obs_source_get_settings(context->source);
    if (!settings) {
        return false;
    }

    const char *path = obs_data_get_string(settings, C64_PALETTE_PATH_KEY);
    if (!path || !path[0]) {
        C64_LOG_WARNING("Palette save: no file path specified");
        obs_data_release(settings);
        return false;
    }

    // Extract name from filename
    char name[64];
    const char *filename = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!filename || backslash > filename)) {
        filename = backslash;
    }
#endif
    if (filename) {
        filename++;
    } else {
        filename = path;
    }
    strncpy(name, filename, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    // Remove .vpl extension
    char *ext = strrchr(name, '.');
    if (ext && strcasecmp(ext, ".vpl") == 0) {
        *ext = '\0';
    }

    bool ok = c64_palette_save_as(name, path);
    if (ok) {
        // Update palette dropdown selection
        obs_data_set_string(settings, C64_PALETTE_KEY, c64_palette_get_active_id());
        obs_source_update(context->source, settings);

        // Repopulate the palette dropdown to include the new palette
        obs_property_t *palette_prop = obs_properties_get(props, C64_PALETTE_KEY);
        if (palette_prop) {
            c64_palette_populate_list(palette_prop);
        }
    }

    obs_data_release(settings);
    return true; // Refresh UI
}

static bool palette_color_changed(void *data, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(props);

    if (!settings || !property) {
        return false;
    }

    // Get the property name to determine which color index
    const char *name = obs_property_name(property);
    if (!name || strncmp(name, "palette_color_", 14) != 0) {
        return false;
    }

    int index = atoi(name + 14);
    if (index < 0 || index >= 16) {
        return false;
    }

    // Get the color value from settings
    long long obs_color = obs_data_get_int(settings, name);
    // OBS color format: 0xAABBGGRR (ABGR)
    uint8_t b = (obs_color >> 16) & 0xFF;
    uint8_t g = (obs_color >> 8) & 0xFF;
    uint8_t r = obs_color & 0xFF;

    // Convert to BGRA format used by the palette system
    uint32_t bgra = 0xFF000000 | (b << 16) | (g << 8) | r;

    c64_palette_set_working_color(index, bgra);

    UNUSED_PARAMETER(data);
    return false; // No UI refresh needed, LUT rebuild happens in set_working_color
}

// Helper function to trim whitespace from both ends of a string
static void trim_config_string(char *str)
{
    if (!str || !*str)
        return;

    // Trim leading whitespace
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n'))
        start++;

    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        end--;

    // Copy trimmed string back
    size_t len = (end - start) + 1;
    memmove(str, start, len);
    str[len] = '\0';
}

bool c64_load_configuration(obs_data_t *settings)
{
    if (!settings) {
        C64_LOG_WARNING("Cannot load configuration - invalid settings object");
        return false;
    }

    // Get the plugin data directory path
    char *plugin_data_path = obs_module_file("properties.ini");
    if (!plugin_data_path) {
        C64_LOG_WARNING("Failed to get plugin data path for properties.ini");
        return false;
    }

    FILE *file = fopen(plugin_data_path, "r");
    bfree(plugin_data_path);

    if (!file) {
        C64_LOG_INFO("No properties.ini found, using built-in defaults");
        return false;
    }

    C64_LOG_INFO("Loading configuration from properties.ini");

    char line[512];
    char current_section[64] = "";
    int loaded_settings = 0;
    bool ci_enforced = false; // When true (CI), apply values as defaults AND directly

    while (fgets(line, sizeof(line), file)) {
        trim_config_string(line);

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#')
            continue;

        // Check for section header [SectionName]
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, line + 1, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
                C64_LOG_DEBUG("Processing configuration section: %s", current_section);
            }
            continue;
        }

        // Parse key=value pairs
        char *equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';
            char *key = line;
            char *value = equals + 1;

            trim_config_string(key);
            trim_config_string(value);

            C64_LOG_INFO("Properties: Processing key='%s' value='%s'", key, value);

            // Detect CI enforcement flag inside [ci] section
            if (strcmp(key, "is_ci") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                if (enabled) {
                    ci_enforced = true;
                    C64_LOG_INFO("CI mode detected in properties.ini - enforcing values as defaults and direct");
                }
                continue;
            }

            // Apply configuration based on key name
            if (strcmp(key, "c64_host") == 0) {
                c64_set_string(settings, "c64_host", value, ci_enforced);
                C64_LOG_INFO("Config: c64_host = %s", value);
                loaded_settings++;
            } else if (strcmp(key, "dns_server_ip") == 0) {
                c64_set_string(settings, "dns_server_ip", value, ci_enforced);
                C64_LOG_DEBUG("Config: dns_server_ip = %s", value);
                loaded_settings++;
            } else if (strcmp(key, "video_port") == 0) {
                int port = atoi(value);
                if (port >= 1024 && port <= 65535) {
                    c64_set_int(settings, "video_port", port, ci_enforced);
                    C64_LOG_DEBUG("Config: video_port = %d", port);
                    loaded_settings++;
                }
            } else if (strcmp(key, "audio_port") == 0) {
                int port = atoi(value);
                if (port >= 1024 && port <= 65535) {
                    c64_set_int(settings, "audio_port", port, ci_enforced);
                    C64_LOG_DEBUG("Config: audio_port = %d", port);
                    loaded_settings++;
                }
            } else if (strcmp(key, "control_port") == 0) {
                int port = atoi(value);
                if (port >= 64 && port <= 65535) {
                    c64_set_int(settings, "control_port", port, ci_enforced);
                    C64_LOG_DEBUG("Config: control_port = %d", port);
                    loaded_settings++;
                }
            } else if (strcmp(key, "auto_detect_ip") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                c64_set_bool(settings, "auto_detect_ip", enabled, ci_enforced);
                C64_LOG_DEBUG("Config: auto_detect_ip = %s", enabled ? "true" : "false");
                loaded_settings++;
            } else if (strcmp(key, "obs_ip_address") == 0) {
                // Only apply non-empty value. Empty means "use auto-detect default".
                if (value && value[0] != '\0') {
                    c64_set_string(settings, "obs_ip_address", value, ci_enforced);
                    C64_LOG_INFO("Config: obs_ip_address = %s", value);
                    loaded_settings++;
                }
            } else if (strcmp(key, "buffer_delay_ms") == 0) {
                int delay = atoi(value);
                if (delay >= 0 && delay <= 500) {
                    c64_set_int(settings, "buffer_delay_ms", delay, ci_enforced);
                    C64_LOG_DEBUG("Config: buffer_delay_ms = %d", delay);
                    loaded_settings++;
                }
            } else if (strcmp(key, "debug_logging") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                c64_set_bool(settings, "debug_logging", enabled, ci_enforced);
                C64_LOG_DEBUG("Config: debug_logging = %s", enabled ? "true" : "false");
                loaded_settings++;
            } else if (strcmp(key, "record_frames") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                c64_set_bool(settings, "record_frames", enabled, ci_enforced);
                C64_LOG_DEBUG("Config: record_frames = %s", enabled ? "true" : "false");
                loaded_settings++;
            } else if (strcmp(key, "record_video") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                if (ci_enforced) {
                    // Enforce both default and direct on CI
                    obs_data_set_default_bool(settings, "record_video", enabled);
                }
                obs_data_set_bool(settings, "record_video", enabled);
                C64_LOG_DEBUG("Config: record_video = %s%s", enabled ? "true" : "false",
                              ci_enforced ? " (default+direct)" : " (direct)");
                loaded_settings++;
            } else if (strcmp(key, "record_csv") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                if (ci_enforced) {
                    obs_data_set_default_bool(settings, "record_csv", enabled);
                }
                obs_data_set_bool(settings, "record_csv", enabled);
                C64_LOG_INFO("Config: record_csv = %s%s (value='%s')", enabled ? "true" : "false",
                             ci_enforced ? " (default+direct)" : " (direct)", value);
                loaded_settings++;
            } else if (strcmp(key, "save_folder") == 0 && strlen(value) > 0) {
                c64_set_string(settings, "save_folder", value, ci_enforced);
                C64_LOG_DEBUG("Config: save_folder = %s", value);
                loaded_settings++;
            }
            // Note: CRT effects can be configured here too if needed in the future
        }
    }

    fclose(file);
    C64_LOG_INFO("Configuration loaded successfully: %d settings applied", loaded_settings);
    return true;
}
