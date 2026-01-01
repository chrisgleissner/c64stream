/*
C64 Stream - UDP Packet Replay Tool for Acceptance Testing
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

Ultra-simple packet sender: reads a CSV manifest file and blindly sends packets
with precalculated delays. No logic, no randomization - just read, send, sleep.

Manifest format (CSV):
  filename,delay_us
  video_0000_0000.bin,333
  video_0000_0001.bin,280
  ...

All simulation logic (jitter, reordering) is precalculated by Python.
*/

#define _POSIX_C_SOURCE 199309L

#include <stdint.h>

#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int ssize_t;

static uint64_t qpc_freq_hz = 0;

static inline uint64_t get_time_us(void)
{
    LARGE_INTEGER counter;
    if (qpc_freq_hz == 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        qpc_freq_hz = (uint64_t)freq.QuadPart;
    }
    QueryPerformanceCounter(&counter);
    return (uint64_t)((double)counter.QuadPart * 1000000.0 / (double)qpc_freq_hz);
}

static void sleep_until_us(uint64_t target_time_us)
{
    uint64_t now_us = get_time_us();
    if (target_time_us <= now_us)
        return;

    uint64_t remaining_us = target_time_us - now_us;

    // Use Sleep for the bulk duration (1ms granularity on Windows)
    DWORD sleep_ms = (DWORD)(remaining_us / 1000ULL);
    if (sleep_ms > 0) {
        Sleep((DWORD)sleep_ms);
    }
}
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

static inline uint64_t get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void sleep_until_us(uint64_t target_time_us)
{
    for (;;) {
        uint64_t now_us = get_time_us();
        if (target_time_us <= now_us) {
            return;
        }

        uint64_t remaining_us = target_time_us - now_us;
        struct timespec ts;
        ts.tv_sec = (time_t)(remaining_us / 1000000ULL);
        ts.tv_nsec = (long)((remaining_us % 1000000ULL) * 1000ULL);
        nanosleep(&ts, NULL);
    }
}
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAX_PACKET_SIZE 1024
#define MAX_PATH_LEN 512
#define MAX_MANIFEST_ENTRIES 50000

struct packet_entry {
    char filename[256];
    long delay_us;
};

static const struct packet_entry *g_entries_for_sort = NULL;

static int compare_indices_by_filename(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return strcmp(g_entries_for_sort[ia].filename, g_entries_for_sort[ib].filename);
}

static int load_manifest(const char *path, struct packet_entry **entries, int *count)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    *entries = malloc(MAX_MANIFEST_ENTRIES * sizeof(struct packet_entry));
    if (!*entries) {
        fclose(f);
        return -1;
    }

    *count = 0;
    char line[512];

    // Skip header if present
    if (fgets(line, sizeof(line), f) && !(strstr(line, "filename") && strstr(line, "delay")))
        rewind(f);

    while (fgets(line, sizeof(line), f) && *count < MAX_MANIFEST_ENTRIES) {
        char *comma = strchr(line, ',');
        if (!comma)
            continue;

        *comma = '\0';
        char *fname = line;
        char *delay_str = comma + 1;

        // Trim leading whitespace from delay
        while (*delay_str == ' ' || *delay_str == '\t')
            delay_str++;

        errno = 0;
        char *endp = NULL;
        long delay = strtol(delay_str, &endp, 10);
        if (errno != 0 || endp == delay_str) {
            fclose(f);
            free(*entries);
            return -1;
        }
        if (delay < 0) {
            fclose(f);
            free(*entries);
            return -1;
        }

        // Trim whitespace and newlines
        size_t len = strlen(fname);
        while (len > 0 &&
               (fname[len - 1] == ' ' || fname[len - 1] == '\t' || fname[len - 1] == '\r' || fname[len - 1] == '\n')) {
            fname[--len] = '\0';
        }

        // Use snprintf for safe copying
        snprintf((*entries)[*count].filename, sizeof((*entries)[0].filename), "%s", fname);
        (*entries)[*count].delay_us = delay;
        (*count)++;
    }

    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    // Ensure logs are emitted in real time even when stdout is piped.
    // Without this, stdio may use block buffering and delay output until exit.
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    if (argc < 6) {
        printf("Usage: %s <manifest> <dir> <host> <port> <packet_size> [--verbose] [--start-at-us <t>]\n", argv[0]);
        return 1;
    }

    const char *manifest_path = argv[1];
    const char *dir_path = argv[2];
    const char *host = argv[3];
    int port = atoi(argv[4]);
    int packet_size = atoi(argv[5]);
    int verbose = 0;
    uint64_t start_at_us = 0;

    for (int i = 6; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
            continue;
        }
        if (strcmp(argv[i], "--start-at-us") == 0 && (i + 1) < argc) {
            start_at_us = (uint64_t)strtoull(argv[i + 1], NULL, 10);
            i++;
            continue;
        }
    }

    if (port <= 0 || port > 65535 || packet_size <= 0 || packet_size > MAX_PACKET_SIZE) {
        fprintf(stderr, "Invalid port or packet_size\n");
        return 1;
    }

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (sock < 0) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    // Best-effort: increase UDP send buffer to reduce sender-side drops under load.
    // Some CI environments clamp this to a low value; failure is non-fatal.
    {
        int send_buffer_size = 4 * 1024 * 1024; // 4MB
#ifdef _WIN32
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&send_buffer_size, sizeof(send_buffer_size));
#else
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size));
#endif
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid host IP: %s\n", host);
        return 1;
    }

    struct packet_entry *entries = NULL;
    int count = 0;

    if (load_manifest(manifest_path, &entries, &count) != 0) {
        fprintf(stderr, "Failed to load manifest\n");
        return 1;
    }

    if (verbose)
        printf("Loaded %d entries from manifest\n", count);

    // Preload packets into memory so send timing is not dominated by per-packet file I/O.
    // This is critical for keeping up with the ~3.8K packets/sec rate without relying on
    // catch-up bursts (which can overflow small UDP receive buffers in CI).
    uint64_t total_bytes = (uint64_t)count * (uint64_t)packet_size;
    if (count <= 0 || total_bytes == 0) {
        fprintf(stderr, "No packets to send\n");
        free(entries);
        return 1;
    }

    uint8_t *packet_data = malloc((size_t)total_bytes);
    if (!packet_data) {
        fprintf(stderr, "Failed to allocate %llu bytes for packet preload\n", (unsigned long long)total_bytes);
        free(entries);
        return 1;
    }

    // Preload packets in filename order to maximize filesystem locality.
    // IMPORTANT:
    // - The manifest order may be heavily shuffled (e.g., jitter/reordering scenarios).
    // - We still SEND in manifest order, but we can safely preload in any order as
    //   long as we store packet bytes at the correct manifest index.
    // - This avoids missing start_at_us on cold CI filesystems, which would otherwise
    //   trigger a "catch up" burst and massive receiver-side drops.
    uint64_t preload_begin_us = get_time_us();

    int *preload_indices = malloc((size_t)count * sizeof(int));
    if (!preload_indices) {
        fprintf(stderr, "Failed to allocate preload indices\n");
        free(packet_data);
        free(entries);
        return 1;
    }

    for (int i = 0; i < count; i++) {
        preload_indices[i] = i;
    }

    g_entries_for_sort = entries;
    qsort(preload_indices, (size_t)count, sizeof(int), compare_indices_by_filename);
    g_entries_for_sort = NULL;

    for (int k = 0; k < count; k++) {
        int i = preload_indices[k];
        char path[MAX_PATH_LEN];
#ifdef _WIN32
        snprintf(path, sizeof(path), "%s\\%s", dir_path, entries[i].filename);
#else
        snprintf(path, sizeof(path), "%s/%s", dir_path, entries[i].filename);
#endif

        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "Failed to open packet file: %s\n", path);
            free(preload_indices);
            free(packet_data);
            free(entries);
            return 1;
        }

        uint8_t *dest = packet_data + ((uint64_t)i * (uint64_t)packet_size);
        size_t n = fread(dest, 1, (size_t)packet_size, f);
        fclose(f);
        if (n != (size_t)packet_size) {
            fprintf(stderr, "Short read for packet file: %s (%zu/%d)\n", path, n, packet_size);
            free(preload_indices);
            free(packet_data);
            free(entries);
            return 1;
        }
    }

    free(preload_indices);

    uint64_t preload_end_us = get_time_us();
    if (verbose) {
        printf("Preload complete (%d packets, %llu bytes) in %.1fms\n", count, (unsigned long long)total_bytes,
               (double)(preload_end_us - preload_begin_us) / 1000.0);
    }

    // Align start across processes (audio+video) using an absolute monotonic timestamp.
    uint64_t start_us = start_at_us ? start_at_us : get_time_us();
    if (verbose) {
        uint64_t now_us = get_time_us();
        if (now_us > start_us) {
            printf("⚠️  Missed start_at_us by %.1fms (will catch up by sending immediately)\n",
                   (double)(now_us - start_us) / 1000.0);
        }
    }
    sleep_until_us(start_us);

    uint64_t cumulative_us = 0; // Target time relative to start
    int sent = 0;
    int send_errors = 0;

    for (int i = 0; i < count; i++) {
        uint8_t *buf = packet_data + ((uint64_t)i * (uint64_t)packet_size);

        // Wait until target time (absolute schedule).
        //
        // IMPORTANT: In loaded environments we may get behind schedule.
        // We must NOT permanently slow down the stream by shifting the schedule origin,
        // and we must NOT add extra delays when we're behind (that stretches the stream).
        // If we fall behind, we send immediately until we catch up.
        cumulative_us += (uint64_t)entries[i].delay_us;
        uint64_t target_us = start_us + cumulative_us;
        uint64_t now_us = get_time_us();
        if (target_us > now_us) {
            sleep_until_us(target_us);
        }

        ssize_t rc = -1;
        for (int attempt = 0; attempt < 10; attempt++) {
            rc = sendto(sock, (char *)buf, packet_size, 0, (struct sockaddr *)&addr, sizeof(addr));
            if (rc == packet_size) {
                break;
            }

#ifdef _WIN32
            int err = WSAGetLastError();
            // Retry on transient buffer exhaustion.
            if (err == WSAENOBUFS && attempt < 9) {
                Sleep(1);
                continue;
            }
            fprintf(stderr, "sendto failed (attempt %d/%d): WSA error=%d\n", attempt + 1, 10, err);
#else
            int err = errno;
            // Retry on transient buffer exhaustion.
            if ((err == ENOBUFS || err == EAGAIN) && attempt < 9) {
                struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};
                nanosleep(&ts, NULL);
                continue;
            }
            fprintf(stderr, "sendto failed (attempt %d/%d): %s\n", attempt + 1, 10, strerror(err));
#endif
            break;
        }

        if (rc != packet_size) {
            send_errors++;
            break;
        }

        sent++;

        if (verbose && sent % 500 == 0)
            printf("  Sent %d/%d\n", sent, count);
    }

    double elapsed = (double)(get_time_us() - start_us) / 1000.0; // Convert to ms
    if (verbose)
        printf("✅ Sent %d packets in %.1fms (schedule: %.1fms, send errors: %d)\n", sent, elapsed,
               (double)cumulative_us / 1000.0, send_errors);

    free(packet_data);
    free(entries);
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    if (sent <= 0)
        return 1;
    return (send_errors == 0) ? 0 : 1;
}
