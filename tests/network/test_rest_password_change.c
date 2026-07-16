/* C64 Stream - C64STR-002 regression.
 *
 * A password change while streaming must take effect promptly: the very next
 * REST request must carry the new X-Password credential (previously the rebuild
 * only ran on a stream failure, so the stale password persisted until the
 * stream dropped or OBS restarted).
 *
 * The production path is: c64_update sees password_changed -> schedules a retry
 * -> c64_rebuild_rest_client -> c64_rest_client_retarget(new password). This
 * test drives that final, observable link against a localhost mock device and
 * asserts the header the device receives changes from the old to the new
 * password on the next request after the retarget. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-rest-client.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

bool c64_debug_logging = false;

#define MAX_CAPTURED 8

struct mock_device {
    int listen_fd;
    int port;
    pthread_t thread;
    bool stop; /* atomic access */
    char captured_password[MAX_CAPTURED][128];
    int captured_count;
};

/* Extract the X-Password header value (case-insensitive) from a raw request. */
static void extract_password(const char *req, char *out, size_t out_size)
{
    out[0] = '\0';
    const char *p = req;
    while (*p) {
        /* Compare header name case-insensitively. */
        if (strncasecmp(p, "X-Password:", 11) == 0) {
            p += 11;
            while (*p == ' ' || *p == '\t')
                p++;
            size_t i = 0;
            while (*p && *p != '\r' && *p != '\n' && i + 1 < out_size) {
                out[i++] = *p++;
            }
            out[i] = '\0';
            return;
        }
        /* advance to next line */
        const char *nl = strchr(p, '\n');
        if (!nl)
            break;
        p = nl + 1;
    }
}

static void *server_main(void *arg)
{
    struct mock_device *dev = arg;
    while (!__atomic_load_n(&dev->stop, __ATOMIC_RELAXED)) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int fd = accept(dev->listen_fd, (struct sockaddr *)&cli, &clilen);
        if (fd < 0) {
            if (__atomic_load_n(&dev->stop, __ATOMIC_RELAXED))
                break;
            continue;
        }

        char buf[4096];
        size_t total = 0;
        /* Read until end of headers. */
        while (total < sizeof(buf) - 1) {
            ssize_t n = read(fd, buf + total, sizeof(buf) - 1 - total);
            if (n <= 0)
                break;
            total += (size_t)n;
            buf[total] = '\0';
            if (strstr(buf, "\r\n\r\n"))
                break;
        }
        buf[total] = '\0';

        if (__atomic_load_n(&dev->captured_count, __ATOMIC_RELAXED) < MAX_CAPTURED) {
            extract_password(buf, dev->captured_password[dev->captured_count],
                             sizeof(dev->captured_password[dev->captured_count]));
            __atomic_add_fetch(&dev->captured_count, 1, __ATOMIC_RELAXED);
        }

        const char *resp = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        ssize_t w = write(fd, resp, strlen(resp));
        (void)w;
        close(fd);
    }
    return NULL;
}

int main(void)
{
    struct mock_device dev;
    memset(&dev, 0, sizeof(dev));

    dev.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(dev.listen_fd >= 0);
    int one = 1;
    setsockopt(dev.listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* ephemeral */
    assert(bind(dev.listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(listen(dev.listen_fd, 4) == 0);

    socklen_t alen = sizeof(addr);
    assert(getsockname(dev.listen_fd, (struct sockaddr *)&addr, &alen) == 0);
    dev.port = ntohs(addr.sin_port);

    pthread_create(&dev.thread, NULL, server_main, &dev);

    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d", dev.port);

    /* Streaming is healthy; the client was built with the old password. */
    c64_rest_client_t *client = c64_rest_client_create(url, "old-secret");
    assert(client != NULL);

    /* First request uses the old credential. */
    c64_rest_reset(client);

    /* User changes the password mid-session; the production retry path retargets
     * the existing client in place with the new credential. */
    assert(c64_rest_client_retarget(client, url, "new-secret"));

    /* The very next REST request must carry the NEW password. */
    c64_rest_reset(client);

    /* Give the server a moment to record the second request if needed. */
    for (int i = 0; i < 100 && __atomic_load_n(&dev.captured_count, __ATOMIC_RELAXED) < 2; i++) {
        usleep(10000);
    }

    __atomic_store_n(&dev.stop, true, __ATOMIC_RELAXED);
    /* Unblock accept() by connecting once. */
    int poke = socket(AF_INET, SOCK_STREAM, 0);
    if (poke >= 0) {
        (void)!connect(poke, (struct sockaddr *)&addr, sizeof(addr));
        close(poke);
    }
    pthread_join(dev.thread, NULL);
    close(dev.listen_fd);
    c64_rest_client_destroy(client);

    printf("captured %d requests: [%s] then [%s]\n", dev.captured_count,
           dev.captured_count > 0 ? dev.captured_password[0] : "",
           dev.captured_count > 1 ? dev.captured_password[1] : "");

    assert(dev.captured_count >= 2);
    assert(strcmp(dev.captured_password[0], "old-secret") == 0); /* before change */
    assert(strcmp(dev.captured_password[1], "new-secret") == 0); /* next request after change */

    printf("test_rest_password_change: PASS\n");
    return 0;
}
