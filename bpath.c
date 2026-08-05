/*
 * bpath.c - PATH builtin
 *
 * PATH [[drive:]path[;...]]
 * PATH ;  (clears PATH)
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

int builtin_path(cmd_context_t *ctx, int argc, char **argv)
{
    (void)ctx;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_PATH), stdout);
        return 0;
    }

    if (argc <= 1) {
        const char *p = libcmd_getenv("PATH");
        if (p)
            printf("PATH=%s\n", p);
        else
            fputs(cmd_gettext(MSG_INFO_NO_PATH), stdout);
        return 0;
    }

    /* argc > 1: set the PATH */
    if (strcmp(argv[1], ";") == 0) {
        libcmd_unsetenv("PATH");
    } else {
        char buf[4096];
        int i;
        buf[0] = '\0';
        for (i = 1; i < argc; i++) {
            if (i > 1) strncat(buf, ";", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
        }
        libcmd_setenv("PATH", buf, 1);
    }
    return 0;
}
