/*
 * lselfpath.c - locate the running executable's own path
 *
 * Locating the current executable is platform-dependent.  The following
 * strategies are tried in order:
 *
 *   1. /proc/self/exe -- if this node exists the system provides
 *      procfs (Linux, Android, *BSD with linprocfs); readlink(2) yields
 *      the real path of the running binary.
 *   2. FreeBSD -- sysctl(KERN_PROC_PATHNAME).
 *   3. Darwin -- _NSGetExecutablePath().
 *   4. argv[0] -- recorded together with the initial working directory
 *      by libcmd_init_self().  First checked as a path relative to that
 *      directory (covers the "./cmd.exe" invocation), then searched in
 *      PATH.
 *
 * If every strategy fails, libcmd_get_self_path() returns NULL and the
 * caller decides how to proceed.
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

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

/* Return 1 if 'path' is a regular executable file. */
static int is_executable_file(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;
    return S_ISREG(st.st_mode) &&
           (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH));
}

/*
 * Fall back to argv[0].  'buf' receives the resolved path and returns
 * 0 on success, -1 if nothing usable is found.
 */
static int self_from_argv0(char *buf, size_t size)
{
    char tmp[SELF_PATH_BUF_SIZE];
    int r;

    if (self_argv0 == NULL || self_argv0[0] == '\0')
        return -1;

    /* Absolute path: use it directly. */
    if (self_argv0[0] == '/') {
        if (!is_executable_file(self_argv0))
            return -1;
        return libcmd_sprintf_s(buf, size, "%s", self_argv0) < 0 ? -1 : 0;
    }

    /* Relative path: resolve against the initial working directory.
     * This covers the "./cmd.exe" invocation. */
    if (self_cwd[0]) {
        r = libcmd_sprintf_s(tmp, sizeof(tmp), "%s/%s",
                             self_cwd, self_argv0);
        if (r < 0)
            return -1;
        if (is_executable_file(tmp))
            return libcmd_sprintf_s(buf, size, "%s", tmp) < 0 ? -1 : 0;
    }

    /* Bare name not found locally: search PATH. */
    return libcmd_find_exec(self_argv0, libcmd_getenv("PATH"),
                            buf, size);
}

const char *libcmd_get_self_path(void)
{
    static char buf[SELF_PATH_BUF_SIZE];

    /* 1. procfs */
    if (access("/proc/self/exe", X_OK) == 0) {
        int n = (int)readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            return buf;
        }
    }

    /* 2. FreeBSD: sysctl(KERN_PROC_PATHNAME) */
#if defined(__FreeBSD__)
    {
        int mib[4];
        size_t len;

        mib[0] = CTL_KERN;
        mib[1] = KERN_PROC;
        mib[2] = KERN_PROC_PATHNAME;
        mib[3] = -1;
        len = sizeof(buf);
        if (sysctl(mib, 4, buf, &len, NULL, 0) == 0 && len > 1)
            return buf;
    }
#endif

    /* 3. Darwin: _NSGetExecutablePath */
#if defined(__APPLE__)
    {
        uint32_t len = (uint32_t)sizeof(buf);
        if (_NSGetExecutablePath(buf, &len) == 0)
            return buf;
    }
#endif

    /* 4. argv[0] fallback */
    if (self_from_argv0(buf, sizeof(buf)) == 0)
        return buf;

    return NULL;
}
