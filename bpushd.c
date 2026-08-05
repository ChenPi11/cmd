/*
 * bpushd.c - PUSHD builtin
 *
 * PUSHD [path | ..]
 *
 * Saves the current directory then changes to path.
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int builtin_pushd(cmd_context_t *ctx, int argc, char **argv)
{
    char cwd[CMD_MAX_PATH];
    const char *target;
    int i;

    for (i = 1; i < argc; i++) {
        if (libcmd_strcasecmp(argv[i], "/?") == 0) {
            fputs(cmd_gettext(MSG_HELP_PUSHD),
                  stdout);
            return 0;
        }
    }

    target = (argc > 1) ? argv[1] : NULL;

    /* Save current directory */
    if (libcmd_getcwd(cwd, sizeof(cwd)) == NULL) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_PUSHD_GETCWD));
        return 1;
    }

    if (ctx->dir_stack_top >= CMD_DIRSTACK_DEPTH) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_PUSHD_OVERFLOW));
        return 1;
    }

    ctx->dir_stack[ctx->dir_stack_top] = libcmd_strdup(cwd);
    if (ctx->dir_stack[ctx->dir_stack_top] == NULL) return 1;
    ctx->dir_stack_top++;

    /* Change to target if specified */
    if (target != NULL) {
        if (libcmd_chdir(target) < 0) {
            fprintf(stderr, cmd_gettext(MSG_ERR_PUSHD_CHANGE),
                    target, libcmd_strerror());
            /* Pop back */
            ctx->dir_stack_top--;
            free(ctx->dir_stack[ctx->dir_stack_top]);
            ctx->dir_stack[ctx->dir_stack_top] = NULL;
            return 1;
        }
    }

    return 0;
}
