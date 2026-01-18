/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script-vm-internal.h"
#include "c64-logging.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

bool c64script_debug_logging_enabled(void)
{
    if (c64_debug_logging) {
        return true;
    }
    const char *env = getenv("C64SCRIPT_DEBUG_LOGS");
    if (!env || env[0] == '\0' || strcmp(env, "0") == 0) {
        return false;
    }
    return true;
}

static double wallclock_now_seconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static bool parse_fixed_digits(const char *s, size_t len, int *out)
{
    int value = 0;
    if (!s || !out || len == 0) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        value = value * 10 + (s[i] - '0');
    }
    *out = value;
    return true;
}

#ifdef _WIN32
static time_t timegm_compat(struct tm *tm_utc)
{
    return _mkgmtime(tm_utc);
}
#else
static time_t timegm_compat(struct tm *tm_utc)
{
    return timegm(tm_utc);
}
#endif

bool parse_wallclock_target(const char *s, double *out_epoch_seconds)
{
    if (!s || !out_epoch_seconds) {
        return false;
    }

    size_t n = strlen(s);

    // "HH:MM" or "HH:MM:SS" (local time; tomorrow if already passed)
    if (n == 5 || n == 8) {
        int hh = 0, mm = 0, ss = 0;
        if (!parse_fixed_digits(s + 0, 2, &hh) || s[2] != ':' || !parse_fixed_digits(s + 3, 2, &mm)) {
            return false;
        }
        if (n == 8) {
            if (s[5] != ':' || !parse_fixed_digits(s + 6, 2, &ss)) {
                return false;
            }
        }
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
            return false;
        }

        time_t now_t = time(NULL);
        struct tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &now_t);
#else
        localtime_r(&now_t, &local_tm);
#endif
        local_tm.tm_hour = hh;
        local_tm.tm_min = mm;
        local_tm.tm_sec = ss;
        time_t target = mktime(&local_tm);
        if (target == (time_t)-1) {
            return false;
        }

        if ((double)target <= wallclock_now_seconds()) {
            target += 24 * 60 * 60;
        }

        *out_epoch_seconds = (double)target;
        return true;
    }

    // "YYYY-MM-DD HH:MM:SS" (local time)
    if (n == 19 && s[4] == '-' && s[7] == '-' && s[10] == ' ' && s[13] == ':' && s[16] == ':') {
        int year = 0, mon = 0, day = 0, hh = 0, mm = 0, ss = 0;
        if (!parse_fixed_digits(s + 0, 4, &year) || !parse_fixed_digits(s + 5, 2, &mon) ||
            !parse_fixed_digits(s + 8, 2, &day) || !parse_fixed_digits(s + 11, 2, &hh) ||
            !parse_fixed_digits(s + 14, 2, &mm) || !parse_fixed_digits(s + 17, 2, &ss)) {
            return false;
        }

        struct tm tm_local = {0};
        tm_local.tm_year = year - 1900;
        tm_local.tm_mon = mon - 1;
        tm_local.tm_mday = day;
        tm_local.tm_hour = hh;
        tm_local.tm_min = mm;
        tm_local.tm_sec = ss;
        time_t target = mktime(&tm_local);
        if (target == (time_t)-1) {
            return false;
        }
        *out_epoch_seconds = (double)target;
        return true;
    }

    // ISO-8601 "YYYY-MM-DDTHH:MM:SS[.fff][Z|±HH:MM]"
    const char *t_pos = strchr(s, 'T');
    if (!t_pos) {
        return false;
    }

    if ((size_t)(t_pos - s) != 10 || s[4] != '-' || s[7] != '-') {
        return false;
    }

    int year = 0, mon = 0, day = 0;
    if (!parse_fixed_digits(s + 0, 4, &year) || !parse_fixed_digits(s + 5, 2, &mon) ||
        !parse_fixed_digits(s + 8, 2, &day)) {
        return false;
    }

    const char *time_part = t_pos + 1;
    if (strlen(time_part) < 8 || time_part[2] != ':' || time_part[5] != ':') {
        return false;
    }

    int hh = 0, mm = 0, ss = 0;
    if (!parse_fixed_digits(time_part + 0, 2, &hh) || !parse_fixed_digits(time_part + 3, 2, &mm) ||
        !parse_fixed_digits(time_part + 6, 2, &ss)) {
        return false;
    }

    const char *rest = time_part + 8;

    // Optional fractional seconds
    if (*rest == '.') {
        rest++;
        while (*rest >= '0' && *rest <= '9') {
            rest++;
        }
    }

    bool has_tz = false;
    int tz_sign = 1;
    int tz_hh = 0, tz_mm = 0;
    if (*rest == 'Z') {
        has_tz = true;
        tz_sign = 1;
        tz_hh = 0;
        tz_mm = 0;
        rest++;
    } else if (*rest == '+' || *rest == '-') {
        has_tz = true;
        tz_sign = (*rest == '-') ? -1 : 1;
        rest++;
        if (!parse_fixed_digits(rest + 0, 2, &tz_hh) || rest[2] != ':' || !parse_fixed_digits(rest + 3, 2, &tz_mm)) {
            return false;
        }
        rest += 5;
    }

    if (*rest != '\0') {
        return false;
    }

    struct tm tm_val = {0};
    tm_val.tm_year = year - 1900;
    tm_val.tm_mon = mon - 1;
    tm_val.tm_mday = day;
    tm_val.tm_hour = hh;
    tm_val.tm_min = mm;
    tm_val.tm_sec = ss;

    time_t base;
    if (has_tz) {
        base = timegm_compat(&tm_val);
        if (base == (time_t)-1) {
            return false;
        }
        int offset_seconds = tz_sign * ((tz_hh * 60 + tz_mm) * 60);
        *out_epoch_seconds = (double)base - (double)offset_seconds;
        return true;
    }

    base = mktime(&tm_val);
    if (base == (time_t)-1) {
        return false;
    }
    *out_epoch_seconds = (double)base;
    return true;
}

bool format_current_time(c64script_runtime_t *runtime, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }

    time_t now = runtime && runtime->override_time_enabled ? runtime->override_time : time(NULL);
    struct tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif
    return strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &local_tm) > 0;
}

bool is_c64u_path(const char *path, const char **out_c64u_path)
{
    if (!path || !out_c64u_path) {
        return false;
    }

    const char *prefix = "c64u:";
    size_t prefix_len = strlen(prefix);
    if (strlen(path) < prefix_len) {
        return false;
    }
    if (strncasecmp(path, prefix, prefix_len) != 0) {
        return false;
    }

    *out_c64u_path = path + prefix_len;
    return true;
}

bool load_binary_file(const char *path, uint8_t **out_data, size_t *out_size, char *error_msg, size_t error_size)
{
    if (!path || !out_data || !out_size) {
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to open file: %s", path);
        }
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to seek file");
        }
        return false;
    }

    long fsize = ftell(f);
    if (fsize <= 0 || fsize > (long)(16 * 1024 * 1024)) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "File too large");
        }
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to seek file");
        }
        return false;
    }

    uint8_t *data = malloc((size_t)fsize);
    if (!data) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Out of memory");
        }
        return false;
    }

    size_t read_count = fread(data, 1, (size_t)fsize, f);
    fclose(f);
    if (read_count != (size_t)fsize) {
        free(data);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to read file");
        }
        return false;
    }

    *out_data = data;
    *out_size = (size_t)fsize;
    return true;
}

bool load_text_file(const char *path, char **out_content, char *error_msg, size_t error_size)
{
    if (!path || !out_content) {
        return false;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to open file: %s", path);
        }
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to seek file");
        }
        return false;
    }

    long fsize = ftell(f);
    if (fsize < 0 || fsize > (long)(16 * 1024 * 1024)) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, fsize < 0 ? "Failed to get file size" : "File too large");
        }
        return false;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to seek file");
        }
        return false;
    }

    char *content = malloc((size_t)fsize + 1);
    if (!content) {
        fclose(f);
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Out of memory");
        }
        return false;
    }

    size_t read_count = fread(content, 1, (size_t)fsize, f);
    fclose(f);
    content[read_count] = '\0';

    *out_content = content;
    return true;
}

bool write_file(const char *path, const char *content, bool truncate, char *error_msg, size_t error_size)
{
    if (!path || !content) {
        return false;
    }

    const char *mode = truncate ? "w" : "a";
    FILE *f = fopen(path, mode);
    if (!f) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to open file: %s", path);
        }
        return false;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    if (written != len) {
        if (error_msg && error_size > 0) {
            snprintf(error_msg, error_size, "Failed to write to file");
        }
        return false;
    }

    return true;
}

const char *file_extension_lower(const char *path)
{
    if (!path) {
        return "";
    }
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) {
        return "";
    }
    return dot + 1;
}

bool require_number(c64script_runtime_t *runtime, const c64script_value_t *value, const char *what)
{
    if (value->type == VALUE_NUMBER) {
        return true;
    }
    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (%s)", what);
    return false;
}

bool require_string(c64script_runtime_t *runtime, const c64script_value_t *value, const char *what)
{
    if (value->type == VALUE_STRING) {
        return true;
    }
    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (%s)", what);
    return false;
}

bool compare_values(c64script_runtime_t *runtime, const c64script_value_t *a, const c64script_value_t *b, int *out_cmp,
                    const char *what)
{
    if (!out_cmp) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "Invalid comparison output");
        return false;
    }

    if (a->type == VALUE_NUMBER && b->type == VALUE_NUMBER) {
        if (a->as.number < b->as.number) {
            *out_cmp = -1;
        } else if (a->as.number > b->as.number) {
            *out_cmp = 1;
        } else {
            *out_cmp = 0;
        }
        return true;
    }

    if (a->type == VALUE_STRING && b->type == VALUE_STRING) {
        const char *a_str = a->as.string ? a->as.string : "";
        const char *b_str = b->as.string ? b->as.string : "";
        int cmp = strcmp(a_str, b_str);
        *out_cmp = (cmp < 0) ? -1 : (cmp > 0 ? 1 : 0);
        return true;
    }

    snprintf(runtime->error_msg, sizeof(runtime->error_msg), "TYPE MISMATCH (%s)", what);
    return false;
}

bool number_to_int(c64script_runtime_t *runtime, const c64script_value_t *value, int *out, const char *what)
{
    if (!require_number(runtime, value, what)) {
        return false;
    }

    double truncated = trunc(value->as.number);
    if (!isfinite(truncated) || truncated > INT_MAX || truncated < INT_MIN) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
        return false;
    }

    *out = (int)truncated;
    return true;
}

bool number_to_uint16(c64script_runtime_t *runtime, const c64script_value_t *value, uint16_t *out, const char *what)
{
    int temp = 0;
    if (!number_to_int(runtime, value, &temp, what)) {
        return false;
    }
    if (temp < 0 || temp > 0xFFFF) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
        return false;
    }
    *out = (uint16_t)temp;
    return true;
}

bool number_to_uint8(c64script_runtime_t *runtime, const c64script_value_t *value, uint8_t *out, const char *what)
{
    int temp = 0;
    if (!number_to_int(runtime, value, &temp, what)) {
        return false;
    }
    if (temp < 0 || temp > 0xFF) {
        snprintf(runtime->error_msg, sizeof(runtime->error_msg), "ILLEGAL QUANTITY");
        return false;
    }
    *out = (uint8_t)temp;
    return true;
}

double wait_unit_multiplier(c64script_wait_unit_t unit)
{
    switch (unit) {
    case C64SCRIPT_WAIT_UNIT_MS:
        return 1.0;
    case C64SCRIPT_WAIT_UNIT_S:
        return 1000.0;
    case C64SCRIPT_WAIT_UNIT_M:
        return 60000.0;
    case C64SCRIPT_WAIT_UNIT_H:
        return 3600000.0;
    case C64SCRIPT_WAIT_UNIT_D:
        return 86400000.0;
    default:
        return 1000.0;
    }
}
