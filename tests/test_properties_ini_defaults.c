/*
Sanity test: shipped data/properties.ini contains the defaults that OBS "Defaults"
button should restore via c64_set_property_defaults() -> c64_load_configuration().
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void read_all(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "FAIL: could not open %s\n", path);
        exit(1);
    }
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
}

static void must_contain(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !strstr(haystack, needle)) {
        fprintf(stderr, "FAIL: expected to find '%s'\n", needle ? needle : "(null)");
        exit(1);
    }
}

int main(void)
{
    char buf[16384];
    read_all("../data/properties.ini", buf, sizeof(buf));

    must_contain(buf, "[network]");
    must_contain(buf, "c64_host=c64u");
    must_contain(buf, "dns_server_ip=192.168.1.1");
    must_contain(buf, "video_port=11000");
    must_contain(buf, "audio_port=11001");
    must_contain(buf, "control_port=64");
    must_contain(buf, "auto_detect_ip=true");
    must_contain(buf, "buffer_delay_ms=10");

    must_contain(buf, "[recording]");
    must_contain(buf, "save_folder=");
    must_contain(buf, "record_frames=false");
    must_contain(buf, "record_video=false");
    must_contain(buf, "record_csv=false");

    must_contain(buf, "[debug]");
    must_contain(buf, "debug_logging=false");

    must_contain(buf, "[effects]");
    must_contain(buf, "scan_line_distance=0.0");
    must_contain(buf, "scan_line_strength=0.0");
    must_contain(buf, "pixel_width=1.0");
    must_contain(buf, "pixel_height=1.0");
    must_contain(buf, "blur_strength=0.0");
    must_contain(buf, "bloom_strength=0.0");
    must_contain(buf, "afterglow_duration_ms=0");
    must_contain(buf, "afterglow_curve=2");
    must_contain(buf, "tint_mode=0");
    must_contain(buf, "tint_strength=0.0");

    printf("OK\n");
    return 0;
}
