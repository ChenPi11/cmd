/*
 * bcopy.c - COPY builtin
 *
 * COPY [/D] [/V] [/N] [/Y | /-Y] [/Z] [/L] [/A | /B] source [destination]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int builtin_copy(cmd_context_t *ctx, int argc, char **argv)
{
    int opt_y      = 1;  /* /Y default: don't prompt (overwrite silently) */
    int i;
    const char *src  = NULL;
    const char *dst  = NULL;
    char real_dst[CMD_MAX_PATH];
    int ret = 0;

    (void)ctx;

    if (argc <= 1 || (argc == 2 && libcmd_strcasecmp(argv[1], "/?") == 0)) {
        fputs(cmd_gettext(MSG_HELP_COPY), stdout);
        return argc <= 1 ? 1 : 0;
    }

    for (i = 1; i < argc; i++) {
        if (libcmd_is_switch(argv[i], "YDNVZLBA")) {
            if (libcmd_strcasecmp(argv[i], "/Y")  == 0) { opt_y  = 1; continue; }
            if (libcmd_strcasecmp(argv[i], "/-Y") == 0) { opt_y  = 0; continue; }
            /* Other flags (/D /V /N /Z /L /A /B) silently ignored */
            continue;
        }
        if (src == NULL)       src = argv[i];
        else if (dst == NULL)  dst = argv[i];
    }

    if (src == NULL) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_COPY_MISSING_SOURCE));
        return 1;
    }

    if (dst == NULL) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_COPY_MISSING_DEST));
        return 1;
    }

    /* Determine if dst is a directory; if so, append the source filename */
    {
        libcmd_stat_t st;

        if (libcmd_stat(dst, &st, 1) == 0 && st.is_dir) {
            libcmd_sprintf_s(real_dst, sizeof(real_dst),
                             "%s/%s", dst, libcmd_path_basename(src));
            dst = real_dst;
        }
    }

    /* Expand wildcards in src */
    if (strchr(src, '*') != NULL || strchr(src, '?') != NULL) {
        libcmd_glob_result_t gr;
        if (libcmd_glob(src, &gr) == 0) {
            size_t j;
            for (j = 0; j < gr.count; j++) {
                libcmd_stat_t st;
                char real_dst2[CMD_MAX_PATH];
                const char *cdst = dst;

                if (libcmd_stat(dst, &st, 1) == 0 && st.is_dir) {
                    libcmd_sprintf_s(real_dst2, sizeof(real_dst2),
                                     "%s/%s", dst,
                                     libcmd_path_basename(gr.paths[j]));
                    cdst = real_dst2;
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

                if (libcmd_copy_file(gr.paths[j], cdst, 1) < 0) {
                    fprintf(stderr, cmd_gettext(MSG_ERR_COPY),
                            gr.paths[j], cdst);
                    ret = 1;
                }
            }
            libcmd_glob_free(&gr);
        }
    } else {
        if (!opt_y && libcmd_access(dst, 0) == 0) {
            int c;
            int ok;

            printf(cmd_gettext(MSG_ASK_OVERWRITE), dst);
            fflush(stdout);
            c = getchar();
            ok = (c == 'y' || c == 'Y');
            while (c != '\n' && c != EOF) c = getchar();
            if (!ok) return 0;
        }
        if (libcmd_copy_file(src, dst, 1) < 0) {
            fprintf(stderr, cmd_gettext(MSG_ERR_COPY_DETAIL),
                    src, dst, libcmd_strerror());
            ret = 1;
        } else {
            printf("%s", cmd_gettext(MSG_INFO_COPY_DONE));
        }
    }

    return ret;
}
