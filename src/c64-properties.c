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
#include <obs-module.h>
#include <util/dstr.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Forward declaration of callbacks
static bool crt_preset_changed(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);

obs_properties_t *c64_create_properties(void *data)
{
    UNUSED_PARAMETER(data);
    obs_properties_t *props = obs_properties_create();

    // Plugin Information Group
    obs_property_t *info_group = obs_properties_add_group(props, "info_group", obs_module_text("PluginInformation"),
                                                          OBS_GROUP_NORMAL, obs_properties_create());
    obs_properties_t *info_props = obs_property_group_content(info_group);

    // Version information (read-only
    obs_property_t *version_prop =
        obs_properties_add_text(info_props, "version_info", obs_module_text("Version"), OBS_TEXT_INFO);
    obs_property_set_long_description(version_prop, c64_get_build_info());
    obs_property_text_set_info_type(version_prop, OBS_TEXT_INFO_NORMAL);

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
        obs_properties_add_int(network_props, "control_port", obs_module_text("ControlPort"), 1024, 65535, 1);
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
    obs_property_t *save_frames_prop =
        obs_properties_add_bool(recording_props, "save_frames", obs_module_text("RecordRawFrames"));
    obs_property_set_long_description(save_frames_prop, obs_module_text("RecordRawFrames.Description"));

    // Record Network and Streaming Events (CSV)
    obs_property_t *record_csv_prop =
        obs_properties_add_bool(recording_props, "record_csv", obs_module_text("RecordNetworkEvents"));
    obs_property_set_long_description(record_csv_prop, obs_module_text("RecordNetworkEvents.Description"));

    // Show Debug Messages in OBS Logs
    obs_property_t *debug_prop =
        obs_properties_add_bool(recording_props, "debug_logging", obs_module_text("ShowDebugMessages"));
    obs_property_set_long_description(debug_prop, obs_module_text("ShowDebugMessages.Description"));

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
        obs_properties_add_float_slider(effects_props, "pixel_width", obs_module_text("PixelWidth"), 0.5, 3.0, 0.1);
    obs_property_set_long_description(pixel_width_prop, obs_module_text("PixelWidth.Description"));

    obs_property_t *pixel_height_prop =
        obs_properties_add_float_slider(effects_props, "pixel_height", obs_module_text("PixelHeight"), 0.5, 3.0, 0.1);
    obs_property_set_long_description(pixel_height_prop, obs_module_text("PixelHeight.Description"));

    obs_property_t *blur_strength_prop = obs_properties_add_float_slider(
        effects_props, "blur_strength", obs_module_text("BlurStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(blur_strength_prop, obs_module_text("BlurStrength.Description"));

    // CRT Bloom
    obs_property_t *bloom_strength_prop = obs_properties_add_float_slider(
        effects_props, "bloom_strength", obs_module_text("BloomStrength"), 0.0, 1.0, 0.05);
    obs_property_set_long_description(bloom_strength_prop, obs_module_text("BloomStrength.Description"));

    // TODO: Afterglow effect needs more work - commented out for now
    // obs_property_t *afterglow_duration_prop = obs_properties_add_int_slider(
    //     effects_props, "afterglow_duration_ms", obs_module_text("AfterglowDuration"), 0, 3000, 10);
    // obs_property_set_long_description(afterglow_duration_prop, obs_module_text("AfterglowDuration.Description"));
    //
    // obs_property_t *afterglow_curve_prop = obs_properties_add_list(
    //     effects_props, "afterglow_curve", obs_module_text("AfterglowCurve"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    // obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.InstantFade"), 0);
    // obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.GradualFade"), 1);
    // obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.RapidFade"), 2);
    // obs_property_list_add_int(afterglow_curve_prop, obs_module_text("AfterglowCurve.LongTail"), 3);
    // obs_property_set_long_description(afterglow_curve_prop, obs_module_text("AfterglowCurve.Description"));

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

    return props;
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

    // Apply the preset
    if (c64_presets_apply(settings, preset_name)) {
        C64_LOG_INFO("Applied CRT preset: %s", preset_name);
        return true;
    }

    return false;
}

void c64_set_property_defaults(obs_data_t *settings)
{
    // Defaults initialization

    obs_data_set_default_bool(settings, "debug_logging", true);
    obs_data_set_default_bool(settings, "auto_detect_ip", true);
    obs_data_set_default_string(settings, "dns_server_ip", "192.168.1.1");
    obs_data_set_default_string(settings, "c64_host", C64_DEFAULT_HOST);
    obs_data_set_default_string(settings, "obs_ip_address", ""); // Empty by default, will be auto-detected
    obs_data_set_default_int(settings, "video_port", C64_DEFAULT_VIDEO_PORT);
    obs_data_set_default_int(settings, "audio_port", C64_DEFAULT_AUDIO_PORT);
    obs_data_set_default_int(settings, "control_port", C64_CONTROL_PORT);
    obs_data_set_default_int(settings, "buffer_delay_ms", 10); // Default 10ms buffer delay

    // Frame saving defaults
    obs_data_set_default_bool(settings, "save_frames", false); // Disabled by default

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
    // TODO: Afterglow effect needs more work - commented out for now
    // obs_data_set_default_int(settings, "afterglow_duration_ms", 0);
    // obs_data_set_default_int(settings, "afterglow_curve", 0);
    obs_data_set_default_int(settings, "tint_mode", 0);
    obs_data_set_default_double(settings, "tint_strength", 0.0);

    // Load configuration overrides from properties.ini if available
    c64_load_configuration(settings);
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

            // Apply configuration based on key name
            if (strcmp(key, "c64_host") == 0) {
                obs_data_set_default_string(settings, "c64_host", value);
                C64_LOG_INFO("Config: c64_host = %s", value);
                loaded_settings++;
            } else if (strcmp(key, "dns_server_ip") == 0) {
                obs_data_set_default_string(settings, "dns_server_ip", value);
                C64_LOG_DEBUG("Config: dns_server_ip = %s", value);
                loaded_settings++;
            } else if (strcmp(key, "video_port") == 0) {
                int port = atoi(value);
                if (port >= 1024 && port <= 65535) {
                    obs_data_set_default_int(settings, "video_port", port);
                    C64_LOG_DEBUG("Config: video_port = %d", port);
                    loaded_settings++;
                }
            } else if (strcmp(key, "audio_port") == 0) {
                int port = atoi(value);
                if (port >= 1024 && port <= 65535) {
                    obs_data_set_default_int(settings, "audio_port", port);
                    C64_LOG_DEBUG("Config: audio_port = %d", port);
                    loaded_settings++;
                }
            } else if (strcmp(key, "control_port") == 0) {
                int port = atoi(value);
                if (port >= 64 && port <= 65535) {
                    obs_data_set_default_int(settings, "control_port", port);
                    C64_LOG_DEBUG("Config: control_port = %d", port);
                    loaded_settings++;
                }
            } else if (strcmp(key, "auto_detect_ip") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                obs_data_set_default_bool(settings, "auto_detect_ip", enabled);
                C64_LOG_DEBUG("Config: auto_detect_ip = %s", enabled ? "true" : "false");
                loaded_settings++;
            } else if (strcmp(key, "obs_ip_address") == 0) {
                obs_data_set_default_string(settings, "obs_ip_address", value);
                C64_LOG_INFO("Config: obs_ip_address = %s", value);
                loaded_settings++;
            } else if (strcmp(key, "buffer_delay_ms") == 0) {
                int delay = atoi(value);
                if (delay >= 0 && delay <= 500) {
                    obs_data_set_default_int(settings, "buffer_delay_ms", delay);
                    C64_LOG_DEBUG("Config: buffer_delay_ms = %d", delay);
                    loaded_settings++;
                }
            } else if (strcmp(key, "debug_logging") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                obs_data_set_default_bool(settings, "debug_logging", enabled);
                C64_LOG_DEBUG("Config: debug_logging = %s", enabled ? "true" : "false");
                loaded_settings++;
            } else if (strcmp(key, "save_frames") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                obs_data_set_default_bool(settings, "save_frames", enabled);
                C64_LOG_DEBUG("Config: save_frames = %s", enabled ? "true" : "false");
                loaded_settings++;
            } else if (strcmp(key, "record_video") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                obs_data_set_bool(settings, "record_video", enabled);
                C64_LOG_DEBUG("Config: record_video = %s - SET DIRECTLY", enabled ? "true" : "false");
                loaded_settings++;
            } else if (strcmp(key, "record_csv") == 0) {
                bool enabled = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
                obs_data_set_bool(settings, "record_csv", enabled);
                C64_LOG_INFO("Config: record_csv = %s (value='%s') - SET DIRECTLY", enabled ? "true" : "false", value);
                loaded_settings++;
            } else if (strcmp(key, "save_folder") == 0 && strlen(value) > 0) {
                obs_data_set_default_string(settings, "save_folder", value);
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
