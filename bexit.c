/*
 * bexit.c - EXIT builtin
 *
 * EXIT [/B] [exitCode]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>

int builtin_exit(cmd_context_t *ctx, int argc, char **argv)
{
    int slash_b    = 0;
    int exit_code  = ctx->exit_code;
    int i;

    for (i = 1; i < argc; i++) {
        if (libcmd_strcasecmp(argv[i], "/B") == 0) {
            slash_b = 1;
        } else if (libcmd_strcasecmp(argv[i], "/?") == 0) {
            fputs(cmd_gettext(MSG_HELP_EXIT), stdout);
            return 0;
        } else {
            exit_code = atoi(argv[i]);
        }
    }

    ctx->exit_code  = exit_code;
    ctx->exit_value = exit_code;

    if (!slash_b) {
        /* Exit the whole process */
        exit(exit_code);
    }

    /* /B: stop current batch file only */
    ctx->stop_batch = 1;
    return exit_code;
}
