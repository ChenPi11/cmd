#define _CRT_SECURE_NO_WARNINGS
/*
 * lwselfpath.c - locate the running executable's own path (Windows implementation)
 *
 * On Windows, uses GetModuleFileNameA to get the full path of the
 * running executable.
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Buffer size used for all self-path strings. */
#define SELF_PATH_BUF_SIZE 4096

/* argv[0] and the working directory at program start. */
static char *self_argv0;
static char  self_cwd[SELF_PATH_BUF_SIZE];

void libcmd_init_self(const char *argv0)
{
    free(self_argv0);
    self_argv0 = argv0 ? libcmd_strdup(argv0) : NULL;

    self_cwd[0] = '\0';
    if (libcmd_getcwd(self_cwd, sizeof(self_cwd)) == NULL)
        self_cwd[0] = '\0';
}

const char *libcmd_get_self_path(void)
{
    static char buf[SELF_PATH_BUF_SIZE];
    DWORD len;

    /* Try GetModuleFileName first */
    len = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
    if (len > 0 && len < sizeof(buf))
        return buf;

    /* Fallback to argv[0] */
    if (self_argv0 != NULL && self_argv0[0] != '\0') {
        /* If it's an absolute path, use it directly */
        if (self_argv0[0] != '\0' && self_argv0[1] == ':') {
            return self_argv0;
        }

        /* If it's a relative path, resolve against current directory */
        if (libcmd_path_join(self_cwd, self_argv0, buf, sizeof(buf)) == 0) {
            return buf;
        }
    }

    return NULL;
}

#endif
