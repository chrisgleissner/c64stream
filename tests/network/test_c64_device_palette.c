/* Follow device palette: the pure helpers (setting parser, VPL parser with the
 * firmware's rules, file name safety, cache key) and the network helpers
 * against an in-process fake Ultimate (REST + FTP with network password).
 * Runs in CI on Linux, macOS and Windows. */

#include "c64-device-palette.h"
#include "fake_ultimate.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#endif

#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                  \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

bool c64_debug_logging = false;

// The Ultimate's built-in palette in VPL form, and its 0xFFBBGGRR values.
static const char *const PEPTO_VPL = "# test palette\n"
                                     "00 00 00\nF7 F7 F7\n8D 2F 34\n6A D4 CD\n98 35 A4\n4C B4 42\n2C 29 B1\n"
                                     "EF EF 5D\n98 4E 20\n5B 38 00\nD1 67 6D\n4A 4A 4A\n7B 7B 7B\n9F EF 93\n"
                                     "6D 6A EF\nB2 B2 B2\n";

static bool test_interval_clamp(void)
{
    CHECK(c64_device_palette_clamp_interval(0) == 1000);
    CHECK(c64_device_palette_clamp_interval(-5) == 1000);
    CHECK(c64_device_palette_clamp_interval(20) == C64_DEVICE_PALETTE_POLL_MIN_MS);
    CHECK(c64_device_palette_clamp_interval(1500) == 1500);
    CHECK(c64_device_palette_clamp_interval(99999) == C64_DEVICE_PALETTE_POLL_MAX_MS);
    return true;
}

static bool test_setting_parser(void)
{
    char name[64];
    CHECK(c64_device_palette_parse_setting("{ \"U64 Specific Settings\" : { \"Palette Definition\" : { \"current\" : "
                                           "\"pepto.vpl\", \"presets\" : [ \"\" ] } }, \"errors\" : [] }",
                                           name, sizeof(name)));
    CHECK(!strcmp(name, "pepto.vpl"));
    // Empty value: the built-in palette.
    CHECK(c64_device_palette_parse_setting("{\"Palette Definition\":{\"current\":\"\"}}", name, sizeof(name)));
    CHECK(!strcmp(name, ""));
    // Escapes as the firmware writes them.
    CHECK(c64_device_palette_parse_setting("{\"Palette Definition\":{\"current\":\"a\\\"b\\\\c.vpl\"}}", name,
                                           sizeof(name)));
    CHECK(!strcmp(name, "a\"b\\c.vpl"));
    // A "current" belonging to another item does not count.
    CHECK(!c64_device_palette_parse_setting("{\"Other\":{\"current\":\"x\"}}", name, sizeof(name)));
    CHECK(!c64_device_palette_parse_setting("{\"Palette Definition\":{\"current\":42}}", name, sizeof(name)));
    CHECK(
        !c64_device_palette_parse_setting("{\"Palette Definition\":{\"current\":\"unterminated}", name, sizeof(name)));
    CHECK(!c64_device_palette_parse_setting("{\"Palette Definition\":{\"current\":\"too-long-for-buffer\"}}", name, 8));
    CHECK(!c64_device_palette_parse_setting(NULL, name, sizeof(name)));
    return true;
}

static bool test_name_safety(void)
{
    CHECK(c64_device_palette_name_is_safe("pepto.vpl"));
    CHECK(c64_device_palette_name_is_safe("My Palette (v2).vpl"));
    CHECK(!c64_device_palette_name_is_safe(""));
    CHECK(!c64_device_palette_name_is_safe(NULL));
    CHECK(!c64_device_palette_name_is_safe(".."));
    CHECK(!c64_device_palette_name_is_safe("../../etc/passwd"));
    CHECK(!c64_device_palette_name_is_safe("dir/file.vpl"));
    CHECK(!c64_device_palette_name_is_safe("dir\\file.vpl"));
    CHECK(!c64_device_palette_name_is_safe("C:file.vpl"));
    CHECK(!c64_device_palette_name_is_safe("bad\nname.vpl"));
    char long_name[80];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    CHECK(!c64_device_palette_name_is_safe(long_name));
    return true;
}

static bool test_vpl_parser_matches_firmware(void)
{
    uint32_t colors[16];
    CHECK(c64_device_palette_parse_vpl(PEPTO_VPL, strlen(PEPTO_VPL), colors));
    CHECK(colors[0] == 0xFF000000u);
    CHECK(colors[2] == 0xFF342F8Du); // 8D 2F 34 -> 0xFFBBGGRR
    CHECK(colors[15] == 0xFFB2B2B2u);

    // Comments, blank lines, CRLF, lower case, a fourth (dither) column.
    const char *vice = "# VICE palette\r\n\r\n# comment only\r\n"
                       "00 00 00 0\r\nff ff ff 1 # white\r\n88 39 32 2\r\n67 b6 bd 3\r\n8b 3f 96 4\r\n"
                       "55 a0 49 5\r\n40 31 8d 6\r\nbf ce 72 7\r\n8b 54 29 8\r\n57 42 00 9\r\nb8 69 62 a\r\n"
                       "50 50 50 b\r\n78 78 78 c\r\n94 e0 89 d\r\n78 69 c4 e\r\n9f 9f 9f f\r\n";
    CHECK(c64_device_palette_parse_vpl(vice, strlen(vice), colors));
    CHECK(colors[1] == 0xFFFFFFFFu);
    CHECK(colors[14] == 0xFFC46978u);

    // The firmware stops at the first non-colour line: 15 colours fail.
    char broken[1024];
    snprintf(broken, sizeof(broken), "%.*sNAME: x\n", (int)(strstr(PEPTO_VPL, "B2 B2 B2") - PEPTO_VPL), PEPTO_VPL);
    CHECK(!c64_device_palette_parse_vpl(broken, strlen(broken), colors));
    // Too short and too long, as in FileTypePalette::parseVplFile.
    CHECK(!c64_device_palette_parse_vpl("00 00", 5, colors));
    static char huge[C64_DEVICE_PALETTE_VPL_MAX + 2];
    memset(huge, '#', sizeof(huge) - 1);
    CHECK(!c64_device_palette_parse_vpl(huge, sizeof(huge) - 1, colors));
    // Extra colours after 16 are ignored.
    char extra[1024];
    snprintf(extra, sizeof(extra), "%s12 34 56\n", PEPTO_VPL);
    CHECK(c64_device_palette_parse_vpl(extra, strlen(extra), colors));
    CHECK(colors[15] == 0xFFB2B2B2u);
    return true;
}

static bool test_cache_path(void)
{
    char path[256];
    CHECK(c64_device_palette_cache_path(path, sizeof(path), "/cache", "5D0464", "pepto.vpl", 169));
    CHECK(!strcmp(path, "/cache/5d0464-169-pepto.vpl"));
    // The same file name with another size is a different cache entry.
    char other[256];
    CHECK(c64_device_palette_cache_path(other, sizeof(other), "/cache", "5D0464", "pepto.vpl", 170));
    CHECK(strcmp(path, other) != 0);
    // Device keys are reduced to a safe file name part; unsafe names refused.
    CHECK(c64_device_palette_cache_path(path, sizeof(path), "/cache", "192.168.1.13", "a.vpl", 1));
    CHECK(!strcmp(path, "/cache/192-168-1-13-1-a.vpl"));
    CHECK(!c64_device_palette_cache_path(path, sizeof(path), "/cache", "dev", "../x.vpl", 1));
    CHECK(!c64_device_palette_cache_path(path, 10, "/cache", "dev", "a.vpl", 1));
    return true;
}

static bool test_describe(void)
{
    char text[256];
    c64_device_palette_describe(C64_PALETTE_SOURCE_DEVICE_FILE, C64_PALETTE_ERROR_NONE, "pepto.vpl", text,
                                sizeof(text));
    CHECK(strstr(text, "pepto.vpl"));
    c64_device_palette_describe(C64_PALETTE_SOURCE_ERROR, C64_PALETTE_ERROR_PASSWORD, NULL, text, sizeof(text));
    CHECK(strstr(text, "password"));
    for (int e = C64_PALETTE_ERROR_NONE; e <= C64_PALETTE_ERROR_CACHE; e++) {
        CHECK(c64_device_palette_error_text((c64_palette_error_t)e) != NULL);
    }
    return true;
}

// --- Network helpers against the fake device -------------------------------

static bool test_fetch_setting_and_size(void)
{
    fake_ultimate_t fake = {.has_setting = true, .ftp_enabled = true};
    snprintf(fake.setting, sizeof(fake.setting), "pepto.vpl");
    CHECK(fake_start(&fake));
    fake_put_file(&fake, "pepto.vpl", PEPTO_VPL);
    char host[64];
    fake_rest_host(&fake, host, sizeof(host));

    char name[64];
    c64_palette_error_t error = C64_PALETTE_ERROR_CACHE;
    bool ok = c64_device_palette_fetch_setting(host, "", 1000, NULL, name, sizeof(name), &error);
    uint64_t size = 0;
    c64_palette_error_t size_error = C64_PALETTE_ERROR_CACHE;
    const bool size_ok = c64_device_palette_fetch_size(host, "", "pepto.vpl", 1000, NULL, &size, &size_error);
    c64_palette_error_t missing_error = C64_PALETTE_ERROR_NONE;
    const bool missing = c64_device_palette_fetch_size(host, "", "missing.vpl", 1000, NULL, &size, &missing_error);
    // Only a 404 is a missing file; a server error is not.
    FAKE_SET(&fake, info_status, 500);
    c64_palette_error_t server_error = C64_PALETTE_ERROR_NONE;
    const bool server = c64_device_palette_fetch_size(host, "", "pepto.vpl", 1000, NULL, &size, &server_error);
    FAKE_SET(&fake, info_status, 403);
    c64_palette_error_t denied_error = C64_PALETTE_ERROR_NONE;
    const bool denied = c64_device_palette_fetch_size(host, "", "pepto.vpl", 1000, NULL, &size, &denied_error);
    fake_stop(&fake);
    c64_palette_error_t gone_error = C64_PALETTE_ERROR_NONE;
    const bool gone = c64_device_palette_fetch_size(host, "", "pepto.vpl", 300, NULL, &size, &gone_error);
    CHECK(ok && error == C64_PALETTE_ERROR_NONE && !strcmp(name, "pepto.vpl"));
    CHECK(size_ok && size == strlen(PEPTO_VPL) && size_error == C64_PALETTE_ERROR_NONE);
    CHECK(!missing && missing_error == C64_PALETTE_ERROR_FILE_NOT_FOUND);
    CHECK(!server && server_error == C64_PALETTE_ERROR_UNREACHABLE);
    CHECK(!denied && denied_error == C64_PALETTE_ERROR_PASSWORD);
    CHECK(!gone && gone_error == C64_PALETTE_ERROR_UNREACHABLE);
    return true;
}

static bool test_password_is_sent_and_checked(void)
{
    fake_ultimate_t fake = {.has_setting = true, .ftp_enabled = true};
    snprintf(fake.password, sizeof(fake.password), "s3cret");
    snprintf(fake.setting, sizeof(fake.setting), "pepto.vpl");
    CHECK(fake_start(&fake));
    fake_put_file(&fake, "pepto.vpl", PEPTO_VPL);
    char host[64];
    fake_rest_host(&fake, host, sizeof(host));
    char name[64];
    char data[C64_DEVICE_PALETTE_VPL_MAX + 1];
    size_t length = 0;
    c64_palette_error_t rest_wrong = C64_PALETTE_ERROR_NONE;
    c64_palette_error_t ftp_wrong = C64_PALETTE_ERROR_NONE;
    c64_palette_error_t rest_right = C64_PALETTE_ERROR_CACHE;
    c64_palette_error_t ftp_right = C64_PALETTE_ERROR_CACHE;
    const bool a = c64_device_palette_fetch_setting(host, "wrong", 1000, NULL, name, sizeof(name), &rest_wrong);
    const bool b = c64_device_palette_download("127.0.0.1", fake.ftp_port, "wrong", "pepto.vpl", 2000, NULL, data,
                                               sizeof(data), &length, &ftp_wrong);
    const bool c = c64_device_palette_fetch_setting(host, "s3cret", 1000, NULL, name, sizeof(name), &rest_right);
    const bool d = c64_device_palette_download("127.0.0.1", fake.ftp_port, "s3cret", "pepto.vpl", 2000, NULL, data,
                                               sizeof(data), &length, &ftp_right);
    const long rejected = FAKE_COUNT(&fake, ftp_logins_rejected);
    fake_stop(&fake);
    CHECK(!a && rest_wrong == C64_PALETTE_ERROR_PASSWORD);
    CHECK(!b && ftp_wrong == C64_PALETTE_ERROR_PASSWORD && rejected >= 1);
    CHECK(c && rest_right == C64_PALETTE_ERROR_NONE);
    CHECK(d && ftp_right == C64_PALETTE_ERROR_NONE && length == strlen(PEPTO_VPL));
    CHECK(!memcmp(data, PEPTO_VPL, length));
    return true;
}

static bool test_download_errors(void)
{
    fake_ultimate_t fake = {.has_setting = false, .ftp_enabled = true};
    CHECK(fake_start(&fake));
    static char big[C64_DEVICE_PALETTE_VPL_MAX + 100];
    memset(big, 'x', sizeof(big) - 1);
    fake_put_file(&fake, "big.vpl", big);
    char host[64];
    fake_rest_host(&fake, host, sizeof(host));
    char name[64];
    char data[C64_DEVICE_PALETTE_VPL_MAX + 1];
    size_t length = 0;
    c64_palette_error_t no_setting = C64_PALETTE_ERROR_NONE;
    c64_palette_error_t not_found = C64_PALETTE_ERROR_NONE;
    c64_palette_error_t too_big = C64_PALETTE_ERROR_NONE;
    c64_palette_error_t bad_name = C64_PALETTE_ERROR_NONE;
    const bool a = c64_device_palette_fetch_setting(host, "", 1000, NULL, name, sizeof(name), &no_setting);
    const bool b = c64_device_palette_download("127.0.0.1", fake.ftp_port, "", "missing.vpl", 2000, NULL, data,
                                               sizeof(data), &length, &not_found);
    const bool c = c64_device_palette_download("127.0.0.1", fake.ftp_port, "", "big.vpl", 2000, NULL, data,
                                               sizeof(data), &length, &too_big);
    const bool d = c64_device_palette_download("127.0.0.1", fake.ftp_port, "", "../x.vpl", 2000, NULL, data,
                                               sizeof(data), &length, &bad_name);
    fake_stop(&fake);
    CHECK(!a && no_setting == C64_PALETTE_ERROR_NO_SETTING);
    CHECK(!b && not_found == C64_PALETTE_ERROR_FILE_NOT_FOUND);
    CHECK(!c && too_big == C64_PALETTE_ERROR_INVALID_FILE);
    CHECK(!d && bad_name == C64_PALETTE_ERROR_BAD_NAME);
    return true;
}

static bool test_unreachable_services(void)
{
    // Ports with nothing listening: REST and FTP report unreachable quickly.
    uint16_t rest_port = 0;
    uint16_t ftp_port = 0;
    socket_t a = fake_listen(&rest_port);
    socket_t b = fake_listen(&ftp_port);
    close(a);
    close(b);
    char host[64];
    snprintf(host, sizeof(host), "127.0.0.1:%u", rest_port);
    char name[64];
    char data[64];
    size_t length = 0;
    c64_palette_error_t rest = C64_PALETTE_ERROR_NONE;
    c64_palette_error_t ftp = C64_PALETTE_ERROR_NONE;
    CHECK(!c64_device_palette_fetch_setting(host, "", 1000, NULL, name, sizeof(name), &rest));
    CHECK(rest == C64_PALETTE_ERROR_UNREACHABLE);
    CHECK(!c64_device_palette_download("127.0.0.1", ftp_port, "", "a.vpl", 1000, NULL, data, sizeof(data), &length,
                                       &ftp));
    CHECK(ftp == C64_PALETTE_ERROR_FTP_UNREACHABLE);
    return true;
}

static bool test_setting_status_mapping(void)
{
    // 404 (or a body without the item): firmware without the setting. Any
    // other failure is a device that cannot answer right now.
    fake_ultimate_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.has_setting = true;
    CHECK(fake_start(&fake));
    char host[64];
    fake_rest_host(&fake, host, sizeof(host));
    char name[64];
    c64_palette_error_t missing = C64_PALETTE_ERROR_NONE;
    c64_palette_error_t busy = C64_PALETTE_ERROR_NONE;
    FAKE_SET(&fake, setting_status, 404);
    const bool missing_ok = c64_device_palette_fetch_setting(host, "", 1000, NULL, name, sizeof(name), &missing);
    FAKE_SET(&fake, setting_status, 503);
    const bool busy_ok = c64_device_palette_fetch_setting(host, "", 1000, NULL, name, sizeof(name), &busy);
    fake_stop(&fake);
    CHECK(!missing_ok && missing == C64_PALETTE_ERROR_NO_SETTING);
    CHECK(!busy_ok && busy == C64_PALETTE_ERROR_UNREACHABLE);
    return true;
}

static bool test_long_names_are_not_truncated(void)
{
    // A 63-byte name of characters that each escape to three bytes must reach
    // the device intact (REST and FTP), never as a truncated path.
    fake_ultimate_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.has_setting = true;
    fake.ftp_enabled = true;
    CHECK(fake_start(&fake));
    char name[C64_DEVICE_PALETTE_NAME_MAX];
    memset(name, ' ', sizeof(name) - 5);
    snprintf(name + sizeof(name) - 5, 5, ".vpl");
    CHECK(strlen(name) == C64_DEVICE_PALETTE_NAME_MAX - 1 && c64_device_palette_name_is_safe(name));
    fake_put_file(&fake, name, PEPTO_VPL);
    char host[64];
    fake_rest_host(&fake, host, sizeof(host));
    uint64_t size = 0;
    c64_palette_error_t error = C64_PALETTE_ERROR_CACHE;
    const bool size_ok = c64_device_palette_fetch_size(host, "", name, 1000, NULL, &size, &error);
    char data[C64_DEVICE_PALETTE_VPL_MAX + 1];
    size_t length = 0;
    const bool downloaded = c64_device_palette_download("127.0.0.1", fake.ftp_port, "", name, 2000, NULL, data,
                                                        sizeof(data), &length, NULL);
    fake_stop(&fake);
    CHECK(size_ok && size == strlen(PEPTO_VPL));
    CHECK(downloaded && length == strlen(PEPTO_VPL));
    return true;
}

static bool test_cancel_aborts_a_slow_request(void)
{
    // A request to a stalled device ends promptly once cancel is set, so
    // removing a source never waits for a timeout.
    fake_ultimate_t fake = {.has_setting = true, .ftp_enabled = true, .http_delay_ms = 3000};
    CHECK(fake_start(&fake));
    char host[64];
    fake_rest_host(&fake, host, sizeof(host));
    volatile bool cancel = true;
    char name[64];
    const uint64_t start = os_gettime_ns();
    const bool ok = c64_device_palette_fetch_setting(host, "", 5000, &cancel, name, sizeof(name), NULL);
    const uint64_t elapsed_ms = (os_gettime_ns() - start) / 1000000ULL;
    fake_stop(&fake);
    CHECK(!ok);
    CHECK(elapsed_ms < 1500);
    return true;
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"interval_clamp", test_interval_clamp},
        {"setting_parser", test_setting_parser},
        {"name_safety", test_name_safety},
        {"vpl_parser_matches_firmware", test_vpl_parser_matches_firmware},
        {"cache_path", test_cache_path},
        {"describe", test_describe},
        {"fetch_setting_and_size", test_fetch_setting_and_size},
        {"password_is_sent_and_checked", test_password_is_sent_and_checked},
        {"download_errors", test_download_errors},
        {"unreachable_services", test_unreachable_services},
        {"setting_status_mapping", test_setting_status_mapping},
        {"long_names_are_not_truncated", test_long_names_are_not_truncated},
        {"cancel_aborts_a_slow_request", test_cancel_aborts_a_slow_request},
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        const bool ok = tests[i].run();
        printf("%s %s\n", ok ? "PASS" : "FAIL", tests[i].name);
        failures += ok ? 0 : 1;
    }
    curl_global_cleanup();
    return failures ? 1 : 0;
}
