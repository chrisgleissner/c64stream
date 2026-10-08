#include "c64-device-switch.h"

#include <stdio.h>
#include <string.h>

bool c64_stop_queue_add(c64_stop_queue_t *queue, const char *host, uint32_t control_port, const char *password,
                        uint64_t rest_demoted_until_ns, char *dropped_host, size_t dropped_host_size)
{
    if (dropped_host && dropped_host_size) {
        dropped_host[0] = '\0';
    }
    if (!queue || !host || !host[0] || !strcmp(host, "0.0.0.0")) {
        return false;
    }
    size_t slot = queue->count;
    for (size_t i = 0; i < queue->count; i++) {
        if (!strcmp(queue->entries[i].host, host) && queue->entries[i].control_port == control_port) {
            slot = i;
            break;
        }
    }
    if (slot == C64_STOP_QUEUE_MAX) {
        if (dropped_host && dropped_host_size) {
            snprintf(dropped_host, dropped_host_size, "%s", queue->entries[0].host);
        }
        c64_stop_queue_remove_at(queue, 0);
        slot = queue->count;
    }
    c64_stop_entry_t *entry = &queue->entries[slot];
    snprintf(entry->host, sizeof(entry->host), "%s", host);
    entry->control_port = control_port;
    snprintf(entry->password, sizeof(entry->password), "%s", password ? password : "");
    entry->rest_demoted_until_ns = rest_demoted_until_ns;
    entry->attempts = 0;
    if (slot == queue->count) {
        queue->count++;
    }
    return true;
}

bool c64_stop_queue_cancel(c64_stop_queue_t *queue, const char *host, uint32_t control_port)
{
    if (!queue || !host) {
        return false;
    }
    for (size_t i = 0; i < queue->count; i++) {
        if (!strcmp(queue->entries[i].host, host) && queue->entries[i].control_port == control_port) {
            c64_stop_queue_remove_at(queue, i);
            return true;
        }
    }
    return false;
}

void c64_stop_queue_remove_at(c64_stop_queue_t *queue, size_t index)
{
    if (!queue || index >= queue->count) {
        return;
    }
    memmove(&queue->entries[index], &queue->entries[index + 1], (queue->count - index - 1) * sizeof(queue->entries[0]));
    queue->count--;
    memset(&queue->entries[queue->count], 0, sizeof(queue->entries[0]));
}

bool c64_stop_queue_record_failure(c64_stop_queue_t *queue, size_t index, uint32_t max_attempts)
{
    if (!queue || index >= queue->count) {
        return false;
    }
    return ++queue->entries[index].attempts >= max_attempts;
}
