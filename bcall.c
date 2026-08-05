/*
 * bcall.c - CALL builtin
 *
 * CALL [drive:][path]filename [batch-parameters]
 * CALL :label [arguments]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration from cinterp.c */
extern int cmd_run_file(cmd_context_t *ctx, const char *path,
                        int argc, char **argv);
extern int cmd_run_file_sub(cmd_context_t *ctx, FILE *fp,
                             const char *path, int argc, char **argv);
extern int cmd_run_line(cmd_context_t *ctx, const char *line);

int builtin_call(cmd_context_t *ctx, int argc, char **argv)
{
    if (argc < 2 || (argc == 2 && libcmd_strcasecmp(argv[1], "/?") == 0)) {
        fputs(cmd_gettext(MSG_HELP_CALL), stdout);
        return argc < 2 ? 0 : 0;
    }

    /* CALL :label - internal label call within the same batch file */
    if (argv[1][0] == ':') {
        cmd_call_frame_t *parent_frame;
        FILE *f;
        long return_pos;
        const char *label = argv[1];
        char search[256];
        char line_buf[CMD_MAX_LINE];
        int found = 0;

        if (ctx->call_depth == 0) {
            fprintf(stderr, "%s", cmd_gettext(MSG_ERR_CALL_LABEL_OUTSIDE));
            return 1;
        }

        parent_frame = ctx->call_stack[ctx->call_depth - 1];
        f = parent_frame->file;
        return_pos = ftell(f);   /* we will return here after the sub-call */

        /* Build search pattern */
        if (label[0] == ':')
            libcmd_sprintf_s(search, sizeof(search), "%s", label);
        else
            libcmd_sprintf_s(search, sizeof(search), ":%s", label);

        /* Scan from beginning for the label */
        rewind(f);
        while (fgets(line_buf, sizeof(line_buf), f) != NULL) {
            const char *p = line_buf;
            char found_label[256];
            size_t llen = 0;
            const char *q;

            while (*p == ' ' || *p == '\t') p++;
            if (*p != ':') continue;

            q = p;
            while (*q && *q != ' ' && *q != '\t' && *q != '\n' &&
                   *q != '\r' && llen < 255)
                found_label[llen++] = *q++;
            found_label[llen] = '\0';

            if (libcmd_strcasecmp(found_label, search) == 0) {
                found = 1;
                break;
            }
        }

        if (!found) {
            /* Restore position and report error */
            fseek(f, return_pos, SEEK_SET);
            fprintf(stderr, cmd_gettext(MSG_ERR_LABEL_NOT_FOUND),
                    label);
            return 1;
        }

        /* Execute the sub-routine (file is positioned just after the label) */
        {
            int ret = cmd_run_file_sub(ctx, f, parent_frame->path,
                                       argc - 1, argv + 1);
            /* Restore parent file position to after the CALL line */
            fseek(f, return_pos, SEEK_SET);
            return ret;
        }
    }

    /* CALL script.bat [args] */
    return cmd_run_file(ctx, argv[1], argc - 1, argv + 1);
}
