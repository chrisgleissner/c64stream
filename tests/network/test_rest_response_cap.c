/* C64 Stream - C64STR-019 regression.
 *
 * A malicious/malfunctioning device could return an arbitrarily large HTTP
 * response. The client sets CURLOPT_MAXFILESIZE_LARGE (1 MiB) so an oversized
 * response is aborted before download. Adversarial review found the option was
 * wiped by the per-request curl_easy_reset and never re-applied, so this test
 * also guards that regression: an over-limit response must make the request
 * fail, while a normal small response still succeeds. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-rest-client.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

bool c64_debug_logging = false;

struct mock_device {
    int listen_fd;
    struct sockaddr_in addr;
    pthread_t thread;
    bool stop;
    size_t body_bytes;
};

static void *server_main(void *arg)
{
    struct mock_device *dev = arg;
    static char chunk[65536];
    memset(chunk, 'A', sizeof(chunk));

    while (!__atomic_load_n(&dev->stop, __ATOMIC_RELAXED)) {
        int fd = accept(dev->listen_fd, NULL, NULL);
        if (fd < 0) {
            if (__atomic_load_n(&dev->stop, __ATOMIC_RELAXED))
                break;
            continue;
        }
        char buf[2048];
        while (read(fd, buf, sizeof(buf)) > 0) {
            if (strstr(buf, "\r\n\r\n"))
                break;
        }
        char header[128];
        int n = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n", dev->body_bytes);
        (void)!write(fd, header, (size_t)n);
        size_t remaining = dev->body_bytes;
        while (remaining > 0) {
            size_t want = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
            ssize_t w = write(fd, chunk, want);
            if (w <= 0)
                break;
            remaining -= (size_t)w;
        }
        close(fd);
    }
    return NULL;
}

static struct mock_device *start_server(size_t body_bytes)
{
    static struct mock_device dev;
    memset(&dev, 0, sizeof(dev));
    dev.body_bytes = body_bytes;
    dev.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(dev.listen_fd >= 0);
    int one = 1;
    setsockopt(dev.listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    dev.addr.sin_family = AF_INET;
    dev.addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dev.addr.sin_port = 0;
    assert(bind(dev.listen_fd, (struct sockaddr *)&dev.addr, sizeof(dev.addr)) == 0);
    assert(listen(dev.listen_fd, 4) == 0);
    socklen_t alen = sizeof(dev.addr);
    assert(getsockname(dev.listen_fd, (struct sockaddr *)&dev.addr, &alen) == 0);
    pthread_create(&dev.thread, NULL, server_main, &dev);
    return &dev;
}

static void stop_server(struct mock_device *dev)
{
    __atomic_store_n(&dev->stop, true, __ATOMIC_RELAXED);
    int poke = socket(AF_INET, SOCK_STREAM, 0);
    if (poke >= 0) {
        (void)!connect(poke, (struct sockaddr *)&dev->addr, sizeof(dev->addr));
        close(poke);
    }
    pthread_join(dev->thread, NULL);
    close(dev->listen_fd);
}

static c64_rest_client_t *client_for(struct mock_device *dev)
{
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d", ntohs(dev->addr.sin_port));
    return c64_rest_client_create(url, NULL);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    /* A normal, small response is accepted. Two requests on the same client
     * exercise the per-request curl_easy_reset path (where MAXFILESIZE was
     * previously lost). */
    struct mock_device *dev = start_server(64);
    c64_rest_client_t *client = client_for(dev);
    assert(client != NULL);
    assert(c64_rest_reset(client) && "small response must be accepted");
    assert(c64_rest_reset(client) && "second small response must be accepted");
    c64_rest_client_destroy(client);
    stop_server(dev);

    /* A response beyond the 1 MiB cap must be rejected (aborted by
     * MAXFILESIZE), not downloaded. */
    dev = start_server((size_t)3 * 1024 * 1024);
    client = client_for(dev);
    assert(client != NULL);
    assert(!c64_rest_reset(client) && "oversized response must be rejected");
    /* Still functional afterwards for a normal request path. */
    c64_rest_client_destroy(client);
    stop_server(dev);

    printf("test_rest_response_cap: PASS\n");
    return 0;
}
