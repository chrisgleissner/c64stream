/* In-process fake Ultimate for palette tests: the REST endpoints that report
 * the Palette Definition setting and file sizes, and an FTP server holding
 * /Flash/data. Both check the network password the way the device does
 * (X-Password header; any FTP user name, password = network password).
 * Loopback only, POSIX sockets or Winsock, one thread per service. */
#pragma once

#include "c64-network.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/platform.h>
#include <util/threading.h>
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#endif

#define FAKE_MAX_FILES 8

typedef struct {
    char name[64];
    char *data;
    size_t size;
} fake_file_t;

typedef struct {
    // Behaviour (change under lock while running)
    char password[64];  // empty: no password
    char setting[64];   // Palette Definition "current"
    bool has_setting;   // false: firmware without the setting (404)
    bool ftp_enabled;   // false: FTP connections are refused
    int http_delay_ms;  // delay between building and sending every HTTP answer
    int info_status;    // non-zero: every file-info request answers with this status
    int setting_status; // non-zero: every setting request answers with this status
    fake_file_t files[FAKE_MAX_FILES];
    size_t file_count;
    // Observations
    volatile long http_requests;
    volatile long setting_requests;
    volatile long info_requests;
    volatile long ftp_retrs;
    volatile long ftp_logins_rejected;
    // Plumbing
    socket_t http;
    socket_t ftp;
    uint16_t http_port;
    uint16_t ftp_port;
    volatile bool stop; // os_atomic
    pthread_t http_thread;
    pthread_t ftp_thread;
    pthread_mutex_t lock;
} fake_ultimate_t;

// Changes a behaviour field of a running fake.
#define FAKE_SET(fake, field, value)                                                                                   \
    do {                                                                                                               \
        pthread_mutex_lock(&(fake)->lock);                                                                             \
        (fake)->field = (value);                                                                                       \
        pthread_mutex_unlock(&(fake)->lock);                                                                           \
    } while (0)
// Reads an observation counter of a running fake.
#define FAKE_COUNT(fake, counter) os_atomic_load_long(&(fake)->counter)

static bool fake_readable(socket_t sock, int timeout_ms)
{
#ifdef _WIN32
    WSAPOLLFD fd = {0};
    fd.fd = sock;
    fd.events = POLLRDNORM;
    return WSAPoll(&fd, 1, timeout_ms) > 0;
#else
    struct pollfd fd = {0};
    fd.fd = sock;
    fd.events = POLLIN;
    return poll(&fd, 1, timeout_ms) > 0;
#endif
}

static void fake_set_blocking(socket_t sock, bool blocking)
{
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    const int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK);
#endif
}

// Waits up to timeout_ms for a connection. Listeners are non-blocking, so a
// client that gives up between poll and accept (a cancelled transfer) cannot
// leave the thread stuck in accept.
static socket_t fake_accept(socket_t listener, int timeout_ms)
{
    if (!fake_readable(listener, timeout_ms)) {
        return INVALID_SOCKET_VALUE;
    }
    socket_t client = accept(listener, NULL, NULL);
    if (client != INVALID_SOCKET_VALUE) {
        fake_set_blocking(client, true); // inherited on Windows and BSD
    }
    return client;
}

static socket_t fake_listen(uint16_t *port)
{
    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET_VALUE) {
        return sock;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(addr);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(sock, 16) != 0 ||
        getsockname(sock, (struct sockaddr *)&addr, &len) != 0) {
        close(sock);
        return INVALID_SOCKET_VALUE;
    }
    *port = ntohs(addr.sin_port);
    fake_set_blocking(sock, false);
    return sock;
}

static void fake_send(socket_t sock, const char *data, size_t length)
{
    while (length) {
        const int sent = (int)send(sock, data, (int)length, 0);
        if (sent <= 0) {
            return;
        }
        data += sent;
        length -= (size_t)sent;
    }
}

static void fake_sendf(socket_t sock, const char *format, const char *arg)
{
    char line[512];
    snprintf(line, sizeof(line), format, arg ? arg : "");
    fake_send(sock, line, strlen(line));
}

// Reads one CRLF/LF-terminated line (FTP) or up to the blank line (HTTP).
static size_t fake_recv_until(socket_t sock, char *buffer, size_t size, const char *terminator)
{
    size_t used = 0;
    buffer[0] = '\0';
    while (used + 1 < size && !strstr(buffer, terminator) && fake_readable(sock, 3000)) {
        const int got = (int)recv(sock, buffer + used, 1, 0); // byte-wise: never read past the line
        if (got <= 0) {
            break;
        }
        used += (size_t)got;
        buffer[used] = '\0';
    }
    return used;
}

static void fake_url_decode(const char *in, char *out, size_t out_size)
{
    size_t n = 0;
    for (; *in && n + 1 < out_size; in++) {
        if (*in == '%' && in[1] && in[2]) {
            char hex[3] = {in[1], in[2], 0};
            out[n++] = (char)strtol(hex, NULL, 16);
            in += 2;
        } else {
            out[n++] = *in;
        }
    }
    out[n] = '\0';
}

// Caller holds lock.
static fake_file_t *fake_find(fake_ultimate_t *fake, const char *name)
{
    for (size_t i = 0; i < fake->file_count; i++) {
        if (!strcmp(fake->files[i].name, name)) {
            return &fake->files[i];
        }
    }
    return NULL;
}

static void fake_put_file(fake_ultimate_t *fake, const char *name, const char *data)
{
    pthread_mutex_lock(&fake->lock);
    fake_file_t *file = fake_find(fake, name);
    if (!file && fake->file_count < FAKE_MAX_FILES) {
        file = &fake->files[fake->file_count++];
        snprintf(file->name, sizeof(file->name), "%s", name);
    }
    if (file) {
        free(file->data);
        file->size = strlen(data);
        file->data = malloc(file->size + 1);
        memcpy(file->data, data, file->size + 1);
    }
    pthread_mutex_unlock(&fake->lock);
}

static void fake_set_setting(fake_ultimate_t *fake, const char *name)
{
    pthread_mutex_lock(&fake->lock);
    snprintf(fake->setting, sizeof(fake->setting), "%s", name);
    pthread_mutex_unlock(&fake->lock);
}

static void fake_http_answer(fake_ultimate_t *fake, socket_t client, const char *request)
{
    os_atomic_inc_long(&fake->http_requests);
    char path[300] = {0};
    sscanf(request, "GET %299s", path);
    pthread_mutex_lock(&fake->lock);
    char expected[128];
    snprintf(expected, sizeof(expected), "X-Password: %s\r\n", fake->password);
    const bool authorized = !fake->password[0] || strstr(request, expected);
    int status = 404;
    char body[512] = "{\n  \"errors\" : [ \"Not found\" ]\n}";
    const char *setting_path = "/v1/configs/U64%20Specific%20Settings/Palette%20Definition";
    const char *files_prefix = "/v1/files/flash/data/";
    if (!authorized) {
        status = 403;
        snprintf(body, sizeof(body), "{\n  \"errors\" : [ \"Forbidden.\" ]\n}");
    } else if (!strcmp(path, setting_path)) {
        os_atomic_inc_long(&fake->setting_requests);
        if (fake->setting_status) {
            status = fake->setting_status;
        } else if (fake->has_setting) {
            status = 200;
            snprintf(body, sizeof(body),
                     "{ \n  \"U64 Specific Settings\" : { \n  \"Palette Definition\" : { \n  \"current\" : "
                     "\"%s\",\n  \"presets\" : [ \"\" ],\n  \"default\" : \"\"\n}\n},\n  \"errors\" : [  ]\n}",
                     fake->setting);
        }
    } else if (!strncmp(path, files_prefix, strlen(files_prefix)) && strstr(path, ":info")) {
        os_atomic_inc_long(&fake->info_requests);
        char encoded[200] = {0};
        snprintf(encoded, sizeof(encoded), "%.*s", (int)(strstr(path, ":info") - path - strlen(files_prefix)),
                 path + strlen(files_prefix));
        char name[64];
        fake_url_decode(encoded, name, sizeof(name));
        const fake_file_t *file = fake_find(fake, name);
        if (fake->info_status) {
            status = fake->info_status;
        } else if (file) {
            status = 200;
            snprintf(body, sizeof(body),
                     "{ \n  \"files\" : { \n  \"path\" : \"flash/data/%s\",\n  \"filename\" : \"%s\",\n  \"size\" : "
                     "%zu,\n  \"extension\" : \"VPL\"\n},\n  \"errors\" : [  ]\n}",
                     name, name, file->size);
        }
    }
    const int delay_ms = fake->http_delay_ms;
    pthread_mutex_unlock(&fake->lock);
    // The answer reflects the state when the request arrived, as on a device
    // that is slow to send it.
    if (delay_ms) {
        os_sleep_ms((uint32_t)delay_ms);
    }
    char header[200];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d X\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n"
             "Connection: close\r\n\r\n",
             status, strlen(body));
    fake_send(client, header, strlen(header));
    fake_send(client, body, strlen(body));
}

static void *fake_http_main(void *opaque)
{
    fake_ultimate_t *fake = opaque;
    while (!os_atomic_load_bool(&fake->stop)) {
        socket_t client = fake_accept(fake->http, 20);
        if (client == INVALID_SOCKET_VALUE) {
            continue;
        }
        char request[2048];
        fake_recv_until(client, request, sizeof(request), "\r\n\r\n");
        fake_http_answer(fake, client, request);
        close(client);
    }
    return NULL;
}

static void fake_ftp_session(fake_ultimate_t *fake, socket_t control)
{
    fake_send(control, "220 Fake Ultimate FTP\r\n", strlen("220 Fake Ultimate FTP\r\n"));
    socket_t passive = INVALID_SOCKET_VALUE;
    bool logged_in = false;
    char line[512];
    while (!os_atomic_load_bool(&fake->stop) && fake_recv_until(control, line, sizeof(line), "\n")) {
        char *end = strpbrk(line, "\r\n");
        if (end) {
            *end = '\0';
        }
        char verb[8] = {0};
        sscanf(line, "%7s", verb);
        const char *arg = strchr(line, ' ') ? strchr(line, ' ') + 1 : "";
        if (!strcmp(verb, "USER")) {
            fake_send(control, "331 Password required\r\n", strlen("331 Password required\r\n"));
        } else if (!strcmp(verb, "PASS")) {
            pthread_mutex_lock(&fake->lock);
            logged_in = !fake->password[0] || !strcmp(arg, fake->password);
            pthread_mutex_unlock(&fake->lock);
            if (logged_in) {
                fake_send(control, "230 Logged in\r\n", strlen("230 Logged in\r\n"));
            } else {
                os_atomic_inc_long(&fake->ftp_logins_rejected);
                fake_send(control, "530 Login incorrect\r\n", strlen("530 Login incorrect\r\n"));
            }
        } else if (!logged_in) {
            fake_send(control, "530 Not logged in\r\n", strlen("530 Not logged in\r\n"));
        } else if (!strcmp(verb, "PWD")) {
            fake_send(control, "257 \"/\"\r\n", strlen("257 \"/\"\r\n"));
        } else if (!strcmp(verb, "CWD") || !strcmp(verb, "TYPE")) {
            fake_send(control, "200 OK\r\n", strlen("200 OK\r\n"));
        } else if (!strcmp(verb, "EPSV") || !strcmp(verb, "PASV")) {
            uint16_t port = 0;
            if (passive != INVALID_SOCKET_VALUE) {
                close(passive);
            }
            passive = fake_listen(&port);
            char reply[100];
            if (!strcmp(verb, "EPSV")) {
                snprintf(reply, sizeof(reply), "229 Entering Extended Passive Mode (|||%u|)\r\n", port);
            } else {
                snprintf(reply, sizeof(reply), "227 Entering Passive Mode (127,0,0,1,%u,%u)\r\n", port >> 8,
                         port & 0xFF);
            }
            fake_send(control, reply, strlen(reply));
        } else if (!strcmp(verb, "SIZE") || !strcmp(verb, "RETR")) {
            const char *name = strrchr(arg, '/') ? strrchr(arg, '/') + 1 : arg;
            pthread_mutex_lock(&fake->lock);
            const fake_file_t *file = fake_find(fake, name);
            char *copy = NULL;
            size_t size = 0;
            if (file) {
                size = file->size;
                copy = malloc(size + 1);
                memcpy(copy, file->data, size + 1);
            }
            pthread_mutex_unlock(&fake->lock);
            if (!copy) {
                fake_send(control, "550 File not found\r\n", strlen("550 File not found\r\n"));
            } else if (!strcmp(verb, "SIZE")) {
                char reply[64];
                snprintf(reply, sizeof(reply), "213 %zu\r\n", size);
                fake_send(control, reply, strlen(reply));
            } else {
                os_atomic_inc_long(&fake->ftp_retrs);
                fake_send(control, "150 Opening data connection\r\n", strlen("150 Opening data connection\r\n"));
                if (passive != INVALID_SOCKET_VALUE) {
                    socket_t data = fake_accept(passive, 3000);
                    if (data != INVALID_SOCKET_VALUE) {
                        fake_send(data, copy, size);
                        close(data);
                    }
                }
                fake_send(control, "226 Transfer complete\r\n", strlen("226 Transfer complete\r\n"));
            }
            free(copy);
        } else if (!strcmp(verb, "QUIT")) {
            fake_send(control, "221 Bye\r\n", strlen("221 Bye\r\n"));
            break;
        } else {
            fake_send(control, "502 Not implemented\r\n", strlen("502 Not implemented\r\n"));
        }
    }
    if (passive != INVALID_SOCKET_VALUE) {
        close(passive);
    }
}

static void *fake_ftp_main(void *opaque)
{
    fake_ultimate_t *fake = opaque;
    while (!os_atomic_load_bool(&fake->stop)) {
        socket_t client = fake_accept(fake->ftp, 20);
        if (client == INVALID_SOCKET_VALUE) {
            continue;
        }
        pthread_mutex_lock(&fake->lock);
        const bool enabled = fake->ftp_enabled;
        pthread_mutex_unlock(&fake->lock);
        if (enabled) {
            fake_ftp_session(fake, client);
        }
        close(client);
    }
    return NULL;
}

static bool fake_start(fake_ultimate_t *fake)
{
    pthread_mutex_init(&fake->lock, NULL);
    fake->http = fake_listen(&fake->http_port);
    fake->ftp = fake_listen(&fake->ftp_port);
    if (fake->http == INVALID_SOCKET_VALUE || fake->ftp == INVALID_SOCKET_VALUE) {
        return false;
    }
    return pthread_create(&fake->http_thread, NULL, fake_http_main, fake) == 0 &&
           pthread_create(&fake->ftp_thread, NULL, fake_ftp_main, fake) == 0;
}

static void fake_stop(fake_ultimate_t *fake)
{
    os_atomic_set_bool(&fake->stop, true);
    pthread_join(fake->http_thread, NULL);
    pthread_join(fake->ftp_thread, NULL);
    close(fake->http);
    close(fake->ftp);
    for (size_t i = 0; i < fake->file_count; i++) {
        free(fake->files[i].data);
    }
    pthread_mutex_destroy(&fake->lock);
}

static void fake_rest_host(const fake_ultimate_t *fake, char *out, size_t out_size)
{
    snprintf(out, out_size, "127.0.0.1:%u", fake->http_port);
}
