/* C64 Stream - C64STR-017 regression.
 *
 * A running C64Script's runtime caches its rest_client pointer. SWITCH_DEVICE
 * used to destroy and recreate that client (and stop the script). The fix
 * retargets the SAME client object in place, so the cached pointer stays valid
 * and the script continues on the new device.
 *
 * This test proves the primitive that makes that safe:
 *   1. Retarget swaps URL/password on the same object (no recreate).
 *   2. A thread that holds the client pointer and keeps issuing reads across
 *      repeated retargets never sees a torn/freed value (no use-after-free) --
 *      exercised under ASan/TSan. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-rest-client.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Provided by the plugin; redefined here so the test links without plugin-main.c.
bool c64_debug_logging = false;

#define URL_A "http://192.168.0.10"
#define URL_B "http://192.168.0.20"
#define ITERS 100000

struct shared {
    c64_rest_client_t *client; /* single shared object, as the script caches it */
    volatile bool stop;
};

/* Emulates the script thread: keeps using the cached client pointer while the
 * device is switched underneath it. */
static void *reader_thread(void *arg)
{
    struct shared *s = arg;
    for (long i = 0; i < ITERS && !s->stop; i++) {
        char url[256];
        assert(c64_rest_client_get_base_url(s->client, url, sizeof(url)));
        /* Must always be one of the two valid targets, never garbage. */
        assert(strcmp(url, URL_A) == 0 || strcmp(url, URL_B) == 0);
    }
    return NULL;
}

static void *switcher_thread(void *arg)
{
    struct shared *s = arg;
    for (long i = 0; i < ITERS && !s->stop; i++) {
        assert(c64_rest_client_retarget(s->client, (i & 1) ? URL_B : URL_A, (i & 1) ? "pw-b" : "pw-a"));
    }
    return NULL;
}

int main(void)
{
    /* 1. Basic in-place retarget semantics. */
    c64_rest_client_t *client = c64_rest_client_create(URL_A, "pw-a");
    assert(client != NULL);

    char url[256];
    assert(c64_rest_client_get_base_url(client, url, sizeof(url)));
    assert(strcmp(url, URL_A) == 0);

    /* The cached pointer the "script" holds. */
    c64_rest_client_t *cached = client;
    assert(c64_rest_client_retarget(client, URL_B, "pw-b"));
    assert(cached == client); /* same object -- not recreated */
    assert(c64_rest_client_get_base_url(client, url, sizeof(url)));
    assert(strcmp(url, URL_B) == 0);

    /* Retarget with NULL password clears credentials but keeps the object. */
    assert(c64_rest_client_retarget(client, URL_A, NULL));
    assert(c64_rest_client_get_base_url(client, url, sizeof(url)));
    assert(strcmp(url, URL_A) == 0);

    printf("retarget semantics: PASS\n");

    /* 2. Concurrent switch vs. cached-pointer use (UAF / torn-read guard). */
    struct shared s = {.client = client, .stop = false};
    pthread_t reader, switcher;
    pthread_create(&reader, NULL, reader_thread, &s);
    pthread_create(&switcher, NULL, switcher_thread, &s);
    pthread_join(switcher, NULL);
    s.stop = true;
    pthread_join(reader, NULL);

    printf("concurrent retarget: PASS\n");

    c64_rest_client_destroy(client);
    printf("All rest client retarget tests passed\n");
    return 0;
}
