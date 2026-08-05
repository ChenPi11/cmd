/*
 * btitle.c - TITLE builtin
 *
 * TITLE [string]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

int builtin_title(cmd_context_t *ctx, int argc, char **argv)
{
    char title[512];
    int  i;

    (void)ctx;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_TITLE), stdout);
        return 0;
    }

    if (argc <= 1) {
        /* No arguments: clear the title (cmd.exe behaviour) */
        libcmd_set_title("");
        return 0;
    }

    title[0] = '\0';
    for (i = 1; i < argc; i++) {
        if (i > 1)
            strncat(title, " ", sizeof(title) - strlen(title) - 1);
        strncat(title, argv[i], sizeof(title) - strlen(title) - 1);
    }

    libcmd_set_title(title);
    return 0;
}
