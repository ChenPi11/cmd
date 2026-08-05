/*
 * bverify.c - VERIFY builtin
 *
 * VERIFY [ON | OFF]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

int builtin_verify(cmd_context_t *ctx, int argc, char **argv)
{
    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_VERIFY), stdout);
        return 0;
    }

    if (argc >= 2) {
        if (libcmd_strcasecmp(argv[1], "ON") == 0) {
            ctx->verify_on = 1;
        } else if (libcmd_strcasecmp(argv[1], "OFF") == 0) {
            ctx->verify_on = 0;
        } else {
            fprintf(stderr, "%s", cmd_gettext(MSG_ERR_VERIFY_ARG));
            return 1;
        }
    }

    printf(cmd_gettext(MSG_INFO_VERIFY_IS), ctx->verify_on ? "on" : "off");
    return 0;
}
