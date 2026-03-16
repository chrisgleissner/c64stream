/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/
#include "c64-automation-hvsc.h"
#include "c64-logging.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#endif

#define HVSC_LOG_PREFIX "[c64-automation] "

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[64];
} c64_md5_ctx;

static void c64_md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t x[16];

    for (int i = 0, j = 0; i < 16; i++, j += 4) {
        x[i] = (uint32_t)block[j] | ((uint32_t)block[j + 1] << 8) | ((uint32_t)block[j + 2] << 16) |
               ((uint32_t)block[j + 3] << 24);
    }

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define FF(a, b, c, d, x, s, ac)                                                                                 \
    {                                                                                                            \
        (a) += F((b), (c), (d)) + (x) + (uint32_t)(ac);                                                          \
        (a) = ROTATE_LEFT((a), (s));                                                                             \
        (a) += (b);                                                                                              \
    }
#define GG(a, b, c, d, x, s, ac)                                                                                 \
    {                                                                                                            \
        (a) += G((b), (c), (d)) + (x) + (uint32_t)(ac);                                                          \
        (a) = ROTATE_LEFT((a), (s));                                                                             \
        (a) += (b);                                                                                              \
    }
#define HH(a, b, c, d, x, s, ac)                                                                                 \
    {                                                                                                            \
        (a) += H((b), (c), (d)) + (x) + (uint32_t)(ac);                                                          \
        (a) = ROTATE_LEFT((a), (s));                                                                             \
        (a) += (b);                                                                                              \
    }
#define II(a, b, c, d, x, s, ac)                                                                                 \
    {                                                                                                            \
        (a) += I((b), (c), (d)) + (x) + (uint32_t)(ac);                                                          \
        (a) = ROTATE_LEFT((a), (s));                                                                             \
        (a) += (b);                                                                                              \
    }

    FF(a, b, c, d, x[0], 7, 0xd76aa478);
    FF(d, a, b, c, x[1], 12, 0xe8c7b756);
    FF(c, d, a, b, x[2], 17, 0x242070db);
    FF(b, c, d, a, x[3], 22, 0xc1bdceee);
    FF(a, b, c, d, x[4], 7, 0xf57c0faf);
    FF(d, a, b, c, x[5], 12, 0x4787c62a);
    FF(c, d, a, b, x[6], 17, 0xa8304613);
    FF(b, c, d, a, x[7], 22, 0xfd469501);
    FF(a, b, c, d, x[8], 7, 0x698098d8);
    FF(d, a, b, c, x[9], 12, 0x8b44f7af);
    FF(c, d, a, b, x[10], 17, 0xffff5bb1);
    FF(b, c, d, a, x[11], 22, 0x895cd7be);
    FF(a, b, c, d, x[12], 7, 0x6b901122);
    FF(d, a, b, c, x[13], 12, 0xfd987193);
    FF(c, d, a, b, x[14], 17, 0xa679438e);
    FF(b, c, d, a, x[15], 22, 0x49b40821);

    GG(a, b, c, d, x[1], 5, 0xf61e2562);
    GG(d, a, b, c, x[6], 9, 0xc040b340);
    GG(c, d, a, b, x[11], 14, 0x265e5a51);
    GG(b, c, d, a, x[0], 20, 0xe9b6c7aa);
    GG(a, b, c, d, x[5], 5, 0xd62f105d);
    GG(d, a, b, c, x[10], 9, 0x02441453);
    GG(c, d, a, b, x[15], 14, 0xd8a1e681);
    GG(b, c, d, a, x[4], 20, 0xe7d3fbc8);
    GG(a, b, c, d, x[9], 5, 0x21e1cde6);
    GG(d, a, b, c, x[14], 9, 0xc33707d6);
    GG(c, d, a, b, x[3], 14, 0xf4d50d87);
    GG(b, c, d, a, x[8], 20, 0x455a14ed);
    GG(a, b, c, d, x[13], 5, 0xa9e3e905);
    GG(d, a, b, c, x[2], 9, 0xfcefa3f8);
    GG(c, d, a, b, x[7], 14, 0x676f02d9);
    GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    HH(a, b, c, d, x[5], 4, 0xfffa3942);
    HH(d, a, b, c, x[8], 11, 0x8771f681);
    HH(c, d, a, b, x[11], 16, 0x6d9d6122);
    HH(b, c, d, a, x[14], 23, 0xfde5380c);
    HH(a, b, c, d, x[1], 4, 0xa4beea44);
    HH(d, a, b, c, x[4], 11, 0x4bdecfa9);
    HH(c, d, a, b, x[7], 16, 0xf6bb4b60);
    HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    HH(a, b, c, d, x[13], 4, 0x289b7ec6);
    HH(d, a, b, c, x[0], 11, 0xeaa127fa);
    HH(c, d, a, b, x[3], 16, 0xd4ef3085);
    HH(b, c, d, a, x[6], 23, 0x04881d05);
    HH(a, b, c, d, x[9], 4, 0xd9d4d039);
    HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
    HH(b, c, d, a, x[2], 23, 0xc4ac5665);

    II(a, b, c, d, x[0], 6, 0xf4292244);
    II(d, a, b, c, x[7], 10, 0x432aff97);
    II(c, d, a, b, x[14], 15, 0xab9423a7);
    II(b, c, d, a, x[5], 21, 0xfc93a039);
    II(a, b, c, d, x[12], 6, 0x655b59c3);
    II(d, a, b, c, x[3], 10, 0x8f0ccc92);
    II(c, d, a, b, x[10], 15, 0xffeff47d);
    II(b, c, d, a, x[1], 21, 0x85845dd1);
    II(a, b, c, d, x[8], 6, 0x6fa87e4f);
    II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    II(c, d, a, b, x[6], 15, 0xa3014314);
    II(b, c, d, a, x[13], 21, 0x4e0811a1);
    II(a, b, c, d, x[4], 6, 0xf7537e82);
    II(d, a, b, c, x[11], 10, 0xbd3af235);
    II(c, d, a, b, x[2], 15, 0x2ad7d2bb);
    II(b, c, d, a, x[9], 21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;

#undef F
#undef G
#undef H
#undef I
#undef ROTATE_LEFT
#undef FF
#undef GG
#undef HH
#undef II
}

static void c64_md5_init(c64_md5_ctx *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

static void c64_md5_update(c64_md5_ctx *ctx, const uint8_t *input, size_t len)
{
    size_t index = (size_t)((ctx->count >> 3) & 0x3F);
    ctx->count += ((uint64_t)len << 3);
    size_t part_len = 64 - index;
    size_t i = 0;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], input, part_len);
        c64_md5_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            c64_md5_transform(ctx->state, &input[i]);
        }
        index = 0;
    }

    memcpy(&ctx->buffer[index], &input[i], len - i);
}

static void c64_md5_final(c64_md5_ctx *ctx, uint8_t digest[16])
{
    static const uint8_t padding[64] = {0x80};
    uint8_t bits[8];

    for (int i = 0; i < 8; i++) {
        bits[i] = (uint8_t)((ctx->count >> (i * 8)) & 0xFF);
    }

    size_t index = (size_t)((ctx->count >> 3) & 0x3f);
    size_t pad_len = (index < 56) ? (56 - index) : (120 - index);
    c64_md5_update(ctx, padding, pad_len);
    c64_md5_update(ctx, bits, 8);

    for (int i = 0; i < 4; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] & 0xFF);
        digest[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 3] = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
    }
}

bool c64_hvsc_md5_file_hex(const char *path, char out_hex[33])
{
    if (!path || !out_hex) {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }

    c64_md5_ctx ctx;
    c64_md5_init(&ctx);

    uint8_t buffer[4096];
    size_t read_bytes = 0;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        c64_md5_update(&ctx, buffer, read_bytes);
    }

    fclose(file);

    uint8_t digest[16];
    c64_md5_final(&ctx, digest);

    for (int i = 0; i < 16; i++) {
        snprintf(out_hex + (i * 2), 3, "%02x", digest[i]);
    }
    out_hex[32] = '\0';
    return true;
}

static bool parse_songlength_value(const char *value, double *out_seconds)
{
    if (!value || !out_seconds) {
        return false;
    }

    while (*value && isspace((unsigned char)*value)) {
        value++;
    }

    int minutes = 0;
    int seconds = 0;
    int millis = 0;
    int millis_digits = 0;

    if (!isdigit((unsigned char)*value)) {
        return false;
    }

    while (isdigit((unsigned char)*value)) {
        minutes = minutes * 10 + (*value - '0');
        value++;
    }

    if (*value != ':') {
        return false;
    }
    value++;

    if (!isdigit((unsigned char)*value)) {
        return false;
    }

    seconds = (*value - '0');
    value++;
    if (isdigit((unsigned char)*value)) {
        seconds = seconds * 10 + (*value - '0');
        value++;
    }

    if (*value == '.') {
        value++;
        while (isdigit((unsigned char)*value) && millis_digits < 3) {
            millis = millis * 10 + (*value - '0');
            millis_digits++;
            value++;
        }
        while (millis_digits < 3) {
            millis *= 10;
            millis_digits++;
        }
    }

    if (minutes < 0 || seconds < 0) {
        return false;
    }

    double total_seconds = (double)minutes * 60.0 + (double)seconds + ((double)millis / 1000.0);
    if (total_seconds < 1.0) {
        total_seconds = 1.0;
    }

    *out_seconds = total_seconds;
    return true;
}

static int songlength_entry_cmp(const void *left, const void *right)
{
    const c64_hvsc_songlength_entry_t *l = (const c64_hvsc_songlength_entry_t *)left;
    const c64_hvsc_songlength_entry_t *r = (const c64_hvsc_songlength_entry_t *)right;
    return strcmp(l->md5, r->md5);
}

void c64_hvsc_songlength_db_clear(c64_hvsc_songlength_db_t *db)
{
    if (!db) {
        return;
    }

    free(db->entries);
    db->entries = NULL;
    db->count = 0;
    db->loaded = false;
    db->source_path[0] = '\0';
}

bool c64_hvsc_songlength_db_load(c64_hvsc_songlength_db_t *db, const char *path)
{
    if (!db || !path || path[0] == '\0') {
        return false;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }

    c64_hvsc_songlength_entry_t *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;
    char line[1024];

    while (fgets(line, sizeof(line), file)) {
        char *cursor = line;
        while (*cursor && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0' || *cursor == ';' || *cursor == '[') {
            continue;
        }

        char *equals = strchr(cursor, '=');
        if (!equals) {
            continue;
        }

        *equals = '\0';
        char *key = cursor;
        char *value = equals + 1;

        while (*key && isspace((unsigned char)*key)) {
            key++;
        }
        char *end_key = key + strlen(key);
        while (end_key > key && isspace((unsigned char)end_key[-1])) {
            end_key--;
        }
        *end_key = '\0';

        if (strlen(key) != 32) {
            continue;
        }

        for (char *p = key; *p; p++) {
            if (!isxdigit((unsigned char)*p)) {
                key = NULL;
                break;
            }
            *p = (char)tolower((unsigned char)*p);
        }
        if (!key) {
            continue;
        }

        while (*value && isspace((unsigned char)*value)) {
            value++;
        }
        if (*value == '\0') {
            continue;
        }

        char length_value[64];
        size_t idx = 0;
        while (value[idx] && !isspace((unsigned char)value[idx]) && idx < sizeof(length_value) - 1) {
            length_value[idx] = value[idx];
            idx++;
        }
        length_value[idx] = '\0';

        double seconds = 0.0;
        if (!parse_songlength_value(length_value, &seconds)) {
            continue;
        }

        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 1024 : capacity * 2;
            c64_hvsc_songlength_entry_t *new_entries =
                realloc(entries, new_capacity * sizeof(c64_hvsc_songlength_entry_t));
            if (!new_entries) {
                free(entries);
                fclose(file);
                return false;
            }
            entries = new_entries;
            capacity = new_capacity;
        }

        strncpy(entries[count].md5, key, sizeof(entries[count].md5) - 1);
        entries[count].md5[32] = '\0';
        entries[count].seconds = seconds;
        count++;
    }

    fclose(file);

    if (count == 0) {
        free(entries);
        return false;
    }

    qsort(entries, count, sizeof(c64_hvsc_songlength_entry_t), songlength_entry_cmp);

    free(db->entries);
    db->entries = entries;
    db->count = count;
    db->loaded = true;
    strncpy(db->source_path, path, sizeof(db->source_path) - 1);
    db->source_path[sizeof(db->source_path) - 1] = '\0';

    C64_LOG_INFO(HVSC_LOG_PREFIX "Loaded songlengths: %zu entries from %s", count, path);
    return true;
}

bool c64_hvsc_songlength_db_lookup(const c64_hvsc_songlength_db_t *db, const char *md5_hex, double *out_seconds)
{
    if (!db || !db->loaded || !md5_hex || !out_seconds) {
        return false;
    }

    c64_hvsc_songlength_entry_t key_entry = {0};
    strncpy(key_entry.md5, md5_hex, sizeof(key_entry.md5) - 1);
    c64_hvsc_songlength_entry_t *found =
        bsearch(&key_entry, db->entries, db->count, sizeof(c64_hvsc_songlength_entry_t), songlength_entry_cmp);
    if (!found) {
        return false;
    }

    *out_seconds = found->seconds;
    return true;
}

static bool songlengths_filename_matches(const char *name)
{
    if (!name) {
        return false;
    }

    if (strcasecmp(name, "Songlengths.md5") == 0 || strcasecmp(name, "Songlengths.txt") == 0) {
        return true;
    }

    return false;
}

static bool path_is_directory(const char *path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
#endif
}

static bool path_is_regular_file(const char *path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode);
#endif
}

static bool strip_trailing_separators(char *path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

    size_t len = strlen(path);
    while (len > 1) {
        char ch = path[len - 1];
        if (ch != '/' && ch != '\\') {
            break;
        }
        path[len - 1] = '\0';
        len--;
    }

    return len > 0;
}

bool c64_hvsc_find_songlengths_file_local(const char *root_path, char *out_path, size_t out_size)
{
    if (!root_path || !out_path || out_size == 0) {
        return false;
    }

    char normalized_root[C64_AUTOMATION_PATH_MAX];
    strncpy(normalized_root, root_path, sizeof(normalized_root) - 1);
    normalized_root[sizeof(normalized_root) - 1] = '\0';
    if (!strip_trailing_separators(normalized_root) || !path_is_directory(normalized_root)) {
        return false;
    }

    static const char *candidates[] = {
#ifdef _WIN32
        "DOCUMENTS\\Songlengths.txt",
        "DOCUMENTS\\Songlengths.md5",
#else
        "DOCUMENTS/Songlengths.txt",
        "DOCUMENTS/Songlengths.md5",
#endif
    };

    for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); i++) {
        char candidate[C64_AUTOMATION_PATH_MAX];
        int written = snprintf(candidate, sizeof(candidate), "%s/%s", normalized_root, candidates[i]);
        if (written < 0 || (size_t)written >= sizeof(candidate)) {
            continue;
        }
        if (path_is_regular_file(candidate)) {
            strncpy(out_path, candidate, out_size - 1);
            out_path[out_size - 1] = '\0';
            C64_LOG_INFO(HVSC_LOG_PREFIX "Found songlengths: %s", out_path);
            return true;
        }
    }

    return false;
}
