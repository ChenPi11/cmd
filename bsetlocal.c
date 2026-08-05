/*
 * bsetlocal.c - SETLOCAL builtin
 *
 * SETLOCAL [ENABLEEXTENSIONS | DISABLEEXTENSIONS]
 *          [ENABLEDELAYEDEXPANSION | DISABLEELAYEDEXPANSION]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int builtin_setlocal(cmd_context_t *ctx, int argc, char **argv)
{
    cmd_local_env_t *loc;
    char **env;
    int env_count, i;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_SETLOCAL), stdout);
        return 0;
    }

    if (ctx->local_depth >= CMD_SETLOCAL_DEPTH) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_SETLOCAL_NESTING));
        return 1;
    }

    loc = &ctx->local_stack[ctx->local_depth];

    /* Save current working directory */
    if (libcmd_getcwd(loc->cwd, sizeof(loc->cwd)) == NULL)
        loc->cwd[0] = '\0';

    /* Save echo and extension flags */
    loc->echo       = ctx->echo;
    loc->extensions = ctx->extensions;
    loc->delayed_expand = ctx->delayed_expand;

    /* Save a copy of the environment */
    env = libcmd_get_environ();
    env_count = 0;
    if (env) {
        for (i = 0; env[i]; i++) env_count++;
    }

    loc->env_vars  = (char **)calloc((size_t)env_count + 1, sizeof(char *));
    loc->env_count = env_count;

    if (loc->env_vars && env) {
        for (i = 0; i < env_count; i++) {
            loc->env_vars[i] = libcmd_strdup(env[i]);
        }
        loc->env_vars[env_count] = NULL;
    }

    ctx->local_depth++;

    /* Process flags */
    for (i = 1; i < argc; i++) {
        if (libcmd_strcasecmp(argv[i], "ENABLEEXTENSIONS") == 0)
            ctx->extensions = 1;
        else if (libcmd_strcasecmp(argv[i], "DISABLEEXTENSIONS") == 0)
            ctx->extensions = 0;
        else if (libcmd_strcasecmp(argv[i], "ENABLEDELAYEDEXPANSION") == 0)
            ctx->delayed_expand = 1;
        else if (libcmd_strcasecmp(argv[i], "DISABLEDELAYEDEXPANSION") == 0)
            ctx->delayed_expand = 0;
    }

    return 0;
}
