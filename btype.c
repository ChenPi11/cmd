/*
 * btype.c - TYPE builtin
 *
 * TYPE [drive:][path]filename
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

int builtin_type(cmd_context_t *ctx, int argc, char **argv)
{
    int i;
    int ret = 0;

    (void)ctx;

    if (argc <= 1 || (argc == 2 && libcmd_strcasecmp(argv[1], "/?") == 0)) {
        fputs(cmd_gettext(MSG_HELP_TYPE), stdout);
        return argc <= 1 ? 1 : 0;
    }

    for (i = 1; i < argc; i++) {
        FILE *f;
        char buf[4096];
        size_t nr;

        /* Skip switches */
        if (argv[i][0] == '/' && libcmd_is_switch(argv[i], ""))
            continue;

        f = fopen(argv[i], "r");
        if (f == NULL) {
            fprintf(stderr, cmd_gettext(MSG_ERR_FILE_NOT_FOUND_NAMED), argv[i]);
            ret = 1;
            continue;
        }

        /* If multiple files, print a header */
        if (argc > 2)
            printf("\n%s\n\n", argv[i]);

        while ((nr = fread(buf, 1, sizeof(buf), f)) > 0) {
            fwrite(buf, 1, nr, stdout);
        }

        fclose(f);
    }

    return ret;
}
