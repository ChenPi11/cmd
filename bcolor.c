/*
 * bcolor.c - COLOR builtin
 *
 * COLOR [attr]
 *
 * Sets the console foreground and background colors.
 * attr is a two-digit hex value: first digit = background, second = foreground.
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

static int parse_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int builtin_color(cmd_context_t *ctx, int argc, char **argv)
{
    (void)ctx;

    if (argc <= 1) {
        /* Reset to defaults */
        libcmd_reset_color();
        return 0;
    }

    if (libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_COLOR), stdout);
        return 0;
    }

    {
        const char *attr = argv[1];
        int bg = -1, fg = -1;
        size_t len = strlen(attr);

        if (len == 1) {
            fg = parse_hex_nibble(attr[0]);
        } else if (len >= 2) {
            bg = parse_hex_nibble(attr[0]);
            fg = parse_hex_nibble(attr[1]);
        }

        if (fg < 0) {
            fprintf(stderr, "%s", cmd_gettext(MSG_ERR_INVALID_COLOR));
            return 1;
        }

        /* Same foreground and background is an error */
        if (bg >= 0 && bg == fg) {
            return 1;
        }

        libcmd_set_color(fg, bg);
    }

    fflush(stdout);
    return 0;
}
