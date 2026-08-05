/*
 * bendlocal.c - ENDLOCAL builtin
 *
 * ENDLOCAL
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int builtin_endlocal(cmd_context_t *ctx, int argc, char **argv)
{
    cmd_local_env_t *loc;
    int i;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_ENDLOCAL), stdout);
        return 0;
    }

    if (ctx->local_depth <= 0) {
        /* Nothing to restore; silently succeed */
        return 0;
    }

    ctx->local_depth--;
    loc = &ctx->local_stack[ctx->local_depth];

    /* Restore working directory */
    if (loc->cwd[0])
        libcmd_chdir(loc->cwd);

    /* Restore flags */
    ctx->echo           = loc->echo;
    ctx->extensions     = loc->extensions;
    ctx->delayed_expand = loc->delayed_expand;

    /* Restore environment */
    if (loc->env_vars) {
        /* Clear current environment */
        char **cur = libcmd_get_environ();
        if (cur) {
            /* Collect names to unset */
            int count = 0;
            while (cur[count]) count++;
            for (i = count - 1; i >= 0; i--) {
                char name[256];
                char *eq = strchr(cur[i], '=');
                if (eq) {
                    size_t nlen = (size_t)(eq - cur[i]);
                    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
                    memcpy(name, cur[i], nlen);
                    name[nlen] = '\0';
                    libcmd_unsetenv(name);
                }
            }
        }

        /* Restore saved environment */
        for (i = 0; loc->env_vars[i]; i++) {
            libcmd_putenv(libcmd_strdup(loc->env_vars[i]));
            free(loc->env_vars[i]);
        }
        free(loc->env_vars);
        loc->env_vars  = NULL;
        loc->env_count = 0;
    }

    return 0;
}
