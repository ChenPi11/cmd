/*
 * bvol.c - VOL builtin
 *
 * VOL [drive:]
 *
 * Tries to read the root mount point's device name and device number.
 * The device name comes from the `mount` command; the device number
 * (major/minor) is read via stat() and is Linux-specific.  On any
 * failure the output quietly falls back to "root" / 0000-0000 without
 * disturbing normal operation.
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __linux__
#include <sys/sysmacros.h>
#endif

/* Read the device name of the root mount point from `mount`. */
static int read_root_device(char *buf, size_t size)
{
    FILE *f;
    char line[1024];
    int ok = 0;

    f = libcmd_popen("command -v mount >/dev/null 2>&1 && mount", "r");
    if (f == NULL)
        return -1;

    while (fgets(line, (int)sizeof(line), f) != NULL) {
        char *p = line;
        char *sp;

        /* Format: <device> on <mountpoint> [type <fstype>] [(opts)] */
        while (*p == ' ' || *p == '\t')
            p++;
        sp = strchr(p, ' ');
        if (sp == NULL)
            continue;
        *sp++ = '\0';
        while (*sp == ' ')
            sp++;
        if (strncmp(sp, "on ", 3) != 0)
            continue;
        sp += 3;
        while (*sp == ' ')
            sp++;
        if (strncmp(sp, "/ ", 2) == 0 || strcmp(sp, "/\n") == 0) {
            libcmd_sprintf_s(buf, size, "%s", p);
            ok = 1;
            break;
        }
    }

    libcmd_pclose(f);
    return ok ? 0 : -1;
}

int builtin_vol(cmd_context_t *ctx, int argc, char **argv)
{
    char device[256];
    unsigned long maj = 0, min = 0;
    struct stat st;

    (void)ctx;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_VOL), stdout);
        return 0;
    }

    /* Unix has a single root volume; a drive argument is accepted and
     * ignored, matching the root mount point */

    if (read_root_device(device, sizeof(device)) == 0)
        printf(cmd_gettext(MSG_INFO_VOLUME_LABEL), device);
    else
        printf(cmd_gettext(MSG_INFO_VOLUME_LABEL), "root");

    if (stat("/", &st) == 0) {
#ifdef __linux__
        maj = (unsigned long)major(st.st_dev);
        min = (unsigned long)minor(st.st_dev);
#endif
    }

    if (maj != 0 || min != 0)
    {
        maj &= 0xFFFF;
        min &= 0xFFFF;
    }
    printf(cmd_gettext(MSG_INFO_VOLUME_SERIAL), maj, min);

    return 0;
}
