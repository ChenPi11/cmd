/*
 * bmklink.c - MKLINK builtin
 *
 * MKLINK [[/D] | [/H] | [/J]] LinkName Target
 *
 * /D  Create a directory symbolic link.
 * /H  Create a hard link instead of a symbolic link.
 * /J  Create a directory junction (treated as symlink on Unix).
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

int builtin_mklink(cmd_context_t *ctx, int argc, char **argv)
{
    int opt_dir      = 0;
    int opt_hard     = 0;
    int opt_junction = 0;
    int i;
    const char *link_name = NULL;
    const char *target    = NULL;

    (void)ctx;

    if (argc < 3 || (argc == 2 && libcmd_strcasecmp(argv[1], "/?") == 0)) {
        fputs(cmd_gettext(MSG_HELP_MKLINK), stdout);
        return argc < 3 ? 1 : 0;
    }

    for (i = 1; i < argc; i++) {
        if (libcmd_strcasecmp(argv[i], "/D") == 0) { opt_dir = 1; continue; }
        if (libcmd_strcasecmp(argv[i], "/H") == 0) { opt_hard = 1; continue; }
        if (libcmd_strcasecmp(argv[i], "/J") == 0) { opt_junction = 1; continue; }
        if (argv[i][0] == '/' && libcmd_is_switch(argv[i], "HDJ")) continue;

        if (link_name == NULL) link_name = argv[i];
        else if (target == NULL) target  = argv[i];
    }

    if (link_name == NULL || target == NULL) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_MKLINK_MISSING));
        return 1;
    }

    /* A junction always points at a directory, and so does /D */
    if (opt_junction || opt_dir) {
        libcmd_stat_t st;
        if (libcmd_stat(target, &st, 1) < 0 || !st.is_dir) {
            fprintf(stderr,
                    cmd_gettext(MSG_ERR_MKLINK_NOT_DIR),
                    opt_junction ? "a junction" : "a directory symbolic link", target);
            return 1;
        }
    }

    if (opt_hard) {
        if (libcmd_link(target, link_name) < 0) {
            fprintf(stderr, cmd_gettext(MSG_ERR_MKLINK_HARD),
                    link_name, libcmd_strerror());
            return 1;
        }
        printf(cmd_gettext(MSG_OK_HARDLINK), link_name, target);
    } else if (opt_junction) {
        /* No junction concept on Unix; a directory symlink is equivalent */
        if (libcmd_symlink(target, link_name) < 0) {
            fprintf(stderr, cmd_gettext(MSG_ERR_MKLINK_JUNCTION),
                    link_name, libcmd_strerror());
            return 1;
        }
        printf(cmd_gettext(MSG_OK_JUNCTION), link_name, target);
    } else {
        if (libcmd_symlink(target, link_name) < 0) {
            fprintf(stderr, cmd_gettext(MSG_ERR_MKLINK_SYMLINK),
                    link_name, libcmd_strerror());
            return 1;
        }
        printf(cmd_gettext(MSG_OK_SYMLINK),
               link_name, target);
    }

    return 0;
}
