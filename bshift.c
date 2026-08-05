/*
 * bshift.c - SHIFT builtin
 *
 * SHIFT [/n]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>

int builtin_shift(cmd_context_t *ctx, int argc, char **argv)
{
    int n = 1;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_SHIFT),
              stdout);
        return 0;
    }

    if (argc >= 2 && argv[1][0] == '/' && argv[1][1] >= '0' && argv[1][1] <= '8') {
        n = argv[1][1] - '0';
    }

    if (ctx->call_depth > 0) {
        cmd_call_frame_t *frame = ctx->call_stack[ctx->call_depth - 1];
        frame->shift_count += n;
    }

    return 0;
}
