/* C64 Stream - C64STR-004 regression.
 *
 * Directory creation used to shell out to system("mkdir ...") at module load,
 * which is slow, flashes a console window on Windows, and is a shell-injection
 * risk. It is replaced by c64_create_directory_recursive; this verifies that
 * helper creates a nested path natively and is idempotent. */
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "c64-file.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool c64_debug_logging = false;

static bool is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

int main(void)
{
    char base[] = "/tmp/c64fu_XXXXXX";
    assert(mkdtemp(base) != NULL);

    /* A multi-level nested path is created in full. */
    char nested[512];
    snprintf(nested, sizeof(nested), "%s/a/b/c/d", base);
    assert(c64_create_directory_recursive(nested));
    assert(is_dir(nested));

    char mid[512];
    snprintf(mid, sizeof(mid), "%s/a/b", base);
    assert(is_dir(mid)); /* intermediate levels exist too */

    /* Idempotent: creating an already-existing tree succeeds. */
    assert(c64_create_directory_recursive(nested));
    assert(is_dir(nested));

    /* cleanup (bottom-up) */
    char p[512];
    snprintf(p, sizeof(p), "%s/a/b/c/d", base);
    rmdir(p);
    snprintf(p, sizeof(p), "%s/a/b/c", base);
    rmdir(p);
    snprintf(p, sizeof(p), "%s/a/b", base);
    rmdir(p);
    snprintf(p, sizeof(p), "%s/a", base);
    rmdir(p);
    rmdir(base);

    printf("test_file_utils: PASS\n");
    return 0;
}
