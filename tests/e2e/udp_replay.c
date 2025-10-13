/*
C64 Stream - UDP Packet Replay Tool for Acceptance Testing
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

High-performance UDP packet sender for replaying pre-generated C64 Ultimate
stream packets. Designed for maximum throughput to handle the high bandwidth
requirements of the C64 Ultimate protocol.
*/

#ifndef _WIN32
// Define POSIX version for nanosleep before any includes
#define _POSIX_C_SOURCE 199309L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#define close closesocket
typedef int ssize_t;

// Windows directory handling
#include <io.h>
#include <direct.h>
typedef struct _finddata_t finddata_t;

// Windows timing
static inline double get_time_ms(void)
{
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static void sleep_us(long microseconds)
{
    Sleep((DWORD)(microseconds / 1000));
}
#else
// POSIX includes
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <dirent.h>

// POSIX timing
static inline double get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void sleep_us(long microseconds)
{
    struct timespec ts;
    ts.tv_sec = microseconds / 1000000;
    ts.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&ts, NULL);
}
#endif

#define MAX_PACKET_SIZE 1024
#define MAX_PATH_LEN 512

int compare_filenames(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

// Cross-platform directory scanning
#ifdef _WIN32
static int scan_directory(const char *dir_path, char ***filenames, int *file_count)
{
    char search_path[MAX_PATH_LEN];
    snprintf(search_path, sizeof(search_path), "%s\\*.bin", dir_path);

    intptr_t handle;
    finddata_t fileinfo;
    int file_capacity = 1000;

    *filenames = malloc(file_capacity * sizeof(char *));
    if (!*filenames)
        return -1;

    *file_count = 0;
    handle = _findfirst(search_path, &fileinfo);

    if (handle == -1) {
        free(*filenames);
        return -1;
    }

    do {
        if (*file_count >= file_capacity) {
            file_capacity *= 2;
            char **new_filenames = realloc(*filenames, file_capacity * sizeof(char *));
            if (!new_filenames) {
                for (int i = 0; i < *file_count; i++)
                    free((*filenames)[i]);
                free(*filenames);
                _findclose(handle);
                return -1;
            }
            *filenames = new_filenames;
        }

        (*filenames)[*file_count] = malloc(strlen(fileinfo.name) + 1);
        if ((*filenames)[*file_count]) {
            strcpy((*filenames)[*file_count], fileinfo.name);
            (*file_count)++;
        }
    } while (_findnext(handle, &fileinfo) == 0);

    _findclose(handle);
    return 0;
}
#else
static int scan_directory(const char *dir_path, char ***filenames, int *file_count)
{
    DIR *dir;
    struct dirent *entry;
    int file_capacity = 1000;

    *filenames = malloc(file_capacity * sizeof(char *));
    if (!*filenames)
        return -1;

    dir = opendir(dir_path);
    if (!dir) {
        free(*filenames);
        return -1;
    }

    *file_count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".bin")) {
            if (*file_count >= file_capacity) {
                file_capacity *= 2;
                char **new_filenames = realloc(*filenames, file_capacity * sizeof(char *));
                if (!new_filenames) {
                    for (int i = 0; i < *file_count; i++)
                        free((*filenames)[i]);
                    free(*filenames);
                    closedir(dir);
                    return -1;
                }
                *filenames = new_filenames;
            }

            (*filenames)[*file_count] = malloc(strlen(entry->d_name) + 1);
            if ((*filenames)[*file_count]) {
                strcpy((*filenames)[*file_count], entry->d_name);
                (*file_count)++;
            }
        }
    }
    closedir(dir);
    return 0;
}
#endif

int send_packets_from_directory(const char *dir_path, const char *host, int port, int packet_size, long delay_us,
                                int verbose)
{
    struct sockaddr_in addr;
    int sock;
    int packets_sent = 0;
    char **filenames = NULL;
    int file_count = 0;

#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return -1;
    }
#endif

    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    // Configure destination address
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", host);
        close(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    // Scan directory for .bin files
    if (scan_directory(dir_path, &filenames, &file_count) != 0) {
        fprintf(stderr, "Failed to scan directory: %s\n", dir_path);
        close(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    // Sort filenames alphabetically to ensure correct order
    qsort(filenames, file_count, sizeof(char *), compare_filenames);

    if (verbose) {
        printf("Found %d packet files in %s\n", file_count, dir_path);
        printf("Sending to %s:%d with %ld us delay between packets\n", host, port, delay_us);
    }

    double start_time = get_time_ms();

    // Send each packet file
    for (int i = 0; i < file_count; i++) {
        char filepath[MAX_PATH_LEN];
#ifdef _WIN32
        snprintf(filepath, sizeof(filepath), "%s\\%s", dir_path, filenames[i]);
#else
        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, filenames[i]);
#endif

        FILE *f = fopen(filepath, "rb");
        if (!f) {
            if (verbose)
                fprintf(stderr, "Warning: Failed to open %s: %s\n", filepath, strerror(errno));
            continue;
        }

        // Read packet data
        unsigned char buffer[MAX_PACKET_SIZE];
        size_t bytes_read = fread(buffer, 1, packet_size, f);
        fclose(f);

        if (bytes_read != (size_t)packet_size) {
            if (verbose)
                fprintf(stderr, "Warning: Expected %d bytes, got %zu in %s\n", packet_size, bytes_read, filepath);
            continue;
        }

        // Send packet
        ssize_t sent = sendto(sock, (const char *)buffer, packet_size, 0, (struct sockaddr *)&addr, sizeof(addr));
        if (sent < 0) {
            fprintf(stderr, "Send failed: %s\n", strerror(errno));
            break;
        }

        packets_sent++;

        // Apply inter-packet delay if specified
        if (delay_us > 0) {
            sleep_us(delay_us);
        }

        // Progress indicator every 100 packets
        if (verbose && packets_sent % 100 == 0) {
            printf("  Sent %d/%d packets\n", packets_sent, file_count);
        }
    }

    double elapsed_ms = get_time_ms() - start_time;
    double packets_per_sec = (packets_sent * 1000.0) / elapsed_ms;

    if (verbose) {
        printf("✅ Sent %d packets in %.2f ms (%.1f packets/sec)\n", packets_sent, elapsed_ms, packets_per_sec);
    }

    // Cleanup
    for (int i = 0; i < file_count; i++) {
        free(filenames[i]);
    }
    free(filenames);
    close(sock);

#ifdef _WIN32
    WSACleanup();
#endif

    return packets_sent;
}

void print_usage(const char *prog_name)
{
    printf("UDP Packet Replay Tool for C64 Stream Testing\n\n");
    printf("Usage: %s <directory> <host> <port> <packet_size> [options]\n\n", prog_name);
    printf("Arguments:\n");
    printf("  directory     Directory containing .bin packet files\n");
    printf("  host          Destination IP address\n");
    printf("  port          Destination UDP port\n");
    printf("  packet_size   Size of each packet in bytes\n\n");
    printf("Options:\n");
    printf("  --delay <us>  Microsecond delay between packets (default: 0)\n");
    printf("  --verbose     Print detailed progress information\n");
    printf("  --help        Show this help message\n\n");
    printf("Examples:\n");
    printf("  # Send video packets with 300us delay\n");
    printf("  %s test_packets/video/PAL 127.0.0.1 11000 780 --delay 300\n\n", prog_name);
    printf("  # Send audio packets\n");
    printf("  %s test_packets/audio/PAL 127.0.0.1 11001 770\n\n", prog_name);
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        print_usage(argv[0]);
        return 1;
    }

    const char *dir_path = argv[1];
    const char *host = argv[2];
    int port = atoi(argv[3]);
    int packet_size = atoi(argv[4]);
    long delay_us = 0;
    int verbose = 0;

    // Parse optional arguments
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            delay_us = atol(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Validate parameters
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: Invalid port number: %d\n", port);
        return 1;
    }

    if (packet_size <= 0 || packet_size > MAX_PACKET_SIZE) {
        fprintf(stderr, "Error: Invalid packet size: %d (max %d)\n", packet_size, MAX_PACKET_SIZE);
        return 1;
    }

    int result = send_packets_from_directory(dir_path, host, port, packet_size, delay_us, verbose);

    return (result > 0) ? 0 : 1;
}
