/*
C64 Stream - UDP Packet Replay Tool for Acceptance Testing
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.

High-performance UDP packet sender for replaying pre-generated C64 Ultimate
stream packets. Designed for maximum throughput to handle the high bandwidth
requirements of the C64 Ultimate protocol.
*/

// Define POSIX version for nanosleep
#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>

#define MAX_PACKET_SIZE 1024
#define MAX_PATH_LEN 512

// Timing utilities
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

int compare_filenames(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

int send_packets_from_directory(const char *dir_path, const char *host, int port, int packet_size, long delay_us,
                                int verbose)
{
    struct sockaddr_in addr;
    int sock;
    int packets_sent = 0;
    DIR *dir;
    struct dirent *entry;
    char **filenames = NULL;
    int file_count = 0;
    int file_capacity = 1000;

    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    // Configure destination address
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", host);
        close(sock);
        return -1;
    }

    // Allocate array for filenames
    filenames = malloc(file_capacity * sizeof(char *));
    if (!filenames) {
        fprintf(stderr, "Failed to allocate memory for filenames\n");
        close(sock);
        return -1;
    }

    // Read directory and collect .bin files
    dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "Failed to open directory: %s\n", strerror(errno));
        free(filenames);
        close(sock);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".bin")) {
            if (file_count >= file_capacity) {
                file_capacity *= 2;
                char **new_filenames = realloc(filenames, file_capacity * sizeof(char *));
                if (!new_filenames) {
                    fprintf(stderr, "Failed to reallocate memory for filenames\n");
                    for (int i = 0; i < file_count; i++)
                        free(filenames[i]);
                    free(filenames);
                    closedir(dir);
                    close(sock);
                    return -1;
                }
                filenames = new_filenames;
            }

            filenames[file_count] = malloc(strlen(entry->d_name) + 1);
            if (filenames[file_count]) {
                strcpy(filenames[file_count], entry->d_name);
                file_count++;
            }
        }
    }
    closedir(dir);

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
        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, filenames[i]);

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
        ssize_t sent = sendto(sock, buffer, packet_size, 0, (struct sockaddr *)&addr, sizeof(addr));
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
