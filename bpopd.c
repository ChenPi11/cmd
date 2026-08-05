/*
 * bpopd.c - POPD builtin
 *
 * POPD
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>

int builtin_popd(cmd_context_t *ctx, int argc, char **argv)
{
    char *saved;

    if (argc == 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_POPD), stdout);
        return 0;
    }

    if (ctx->dir_stack_top <= 0) {
        /* No directory on stack; silently succeed like CMD.EXE */
        return 0;
    }

    ctx->dir_stack_top--;
    saved = ctx->dir_stack[ctx->dir_stack_top];
    ctx->dir_stack[ctx->dir_stack_top] = NULL;

    if (libcmd_chdir(saved) < 0) {
        fprintf(stderr, cmd_gettext(MSG_ERR_POPD_RESTORE),
                saved, libcmd_strerror());
        free(saved);
        return 1;
    }

    free(saved);
    return 0;
}
