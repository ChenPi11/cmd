/*
 * bprompt.c - PROMPT builtin
 *
 * PROMPT [text]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

int builtin_prompt(cmd_context_t *ctx, int argc, char **argv)
{
    int i;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_PROMPT), stdout);
        return 0;
    }

    if (argc <= 1) {
        /* Reset to default */
        libcmd_sprintf_s(ctx->prompt_string, sizeof(ctx->prompt_string), "$P$G");
        return 0;
    }

    /* Rebuild prompt string from all args after PROMPT */
    {
        char buf[256];
        buf[0] = '\0';
        for (i = 1; i < argc; i++) {
            if (i > 1) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
        }
        libcmd_sprintf_s(ctx->prompt_string, sizeof(ctx->prompt_string),
                         "%s", buf);
    }

    /* Also update PROMPT environment variable */
    libcmd_setenv("PROMPT", ctx->prompt_string, 1);
    return 0;
}
