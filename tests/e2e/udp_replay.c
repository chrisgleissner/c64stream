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

static inline double get_time_ms(void)
{
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static void sleep_until_us(double target_time_us)
{
    // Sleep-only approach (no busy-wait to save CPU)
    // The plugin's network buffer handles any timing jitter
    double now = get_time_ms() * 1000.0;
    double remaining_us = target_time_us - now;

    if (remaining_us <= 0)
        return;

    // Use Sleep for the entire duration (1ms granularity on Windows)
    long sleep_ms = (long)(remaining_us / 1000);
    if (sleep_ms > 0)
        Sleep((DWORD)sleep_ms);
}
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

static inline double get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void sleep_until_us(double target_time_us)
{
    // Sleep-only approach (no busy-wait to save CPU)
    // The plugin's network buffer handles any timing jitter
    double now = get_time_ms() * 1000.0;
    double remaining_us = target_time_us - now;

    if (remaining_us <= 0)
        return;

    // Use nanosleep for the entire duration
    long sleep_us = (long)remaining_us;
    struct timespec ts;
    ts.tv_sec = sleep_us / 1000000;
    ts.tv_nsec = (sleep_us % 1000000) * 1000;
    nanosleep(&ts, NULL);
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
        long delay = atol(comma + 1);

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
    if (argc < 6) {
        printf("Usage: %s <manifest> <dir> <host> <port> <packet_size> [--verbose]\n", argv[0]);
        return 1;
    }

    const char *manifest_path = argv[1];
    const char *dir_path = argv[2];
    const char *host = argv[3];
    int port = atoi(argv[4]);
    int packet_size = atoi(argv[5]);
    int verbose = (argc > 6 && strcmp(argv[6], "--verbose") == 0);

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

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    struct packet_entry *entries = NULL;
    int count = 0;

    if (load_manifest(manifest_path, &entries, &count) != 0) {
        fprintf(stderr, "Failed to load manifest\n");
        return 1;
    }

    if (verbose)
        printf("Loaded %d entries from manifest\n", count);

    double start_us = get_time_ms() * 1000.0;
    double cumulative_us = 0; // Target time relative to start
    int sent = 0;

    for (int i = 0; i < count; i++) {
        char path[MAX_PATH_LEN];
#ifdef _WIN32
        snprintf(path, sizeof(path), "%s\\%s", dir_path, entries[i].filename);
#else
        snprintf(path, sizeof(path), "%s/%s", dir_path, entries[i].filename);
#endif

        FILE *f = fopen(path, "rb");
        if (!f)
            continue;

        unsigned char buf[MAX_PACKET_SIZE];
        size_t n = fread(buf, 1, packet_size, f);
        fclose(f);

        if (n != (size_t)packet_size)
            continue;

        // Wait until target time (absolute timing - compensates for I/O overhead)
        cumulative_us += entries[i].delay_us;
        double target_us = start_us + cumulative_us;
        sleep_until_us(target_us);

        sendto(sock, (char *)buf, packet_size, 0, (struct sockaddr *)&addr, sizeof(addr));
        sent++;

        if (verbose && sent % 500 == 0)
            printf("  Sent %d/%d\n", sent, count);
    }

    double elapsed = (get_time_ms() * 1000.0 - start_us) / 1000.0; // Convert to ms
    if (verbose)
        printf("✅ Sent %d packets in %.1fms\n", sent, elapsed);

    free(entries);
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    return (sent > 0) ? 0 : 1;
}
