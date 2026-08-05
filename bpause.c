/*
 * bpause.c - PAUSE builtin
 *
 * PAUSE
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>

int builtin_pause(cmd_context_t *ctx, int argc, char **argv)
{
    (void)ctx;
    (void)argc;
    (void)argv;

    fputs(cmd_gettext(MSG_ASK_PRESS_KEY), stdout);
    fflush(stdout);
    getchar();
    fputc('\n', stdout);
    return 0;
}
