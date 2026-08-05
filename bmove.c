/*
 * bmove.c - MOVE builtin
 *
 * MOVE [/Y | /-Y] [drive:][path]filename [...] destination
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int builtin_move(cmd_context_t *ctx, int argc, char **argv)
{
    int opt_y = 1;
    int i;
    const char *dst = NULL;
    const char **srcs = NULL;
    int src_count = 0;
    int ret = 0;

    (void)ctx;

    if (argc <= 1 || (argc == 2 && libcmd_strcasecmp(argv[1], "/?") == 0)) {
        fputs(cmd_gettext(MSG_HELP_MOVE), stdout);
        return argc <= 1 ? 1 : 0;
    }

    srcs = (const char **)malloc((size_t)argc * sizeof(char *));
    if (!srcs) return 1;

    for (i = 1; i < argc; i++) {
        if (libcmd_strcasecmp(argv[i], "/Y")  == 0) { opt_y = 1; continue; }
        if (libcmd_strcasecmp(argv[i], "/-Y") == 0) { opt_y = 0; continue; }
        if (argv[i][0] == '/' && libcmd_is_switch(argv[i], "Y")) continue;
        srcs[src_count++] = argv[i];
    }

    if (src_count < 2) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_MOVE_REQUIRES));
        free(srcs);
        return 1;
    }

    dst = srcs[--src_count];

    for (i = 0; i < src_count; i++) {
        const char *src = srcs[i];

        /* Expand wildcards */
        if (strchr(src, '*') || strchr(src, '?')) {
            libcmd_glob_result_t gr;
            if (libcmd_glob(src, &gr) == 0) {
                size_t j;
                for (j = 0; j < gr.count; j++) {
                    char real_dst[CMD_MAX_PATH];
                    libcmd_stat_t st;
                    const char *cdst = dst;

                    if (libcmd_stat(dst, &st, 1) == 0 && st.is_dir) {
                        libcmd_sprintf_s(real_dst, sizeof(real_dst),
                                         "%s/%s", dst,
                                         libcmd_path_basename(gr.paths[j]));
                        cdst = real_dst;
                    }

                    if (!opt_y && libcmd_access(cdst, 0) == 0) {
                        int c;
                        int ok;

                        printf(cmd_gettext(MSG_ASK_OVERWRITE), cdst);
                        fflush(stdout);
                        c = getchar();
                        ok = (c == 'y' || c == 'Y');
                        while (c != '\n' && c != EOF) c = getchar();
                        if (!ok) continue;
                    }

                    if (libcmd_rename(gr.paths[j], cdst) < 0) {
                        fprintf(stderr, cmd_gettext(MSG_ERR_MOVE),
                                gr.paths[j], cdst, libcmd_strerror());
                        ret = 1;
                    }
                }
                libcmd_glob_free(&gr);
            }
        } else {
            char real_dst[CMD_MAX_PATH];
            libcmd_stat_t st;
            const char *cdst = dst;

            if (libcmd_stat(dst, &st, 1) == 0 && st.is_dir) {
                libcmd_sprintf_s(real_dst, sizeof(real_dst),
                                 "%s/%s", dst, libcmd_path_basename(src));
                cdst = real_dst;
            }

            if (!opt_y && libcmd_access(cdst, 0) == 0) {
                int c;
                int ok;

                printf(cmd_gettext(MSG_ASK_OVERWRITE), cdst);
                fflush(stdout);
                c = getchar();
                ok = (c == 'y' || c == 'Y');
                while (c != '\n' && c != EOF) c = getchar();
                if (!ok) continue;
            }

            if (libcmd_rename(src, cdst) < 0) {
                fprintf(stderr, cmd_gettext(MSG_ERR_MOVE),
                        src, cdst, libcmd_strerror());
                ret = 1;
            }
        }
    }

    free(srcs);
    return ret;
}
