/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-parser.h"
#include "c64-logging.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Windows compatibility: strtok_r equivalent
#ifdef _WIN32
#define strtok_r strtok_s
#endif

#define MACRO_LOG_PREFIX "[c64-script] "
#define INITIAL_CAPACITY 64

// Helper: trim whitespace from start/end of string
static char *trim(char *str)
{
    while (isspace((unsigned char)*str))
        str++;
    if (*str == '\0')
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    *(end + 1) = '\0';
    return str;
}

// Helper: parse duration string (500ms, 2s, 1m, 1.5s)
static bool parse_duration(const char *str, uint32_t *duration_ms)
{
    char *endptr;
    double value = strtod(str, &endptr);
    if (endptr == str || value < 0) {
        return false;
    }

    if (strcmp(endptr, "ms") == 0) {
        *duration_ms = (uint32_t)value;
    } else if (strcmp(endptr, "s") == 0) {
        *duration_ms = (uint32_t)(value * 1000);
    } else if (strcmp(endptr, "m") == 0) {
        *duration_ms = (uint32_t)(value * 60000);
    } else {
        return false;
    }

    return true;
}

// Helper: parse path and determine type
static void parse_path(const char *path, c64_script_command_t *cmd)
{
    if (strncmp(path, "c64u:", 5) == 0) {
        cmd->path_type = C64_SCRIPT_PATH_C64U;
        strncpy(cmd->arg1, path + 5, sizeof(cmd->arg1) - 1);
    } else {
        cmd->path_type = C64_SCRIPT_PATH_LOCAL;
        strncpy(cmd->arg1, path, sizeof(cmd->arg1) - 1);
    }
    cmd->arg1[sizeof(cmd->arg1) - 1] = '\0';
}

// Helper: expand script capacity
static bool expand_capacity(c64_script_t *script)
{
    size_t new_capacity = script->capacity * 2;
    c64_script_command_t *new_commands = realloc(script->commands, new_capacity * sizeof(c64_script_command_t));
    if (!new_commands) {
        return false;
    }
    script->commands = new_commands;
    script->capacity = new_capacity;
    return true;
}

// Parse a single line into a command
static bool parse_line(const char *line, int line_number, c64_script_t *script)
{
    char buffer[1024];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *trimmed = trim(buffer);

    // Skip empty lines and comments
    if (trimmed[0] == '\0' || trimmed[0] == '#') {
        return true;
    }

    // Tokenize
    char *saveptr;
    char *cmd_str = strtok_r(trimmed, " \t", &saveptr);
    if (!cmd_str) {
        return true;
    }

    // Expand capacity if needed
    if (script->num_commands >= script->capacity) {
        if (!expand_capacity(script)) {
            snprintf(script->error_msg, sizeof(script->error_msg), "Out of memory");
            script->error_line = line_number;
            return false;
        }
    }

    c64_script_command_t *cmd = &script->commands[script->num_commands];
    memset(cmd, 0, sizeof(*cmd));
    cmd->line_number = line_number;

    // Parse command
    if (strcmp(cmd_str, "effect") == 0) {
        cmd->type = C64_SCRIPT_CMD_EFFECT;
        char *arg = strtok_r(NULL, " \t", &saveptr);
        if (!arg) {
            snprintf(script->error_msg, sizeof(script->error_msg), "effect requires preset name");
            script->error_line = line_number;
            return false;
        }
        strncpy(cmd->arg1, arg, sizeof(cmd->arg1) - 1);
    } else if (strcmp(cmd_str, "effect_param") == 0) {
        cmd->type = C64_SCRIPT_CMD_EFFECT_PARAM;
        char *name = strtok_r(NULL, " \t", &saveptr);
        char *value = strtok_r(NULL, " \t", &saveptr);
        if (!name || !value) {
            snprintf(script->error_msg, sizeof(script->error_msg), "effect_param requires name and value");
            script->error_line = line_number;
            return false;
        }
        strncpy(cmd->arg1, name, sizeof(cmd->arg1) - 1);
        strncpy(cmd->arg2, value, sizeof(cmd->arg2) - 1);
    } else if (strcmp(cmd_str, "palette") == 0) {
        cmd->type = C64_SCRIPT_CMD_PALETTE;
        char *arg = strtok_r(NULL, " \t", &saveptr);
        if (!arg) {
            snprintf(script->error_msg, sizeof(script->error_msg), "palette requires palette name");
            script->error_line = line_number;
            return false;
        }
        strncpy(cmd->arg1, arg, sizeof(cmd->arg1) - 1);
    } else if (strcmp(cmd_str, "play_sid") == 0) {
        cmd->type = C64_SCRIPT_CMD_PLAY_SID;
        char *path = strtok_r(NULL, " \t", &saveptr);
        if (!path) {
            snprintf(script->error_msg, sizeof(script->error_msg), "play_sid requires file path");
            script->error_line = line_number;
            return false;
        }
        parse_path(path, cmd);

        // Check for optional songnr parameter
        char *param = strtok_r(NULL, " \t", &saveptr);
        if (param && strncmp(param, "songnr=", 7) == 0) {
            cmd->song_number = atoi(param + 7);
        } else {
            cmd->song_number = 0;
        }
    } else if (strcmp(cmd_str, "run_prg") == 0) {
        cmd->type = C64_SCRIPT_CMD_RUN_PRG;
        char *path = strtok_r(NULL, " \t", &saveptr);
        if (!path) {
            snprintf(script->error_msg, sizeof(script->error_msg), "run_prg requires file path");
            script->error_line = line_number;
            return false;
        }
        parse_path(path, cmd);
    } else if (strcmp(cmd_str, "mount_disk") == 0) {
        cmd->type = C64_SCRIPT_CMD_MOUNT_DISK;
        char *path = strtok_r(NULL, " \t", &saveptr);
        if (!path) {
            snprintf(script->error_msg, sizeof(script->error_msg), "mount_disk requires file path");
            script->error_line = line_number;
            return false;
        }
        parse_path(path, cmd);
    } else if (strcmp(cmd_str, "autostart") == 0) {
        cmd->type = C64_SCRIPT_CMD_AUTOSTART;
    } else if (strcmp(cmd_str, "reset") == 0) {
        cmd->type = C64_SCRIPT_CMD_RESET;
    } else if (strcmp(cmd_str, "reboot") == 0) {
        cmd->type = C64_SCRIPT_CMD_REBOOT;
    } else if (strcmp(cmd_str, "wait") == 0) {
        cmd->type = C64_SCRIPT_CMD_WAIT;
        char *duration = strtok_r(NULL, " \t", &saveptr);
        if (!duration) {
            snprintf(script->error_msg, sizeof(script->error_msg), "wait requires duration");
            script->error_line = line_number;
            return false;
        }
        if (!parse_duration(duration, &cmd->duration_ms)) {
            snprintf(script->error_msg, sizeof(script->error_msg), "Invalid duration format (use 500ms, 2s, 1m)");
            script->error_line = line_number;
            return false;
        }
    } else if (strcmp(cmd_str, "record_start") == 0) {
        cmd->type = C64_SCRIPT_CMD_RECORD_START;
    } else if (strcmp(cmd_str, "record_stop") == 0) {
        cmd->type = C64_SCRIPT_CMD_RECORD_STOP;
    } else if (strcmp(cmd_str, "stop") == 0) {
        cmd->type = C64_SCRIPT_CMD_STOP;
    } else if (strcmp(cmd_str, "loop") == 0) {
        cmd->type = C64_SCRIPT_CMD_LOOP;
        char *count = strtok_r(NULL, " \t", &saveptr);
        if (count) {
            cmd->loop_count = atoi(count);
        } else {
            cmd->loop_count = 0; // infinite
        }
    } else if (strcmp(cmd_str, "label") == 0) {
        cmd->type = C64_SCRIPT_CMD_LABEL;
        char *name = strtok_r(NULL, " \t", &saveptr);
        if (!name) {
            snprintf(script->error_msg, sizeof(script->error_msg), "label requires name");
            script->error_line = line_number;
            return false;
        }
        strncpy(cmd->arg1, name, sizeof(cmd->arg1) - 1);
    } else if (strcmp(cmd_str, "goto") == 0) {
        cmd->type = C64_SCRIPT_CMD_GOTO;
        char *name = strtok_r(NULL, " \t", &saveptr);
        if (!name) {
            snprintf(script->error_msg, sizeof(script->error_msg), "goto requires label name");
            script->error_line = line_number;
            return false;
        }
        strncpy(cmd->arg1, name, sizeof(cmd->arg1) - 1);
    } else {
        snprintf(script->error_msg, sizeof(script->error_msg), "Unknown command: %s", cmd_str);
        script->error_line = line_number;
        return false;
    }

    script->num_commands++;
    return true;
}

c64_script_t *c64_script_parse_string(const char *content)
{
    if (!content) {
        return NULL;
    }

    c64_script_t *script = calloc(1, sizeof(c64_script_t));
    if (!script) {
        return NULL;
    }

    script->capacity = INITIAL_CAPACITY;
    script->commands = calloc(script->capacity, sizeof(c64_script_command_t));
    if (!script->commands) {
        free(script);
        return NULL;
    }

    // Parse line by line
    char *content_copy = strdup(content);
    if (!content_copy) {
        c64_script_free(script);
        return NULL;
    }

    char *line = content_copy;
    char *next_line = NULL;
    int line_number = 1;
    bool success = true;

    while (line && success) {
        // Find end of line
        next_line = strchr(line, '\n');
        if (next_line) {
            *next_line = '\0';
            next_line++;
        }

        success = parse_line(line, line_number, script);
        line = next_line;
        line_number++;
    }

    free(content_copy);

    if (!success) {
        C64_LOG_ERROR(MACRO_LOG_PREFIX "Parse error at line %d: %s", script->error_line, script->error_msg);
        c64_script_free(script);
        return NULL;
    }

    C64_LOG_INFO(MACRO_LOG_PREFIX "Parsed %zu commands", script->num_commands);
    return script;
}

c64_script_t *c64_script_parse_file(const char *file_path)
{
    if (!file_path) {
        return NULL;
    }

    FILE *file = fopen(file_path, "r");
    if (!file) {
        C64_LOG_ERROR(MACRO_LOG_PREFIX "Failed to open file: %s", file_path);
        return NULL;
    }

    // Read entire file into memory
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 1024 * 1024) { // Max 1MB
        C64_LOG_ERROR(MACRO_LOG_PREFIX "Invalid file size: %ld", file_size);
        fclose(file);
        return NULL;
    }

    char *content = malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    size_t read = fread(content, 1, file_size, file);
    content[read] = '\0';
    fclose(file);

    c64_script_t *script = c64_script_parse_string(content);
    free(content);

    return script;
}

void c64_script_free(c64_script_t *script)
{
    if (!script) {
        return;
    }

    free(script->commands);
    free(script);
}

size_t c64_script_get_command_count(c64_script_t *script)
{
    if (!script) {
        return 0;
    }
    return script->num_commands;
}

const char *c64_script_get_error(c64_script_t *script)
{
    if (!script) {
        return "Invalid script";
    }
    return script->error_msg[0] ? script->error_msg : NULL;
}

int c64_script_get_error_line(c64_script_t *script)
{
    if (!script) {
        return 0;
    }
    return script->error_line;
}
