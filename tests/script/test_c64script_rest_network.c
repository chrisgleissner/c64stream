/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

// Ensure asserts are always enabled in tests
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-script.h"
#include "c64-script-runtime.h"
#include "c64-rest-client.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <curl/curl.h>

#ifndef C64STREAM_SOURCE_DIR
#define C64STREAM_SOURCE_DIR "."
#endif

#define TEST(name) static void name(void)
#define RUN_TEST(name)                                                                                                 \
    do {                                                                                                               \
        printf("Running test: %s ... ", #name);                                                                        \
        name();                                                                                                        \
        printf("OK\n");                                                                                                \
    } while (0)

static bool wait_for_port(int port, int max_attempts)
{
#ifdef _WIN32
    (void)port;
    (void)max_attempts;
    return false;
#else
    for (int i = 0; i < max_attempts; i++) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return false;
        }
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        int rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
        close(sock);
        if (rc == 0) {
            return true;
        }
        usleep(100000);
    }
    return false;
#endif
}

static bool port_in_use(int port)
{
#ifdef _WIN32
    (void)port;
    return false;
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);
    return rc == 0;
#endif
}

static char *read_file_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read_count = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read_count] = '\0';
    return buf;
}

static void assert_log_contains(const char *log, const char *needle)
{
    assert(log != NULL);
    assert(needle != NULL);
    if (!strstr(log, needle)) {
        fprintf(stderr, "Missing REST call: %s\n", needle);
        assert(false);
    }
}

#ifndef _WIN32
static pid_t start_mock_server(int port, const char *log_path)
{
    if (port_in_use(port)) {
        return -1;
    }

    const char *python = getenv("PYTHON");
    if (!python || python[0] == '\0') {
        python = "python3";
    }

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        char port_buf[16];
        snprintf(port_buf, sizeof(port_buf), "%d", port);

        setenv("C64U_MOCK_LOG", log_path, 1);

        char script_path[512];
        snprintf(script_path, sizeof(script_path), "%s/tests/e2e/mock_c64u_server.py", C64STREAM_SOURCE_DIR);

        execlp(python, python, script_path, "--port", port_buf, NULL);
        _exit(127);
    }

    if (!wait_for_port(port, 50)) {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return -1;
    }

    int status = 0;
    pid_t waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
        return -1;
    }

    return pid;
}

static void stop_mock_server(pid_t pid)
{
    if (pid <= 0) {
        return;
    }
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
}
#endif

static bool write_temp_file(const char *suffix, const uint8_t *data, size_t size, char *out_path, size_t out_size)
{
    if (!out_path || out_size == 0) {
        return false;
    }
#ifdef _WIN32
    (void)suffix;
    (void)data;
    (void)size;
    out_path[0] = '\0';
    return false;
#else
    char tmpl[256];
    snprintf(tmpl, sizeof(tmpl), "/tmp/c64u_mock_XXXXXX%s", suffix ? suffix : "");
    int fd = mkstemps(tmpl, suffix ? (int)strlen(suffix) : 0);
    if (fd < 0) {
        return false;
    }
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        return false;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    snprintf(out_path, out_size, "%s", tmpl);
    return true;
#endif
}

TEST(rest_network_io_commands)
{
#ifdef _WIN32
    printf("SKIP (requires POSIX process control)\n");
    return;
#else
    curl_global_init(CURL_GLOBAL_DEFAULT);

    char log_path[256];
    snprintf(log_path, sizeof(log_path), "/tmp/c64u_mock_log_%d.txt", (int)getpid());
    remove(log_path);

    const int port = 8065;
    pid_t server_pid = start_mock_server(port, log_path);
    assert(server_pid > 0);

    char base_url[128];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", port);

    c64_rest_client_t *rest_client = c64_rest_client_create(base_url, NULL);
    assert(rest_client != NULL);

    uint8_t dummy_data[4] = {0x01, 0x02, 0x03, 0x04};
    char disk_path[256];
    char rom_path[256];
    char prg_path[256];
    assert(write_temp_file(".d64", dummy_data, sizeof(dummy_data), disk_path, sizeof(disk_path)));
    assert(write_temp_file(".rom", dummy_data, sizeof(dummy_data), rom_path, sizeof(rom_path)));
    assert(write_temp_file(".prg", dummy_data, sizeof(dummy_data), prg_path, sizeof(prg_path)));

    char source[4096];
    snprintf(source, sizeof(source),
             "RESET\n"
             "REBOOT\n"
             "PAUSE\n"
             "RESUME\n"
             "POWEROFF\n"
             "POKE $C000, 255\n"
             "PEEKVAL = PEEK($C000)\n"
             "CFG \"Audio Mixer\",\"Vol Sid Socket 1\",\"60\"\n"
             "CFGVAL$ = CFG$(\"Audio Mixer\",\"Vol Sid Socket 1\")\n"
             "DIM CATS$(16)\n"
             "DIM ITEMS$(16)\n"
             "DIM OPTS$(16)\n"
             "COUNT = CFG_ITEM$(CATS$())\n"
             "ITEMCOUNT = CFG_ITEM$(\"Audio Mixer\", ITEMS$())\n"
             "COUNT2 = CFG_OPTIONS$(\"Audio Mixer\",\"Vol Sid Socket 1\", OPTS$())\n"
             "CFGSAVE\n"
             "CFGLOAD\n"
             "CFGRESET\n"
             "SID_MODEL ULTI1, \"8580\"\n"
             "SID_ENABLE SOCKET1, 1\n"
             "SID_VOL ULTI1, \"80\"\n"
             "SID_FILTER_CURVE ULTI1, \"Flat\"\n"
             "SID_RESONANCE ULTI2, \"High\"\n"
             "SID_COMBINED ULTI1, \"Enabled\"\n"
             "SID_DIGIS ULTI2, \"Off\"\n"
             "VIC_MODE \"PAL\"\n"
             "CPU_SPEED \" 1\"\n"
             "X$ = DRIVE$(DRIVE_A, PROP_ENABLED)\n"
             "DRIVE_ON DRIVE_A\n"
             "DRIVE_OFF DRIVE_A\n"
             "DRIVE_RESET DRIVE_A\n"
             "DRIVE_UNMOUNT DRIVE_A\n"
             "DRIVE_MOUNT DRIVE_A, \"c64u:/Commodore/D64/disk1.d64\", TYPE_D64, MODE_READONLY\n"
             "DRIVE_MOUNT DRIVE_B, \"%s\", TYPE_D64, MODE_READWRITE\n"
             "DRIVE_ROM DRIVE_A, \"c64u:/ROMs/1541.rom\"\n"
             "DRIVE_ROM DRIVE_B, \"%s\"\n"
             "DRIVE_MODE DRIVE_B, MODE_1581\n"
             "DRIVE_BUS_ID DRIVE_A, 8\n"
             "MOUNTDISK \"%s\"\n"
             "PLAYSID \"c64u:/Commodore/SID/tune1.sid\"\n"
             "RUNPRG \"c64u:/Commodore/PRG/game1.prg\"\n"
             "RUNPRG \"%s\"\n"
             "MOUNTDISK \"c64u:/Commodore/D64/disk2.d64\"\n"
             "STOP\n",
             disk_path, rom_path, disk_path, prg_path);

    char error[512];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error, sizeof(error));
    assert(ast != NULL);

    c64script_runtime_t *runtime = c64script_runtime_create();
    assert(runtime != NULL);
    runtime->rest_client = rest_client;

    bool success = c64script_compile(ast, runtime, error, sizeof(error));
    assert(success);

    success = c64script_execute(runtime);
    if (!success) {
        fprintf(stderr, "C64Script error at line %d: %s\n", runtime->error_line, runtime->error_msg);
    }
    assert(success);

    c64script_value_t value;
    bool got_var = c64script_runtime_get_var(runtime, "PEEKVAL", &value);
    assert(got_var);
    assert(value.type == VALUE_NUMBER);
    assert(value.as.number == 255.0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "CFGVAL$", &value);
    assert(got_var);
    assert(value.type == VALUE_STRING);
    assert(strcmp(value.as.string, "60") == 0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "COUNT", &value);
    assert(got_var);
    assert(value.type == VALUE_NUMBER);
    assert(value.as.number >= 3.0);
    c64script_value_free(&value);

    got_var = c64script_runtime_get_var(runtime, "ITEMCOUNT", &value);
    assert(got_var);
    assert(value.type == VALUE_NUMBER);
    assert(value.as.number >= 2.0);
    c64script_value_free(&value);

    char *log = read_file_text(log_path);
    assert(log != NULL);

    assert_log_contains(log, "PUT /v1/machine:reset");
    assert_log_contains(log, "PUT /v1/machine:reboot");
    assert_log_contains(log, "PUT /v1/machine:pause");
    assert_log_contains(log, "PUT /v1/machine:resume");
    assert_log_contains(log, "PUT /v1/machine:poweroff");
    assert_log_contains(log, "PUT /v1/machine:writemem?address=C000");
    assert_log_contains(log, "GET /v1/machine:readmem?address=C000");
    assert_log_contains(log, "PUT /v1/configs/Audio%20Mixer/Vol%20Sid%20Socket%201?value=60");
    assert_log_contains(log, "GET /v1/configs/Audio%20Mixer/Vol%20Sid%20Socket%201");
    assert_log_contains(log, "GET /v1/configs");
    assert_log_contains(log, "GET /v1/configs/Audio%20Mixer");
    assert_log_contains(log, "PUT /v1/configs:save_to_flash");
    assert_log_contains(log, "PUT /v1/configs:load_from_flash");
    assert_log_contains(log, "PUT /v1/configs:reset_to_default");
    assert_log_contains(log, "GET /v1/drives");
    assert_log_contains(log, "PUT /v1/drives/a:on");
    assert_log_contains(log, "PUT /v1/drives/a:off");
    assert_log_contains(log, "PUT /v1/drives/a:reset");
    assert_log_contains(log, "PUT /v1/drives/a:remove");
    assert_log_contains(log, "PUT /v1/drives/a:mount?image=");
    assert_log_contains(log, "POST /v1/drives/b:mount?type=d64&mode=readwrite");
    assert_log_contains(log, "PUT /v1/drives/a:load_rom?file=");
    assert_log_contains(log, "POST /v1/drives/b:load_rom");
    assert_log_contains(log, "PUT /v1/drives/b:set_mode?mode=1581");
    assert_log_contains(log, "PUT /v1/configs/Drive%20A%20Settings/Drive%20Bus%20ID?value=8");
    assert_log_contains(log, "PUT /v1/runners:sidplay?file=");
    assert_log_contains(log, "POST /v1/runners:run_prg?path=");
    assert_log_contains(log, "POST /v1/runners:run_prg");
    assert_log_contains(log, "POST /v1/drives/a:mount?path=");
    assert_log_contains(log, "POST /v1/drives/a:mount?type=d64&mode=readonly");

    free(log);

    c64script_runtime_destroy(runtime);
    c64script_ast_free(ast);
    c64_rest_client_destroy(rest_client);
    stop_mock_server(server_pid);

    remove(disk_path);
    remove(rom_path);
    remove(prg_path);
    remove(log_path);

    curl_global_cleanup();
#endif
}

int main(void)
{
    RUN_TEST(rest_network_io_commands);
    return 0;
}
