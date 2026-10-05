/* Exercise the real legacy sender with local socket pairs and connection failures. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-av-sync.h"
#include "c64-network.h"
#include "c64-protocol.h"
#include "c64-record-network.h"

#include <assert.h>
#include <signal.h>
#include <string.h>

bool c64_debug_logging = false;
static int peers[2];
static size_t connection_count;
static size_t fail_connection;
static bool fail_send;

socket_t c64_create_tcp_socket(const char *host, uint32_t port)
{
    assert(strcmp(host, "192.168.1.64") == 0);
    assert(port == 64);
    const size_t index = connection_count++;
    if (index == fail_connection) {
        return INVALID_SOCKET_VALUE;
    }
    assert(index < 2);
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    peers[index] = pair[1];
    if (fail_send) {
        close(peers[index]);
    }
    return pair[0];
}

int c64_get_socket_error(void)
{
    return errno;
}

const char *c64_get_socket_error_string(int error)
{
    return strerror(error);
}

void c64_av_sync_on_video_pop(struct c64_source *context, enum c64_av_sync_origin origin, uint16_t frame_num,
                              uint64_t timestamp_ns)
{
    (void)context;
    (void)origin;
    (void)frame_num;
    (void)timestamp_ns;
}

void c64_av_sync_on_audio_pop(struct c64_source *context, enum c64_av_sync_origin origin, uint64_t timestamp_ns)
{
    (void)context;
    (void)origin;
    (void)timestamp_ns;
}

void c64_network_log_video_packet(struct c64_source *context, uint16_t sequence_num, uint16_t frame_num,
                                  uint16_t line_num, bool is_last_packet, size_t packet_size, size_t data_payload,
                                  int64_t jitter_us, bool is_all_white, uint64_t packet_timestamp_ns)
{
    (void)context;
    (void)sequence_num;
    (void)frame_num;
    (void)line_num;
    (void)is_last_packet;
    (void)packet_size;
    (void)data_payload;
    (void)jitter_us;
    (void)is_all_white;
    (void)packet_timestamp_ns;
}

void c64_network_log_audio_packet(struct c64_source *context, uint16_t sequence_num, size_t packet_size,
                                  uint16_t sample_count, int64_t jitter_us, bool has_signal,
                                  uint64_t packet_timestamp_ns)
{
    (void)context;
    (void)sequence_num;
    (void)packet_size;
    (void)sample_count;
    (void)jitter_us;
    (void)has_signal;
    (void)packet_timestamp_ns;
}

static void reset_connections(size_t failure)
{
    connection_count = 0;
    fail_connection = failure;
    fail_send = false;
}

static void check_command(size_t index, const uint8_t *expected, size_t size)
{
    uint8_t received[140];
    assert(recv(peers[index], received, sizeof(received), MSG_WAITALL) == (ssize_t)size);
    assert(memcmp(received, expected, size) == 0);
    close(peers[index]);
}

int main(void)
{
    // A closed peer must report delivery failure rather than terminate this
    // test process on POSIX's default SIGPIPE action.
    assert(signal(SIGPIPE, SIG_IGN) != SIG_ERR);

    reset_connections(0);
    assert(!c64_send_control_command_to("192.168.1.64", 64, false, 0, NULL));
    assert(connection_count == 1);

    reset_connections(SIZE_MAX);
    fail_send = true;
    assert(!c64_send_control_command_to("192.168.1.64", 64, false, 0, NULL));
    assert(connection_count == 1);

    reset_connections(1);
    assert(!c64_send_control_command_to("192.168.1.64", 64, true, 0, "127.0.0.1:21000"));
    assert(connection_count == 2);
    const uint8_t stop_video[] = {0x30, 0xFF, 0, 0};
    check_command(0, stop_video, sizeof(stop_video));

    reset_connections(SIZE_MAX);
    assert(c64_send_control_command_to("192.168.1.64", 64, false, 1, NULL));
    const uint8_t stop_audio[] = {0x31, 0xFF, 0, 0};
    check_command(0, stop_audio, sizeof(stop_audio));

    reset_connections(SIZE_MAX);
    assert(c64_send_control_command_to("192.168.1.64", 64, true, 0, "127.0.0.1:21000"));
    check_command(0, stop_video, sizeof(stop_video));
    const uint8_t start_video[] = {0x20, 0xFF, 17,  0,   0,   0,   '1', '2', '7', '.', '0',
                                   '.',  '0',  '.', '1', ':', '2', '1', '0', '0', '0'};
    check_command(1, start_video, sizeof(start_video));

    reset_connections(SIZE_MAX);
    assert(!c64_send_control_command_to(NULL, 64, false, 0, NULL));
    assert(!c64_send_control_command_to("", 64, false, 0, NULL));
    assert(!c64_send_control_command_to("192.168.1.64", 64, true, 0, NULL));
    assert(!c64_send_control_command_to("192.168.1.64", 64, true, 0, ""));
    assert(connection_count == 0);
    return 0;
}
