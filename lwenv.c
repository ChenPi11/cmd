#define _CRT_SECURE_NO_WARNINGS
/*
 * lwenv.c - environment variable management (Windows implementation)
 *
 * Environment variable lookup is case-insensitive, matching cmd.exe
 * semantics.  Uses Windows API for environment variable operations.
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <windows.h>
#include <stdlib.h>
#include <string.h>

/* Callers may hold several getenv results at once (bstart keeps PATH
 * while find_exec consults PATHEXT, etc.).  A single static buffer
 * would silently alias them, so keep a small rotating pool instead:
 * the most recent LIBCMD_GETENV_BUFS lookups stay valid. */
#define LIBCMD_GETENV_BUFS 8
#define LIBCMD_GETENV_SIZE 32768

static char getenv_bufs[LIBCMD_GETENV_BUFS][LIBCMD_GETENV_SIZE];
static int getenv_buf_i;

const char *libcmd_getenv(const char *name)
{
    char *buf;
    DWORD len;

    if (name == NULL)
        return NULL;

    getenv_buf_i = (getenv_buf_i + 1) % LIBCMD_GETENV_BUFS;
    buf = getenv_bufs[getenv_buf_i];

    len = GetEnvironmentVariableA(name, buf, LIBCMD_GETENV_SIZE);
    if (len == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
        return NULL;
    if (len >= LIBCMD_GETENV_SIZE)
        return NULL;

    return buf;
}

int libcmd_setenv(const char *name, const char *value, int overwrite)
{
    const char *existing;

    if (name == NULL || value == NULL)
        return -1;

    if (!overwrite) {
        existing = libcmd_getenv(name);
        if (existing != NULL)
            return 0;
    }

    if (SetEnvironmentVariableA(name, value))
        return 0;
    return -1;
}

int libcmd_unsetenv(const char *name)
{
    if (name == NULL)
        return -1;

    if (SetEnvironmentVariableA(name, NULL))
        return 0;
    return -1;
}

int libcmd_putenv(char *string)
{
    char *eq;
    char name[4096];
    char *value;

    if (string == NULL)
        return -1;

    eq = strchr(string, '=');
    if (eq == NULL)
        return -1;

    /* Extract name */
    if ((size_t)(eq - string) >= sizeof(name))
        return -1;
    memcpy(name, string, (size_t)(eq - string));
    name[eq - string] = '\0';

    /* Value is after '=' */
    value = eq + 1;

    return libcmd_setenv(name, value, 1);
}

char **libcmd_get_environ(void)
{
    /* Windows doesn't have a direct environ pointer like Unix.
     * Return NULL to indicate not available. */
    return NULL;
}

#endif
