/*
 * bcd.c - CD / CHDIR builtin
 *
 * CHDIR [/D] [drive:][path]
 * CHDIR [..]
 * CD    [/D] [drive:][path]
 * CD    [..]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

int builtin_cd(cmd_context_t *ctx, int argc, char **argv)
{
    int slash_d = 0;
    int i;
    const char *target = NULL;

    (void)ctx;

    for (i = 1; i < argc; i++) {
        if (libcmd_strcasecmp(argv[i], "/D") == 0) {
            slash_d = 1;
        } else if (libcmd_strcasecmp(argv[i], "/help") == 0 ||
                   libcmd_strcasecmp(argv[i], "/?") == 0) {
            fputs(cmd_gettext(MSG_HELP_CD), stdout);
            return 0;
        } else {
            target = argv[i];
        }
    }

    /* No target: print current directory */
    if (target == NULL) {
        char cwd[CMD_MAX_PATH];
        if (libcmd_getcwd(cwd, sizeof(cwd)) != NULL) {
            puts(cwd);
        } else {
            fprintf(stderr, cmd_gettext(MSG_ERR_CD_GETCWD),
                    libcmd_strerror());
            return 1;
        }
        return 0;
    }

    /* On Unix there are no drive letters; strip "C:" prefix if present */
    if (target[0] != '\0' && target[1] == ':') {
        /* Skip drive letter */
        target += 2;
        if (target[0] == '\0') {
            /* Just "C:" - print current dir of that drive (treat as /) */
            char cwd[CMD_MAX_PATH];
            if (libcmd_getcwd(cwd, sizeof(cwd)) != NULL)
                puts(cwd);
            return 0;
        }
    }

    /* With /D, cmd.exe also switches drives.  Unix has no drive letters,
     * so the directory change below is all there is to do; /D paths
     * (e.g. "cd /D C:\tmp") are handled by the drive-strip above. */
    (void)slash_d;

    if (libcmd_chdir(target) < 0) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_PATH_NOT_FOUND));
        return 1;
    }

    return 0;
}
