/*
 * bcls.c - CLS builtin
 *
 * CLS
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>

int builtin_cls(cmd_context_t *ctx, int argc, char **argv)
{
    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_CLS), stdout);
        return 0;
    }

    (void)ctx;
    (void)argc;
    (void)argv;

    if (libcmd_cls() < 0)
        return 1;
    fflush(stdout);
    return 0;
}
