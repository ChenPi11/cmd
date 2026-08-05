/*
 * brd.c - RD / RMDIR builtin
 *
 * RMDIR [/S] [/Q] [drive:]path
 * RD    [/S] [/Q] [drive:]path
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Recursively delete a directory and its contents */
static int rmdir_recursive(const char *path)
{
    libcmd_dir_t d;
    libcmd_dirent_t entry;
    char child[CMD_MAX_PATH];
    int ret = 0;

    d = libcmd_opendir(path);
    if (d == NULL)
        return -1;

    /* We need to change to the directory to stat entries correctly */
    {
        char old_cwd[CMD_MAX_PATH];
        libcmd_getcwd(old_cwd, sizeof(old_cwd));
        libcmd_chdir(path);

        while (libcmd_readdir(d, &entry) == 0) {
            if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
                continue;

            libcmd_sprintf_s(child, sizeof(child), "%s/%s", path, entry.name);

            if (entry.is_dir) {
                if (rmdir_recursive(child) < 0)
                    ret = -1;
            } else {
                if (libcmd_unlink(child) < 0)
                    ret = -1;
            }
        }

        libcmd_chdir(old_cwd);
    }

    libcmd_closedir(d);

    if (libcmd_rmdir(path) < 0)
        ret = -1;

    return ret;
}

int builtin_rd(cmd_context_t *ctx, int argc, char **argv)
{
    int opt_s = 0; /* /S - recursive */
    int opt_q = 0; /* /Q - quiet     */
    int i;
    int ret = 0;

    (void)ctx;

    if (argc <= 1 || (argc == 2 && libcmd_strcasecmp(argv[1], "/?") == 0)) {
        fputs(cmd_gettext(MSG_HELP_RD),
              stdout);
        return argc <= 1 ? 1 : 0;
    }

    for (i = 1; i < argc; i++) {
        if (libcmd_strcasecmp(argv[i], "/S") == 0) { opt_s = 1; continue; }
        if (libcmd_strcasecmp(argv[i], "/Q") == 0) { opt_q = 1; continue; }
        if (argv[i][0] == '/' && libcmd_is_switch(argv[i], "SQ")) continue;

        {
            const char *path = argv[i];
            if (path[0] && path[1] == ':') path += 2;

            if (opt_s) {
                if (!opt_q) {
                    int c;
                    int confirmed;

                    printf(cmd_gettext(MSG_ASK_RD_CONFIRM), path);
                    fflush(stdout);
                    c = getchar();
                    confirmed = (c == 'y' || c == 'Y');
                    while (c != '\n' && c != EOF) c = getchar();
                    if (!confirmed) continue;
                }
                if (rmdir_recursive(path) < 0) {
                    fprintf(stderr, cmd_gettext(MSG_ERR_DIR_NOT_EMPTY), path);
                    ret = 1;
                }
            } else {
                if (libcmd_rmdir(path) < 0) {
                    fprintf(stderr, cmd_gettext(MSG_ERR_DIR_NOT_EMPTY), path);
                    ret = 1;
                }
            }
        }
    }

    return ret;
}
