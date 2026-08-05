/*
 * bgoto.c - GOTO builtin
 *
 * GOTO label
 * GOTO :EOF
 *
 * Searches for a label in the current batch file and continues execution
 * from that point.
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

/* Special exit-code label */
#define GOTO_EOF_LABEL ":EOF"

int builtin_goto(cmd_context_t *ctx, int argc, char **argv)
{
    cmd_call_frame_t *frame;
    const char *label;
    char search[256];
    char line[CMD_MAX_LINE];
    long start_pos;
    int found = 0;

    if (argc < 2 || libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_GOTO), stdout);
        return argc < 2 ? 1 : 0;
    }

    if (ctx->call_depth == 0) {
        /* Not in a batch file; GOTO has no effect */
        return 0;
    }

    frame = ctx->call_stack[ctx->call_depth - 1];
    label = argv[1];

    /* :EOF exits the current batch file */
    if (libcmd_strcasecmp(label, ":EOF") == 0 ||
        libcmd_strcasecmp(label, "EOF")  == 0) {
        ctx->stop_batch = 1;
        return 0;
    }

    /* Build the label to search for (with or without leading :) */
    if (label[0] == ':')
        libcmd_sprintf_s(search, sizeof(search), "%s", label);
    else
        libcmd_sprintf_s(search, sizeof(search), ":%s", label);

    /* Rewind the batch file and search for the label */
    start_pos = ftell(frame->file);
    rewind(frame->file);

    while (fgets(line, sizeof(line), frame->file) != NULL) {
        const char *p = line;
        size_t len;

        /* Strip leading whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (*p != ':') continue;

        /* Strip trailing whitespace/newline */
        len = strlen(p);
        while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r' ||
                            p[len-1] == ' '  || p[len-1] == '\t'))
            len--;

        /* Compare label (case-insensitive, stop at space) */
        {
            char found_label[256];
            size_t llen = 0;
            const char *q = p;
            while (*q && !(*q == ' ' || *q == '\t' ||
                           *q == '\n' || *q == '\r') &&
                   llen < 255) {
                found_label[llen++] = *q++;
            }
            found_label[llen] = '\0';

            if (libcmd_strcasecmp(found_label, search) == 0) {
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        /* Seek back to where we were; label not found exits the batch file */
        fseek(frame->file, start_pos, SEEK_SET);
        fprintf(stderr, cmd_gettext(MSG_ERR_LABEL_NOT_FOUND),
                label);
        ctx->stop_batch = 1;
        return 1;
    }

    /* Execution continues from the current file position (after the label) */
    return 0;
}
