/*
 * becho.c - ECHO builtin
 *
 * ECHO [ON | OFF]
 * ECHO [message]
 * ECHO.
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

int builtin_echo(cmd_context_t *ctx, int argc, char **argv)
{
    if (argc <= 1) {
        /* Print current echo state */
        printf(cmd_gettext(MSG_INFO_ECHO_IS), ctx->echo ? cmd_gettext(MSG_ON) : cmd_gettext(MSG_OFF));
        return 0;
    }

    if (libcmd_strcasecmp(argv[1], "on") == 0 && argc == 2) {
        ctx->echo = 1;
        return 0;
    }
    if (libcmd_strcasecmp(argv[1], "off") == 0 && argc == 2) {
        ctx->echo = 0;
        return 0;
    }

    /* ECHO. prints an empty line */
    if (argv[1][0] == '.' && argv[1][1] == '\0' && argc == 2) {
        fputc('\n', stdout);
        return 0;
    }

    /* Print the message (all args joined with space) */
    {
        int i;
        for (i = 1; i < argc; i++) {
            if (i > 1) fputc(' ', stdout);
            fputs(argv[i], stdout);
        }
        fputc('\n', stdout);
    }
    return 0;
}
