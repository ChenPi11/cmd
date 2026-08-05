/*
 * bmd.c - MD / MKDIR builtin
 *
 * MD [drive:]path
 * MKDIR [drive:]path
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

int builtin_md(cmd_context_t *ctx, int argc, char **argv)
{
    int i;
    int ret = 0;

    (void)ctx;

    if (argc <= 1 || (argc == 2 && libcmd_strcasecmp(argv[1], "/?") == 0)) {
        fputs(cmd_gettext(MSG_HELP_MD), stdout);
        return argc <= 1 ? 1 : 0;
    }

    for (i = 1; i < argc; i++) {
        const char *path = argv[i];

        if (libcmd_is_switch(argv[i], "")) continue;

        /* Strip drive letter if present */
        if (path[0] && path[1] == ':') path += 2;

        if (libcmd_mkdir(path, 1) < 0) {
            fprintf(stderr, cmd_gettext(MSG_ERR_ALREADY_EXISTS), path);
            ret = 1;
        }
    }
    return ret;
}
