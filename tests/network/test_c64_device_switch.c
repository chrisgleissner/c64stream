/* Edge cases of the device-switch stop queue (c64-device-switch.c): which
 * devices switched away from are still stopped, and which must not be. */

#include "c64-device-switch.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr)                                                                                                       \
    do {                                                                                                                  \
        if (!(expr)) {                                                                                                    \
            fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                \
            return 1;                                                                                                     \
        }                                                                                                                 \
    } while (0)

static int test_back_and_forth(void)
{
    c64_stop_queue_t queue = {0};
    // A -> B queues A; B -> A queues B and must cancel A's pending stop,
    // otherwise the stop would follow A's new start and end its stream.
    CHECK(c64_stop_queue_add(&queue, "192.168.1.146", 64, "", 0, NULL, 0));
    CHECK(c64_stop_queue_add(&queue, "192.168.1.13", 64, "", 0, NULL, 0));
    CHECK(c64_stop_queue_cancel(&queue, "192.168.1.146", 64));
    CHECK(queue.count == 1);
    CHECK(strcmp(queue.entries[0].host, "192.168.1.13") == 0);
    CHECK(!c64_stop_queue_cancel(&queue, "192.168.1.146", 64));
    return 0;
}

static int test_same_host_different_port(void)
{
    c64_stop_queue_t queue = {0};
    // Two devices behind one address: switching from :6400 to :6401 must still
    // stop :6400, and only a switch back to :6400 cancels that.
    CHECK(c64_stop_queue_add(&queue, "127.0.0.1", 6400, "", 0, NULL, 0));
    CHECK(!c64_stop_queue_cancel(&queue, "127.0.0.1", 6401));
    CHECK(queue.count == 1);
    CHECK(c64_stop_queue_add(&queue, "127.0.0.1", 6401, "", 0, NULL, 0));
    CHECK(queue.count == 2);
    CHECK(c64_stop_queue_cancel(&queue, "127.0.0.1", 6400));
    CHECK(queue.count == 1 && queue.entries[0].control_port == 6401);
    return 0;
}

static int test_repeated_switch_refreshes_entry(void)
{
    c64_stop_queue_t queue = {0};
    CHECK(c64_stop_queue_add(&queue, "c64u", 64, "old", 0, NULL, 0));
    CHECK(!c64_stop_queue_record_failure(&queue, 0, 5));
    CHECK(queue.entries[0].attempts == 1);
    // Switching away from the same device again: one entry, fresh attempts,
    // the latest credentials.
    CHECK(c64_stop_queue_add(&queue, "c64u", 64, "new", 0, NULL, 0));
    CHECK(queue.count == 1);
    CHECK(queue.entries[0].attempts == 0);
    CHECK(strcmp(queue.entries[0].password, "new") == 0);
    return 0;
}

static int test_nothing_to_stop(void)
{
    c64_stop_queue_t queue = {0};
    CHECK(!c64_stop_queue_add(&queue, "", 64, "", 0, NULL, 0));
    CHECK(!c64_stop_queue_add(&queue, "0.0.0.0", 64, "", 0, NULL, 0)); // receive-only source
    CHECK(!c64_stop_queue_add(&queue, NULL, 64, "", 0, NULL, 0));
    CHECK(queue.count == 0);
    return 0;
}

static int test_overflow_drops_oldest(void)
{
    c64_stop_queue_t queue = {0};
    char dropped[64];
    char host[32];
    for (int i = 0; i < C64_STOP_QUEUE_MAX; i++) {
        snprintf(host, sizeof(host), "10.0.0.%d", i + 1);
        CHECK(c64_stop_queue_add(&queue, host, 64, "", 0, dropped, sizeof(dropped)));
        CHECK(dropped[0] == '\0');
    }
    CHECK(c64_stop_queue_add(&queue, "10.0.0.99", 64, "", 0, dropped, sizeof(dropped)));
    CHECK(strcmp(dropped, "10.0.0.1") == 0);
    CHECK(queue.count == C64_STOP_QUEUE_MAX);
    CHECK(strcmp(queue.entries[C64_STOP_QUEUE_MAX - 1].host, "10.0.0.99") == 0);
    return 0;
}

static int test_give_up_after_attempts(void)
{
    c64_stop_queue_t queue = {0};
    CHECK(c64_stop_queue_add(&queue, "offline", 64, "", 0, NULL, 0));
    for (int i = 1; i < 5; i++) {
        CHECK(!c64_stop_queue_record_failure(&queue, 0, 5));
    }
    CHECK(c64_stop_queue_record_failure(&queue, 0, 5));
    c64_stop_queue_remove_at(&queue, 0);
    CHECK(queue.count == 0);
    c64_stop_queue_remove_at(&queue, 0); // out of range is harmless
    CHECK(!c64_stop_queue_record_failure(&queue, 0, 5));
    return 0;
}

int main(void)
{
    if (test_back_and_forth() || test_same_host_different_port() || test_repeated_switch_refreshes_entry() ||
        test_nothing_to_stop() || test_overflow_drops_oldest() || test_give_up_after_attempts()) {
        return 1;
    }
    puts("device switch queue tests passed");
    return 0;
}
