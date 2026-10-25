#define _CRT_SECURE_NO_WARNINGS
/*
 * lwpath.c - path manipulation utilities (Windows implementation)
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <windows.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int libcmd_path_join(const char *base, const char *rel,
                     char *buf, size_t size)
{
    size_t blen;

    if (buf == NULL || size == 0)
        return -1;

    if (libcmd_path_is_abs(rel)) {
        return libcmd_sprintf_s(buf, size, "%s", rel) >= 0 ? 0 : -1;
    }

    blen = strlen(base);
    if (blen > 0 && (base[blen - 1] == '\\' || base[blen - 1] == '/')) {
        return libcmd_sprintf_s(buf, size, "%s%s", base, rel) >= 0 ? 0 : -1;
    } else {
        return libcmd_sprintf_s(buf, size, "%s\\%s", base, rel) >= 0 ? 0 : -1;
    }
}

int libcmd_path_dirname(const char *path, char *buf, size_t size)
{
    const char *last_slash;
    const char *last_backslash;
    const char *last;
    size_t len;

    if (path == NULL || buf == NULL || size == 0)
        return -1;

    last_slash = strrchr(path, '/');
    last_backslash = strrchr(path, '\\');

    /* Use the last separator found */
    if (last_slash == NULL && last_backslash == NULL) {
        return libcmd_sprintf_s(buf, size, ".") >= 0 ? 0 : -1;
    }

    if (last_slash == NULL)
        last = last_backslash;
    else if (last_backslash == NULL)
        last = last_slash;
    else
        last = (last_slash > last_backslash) ? last_slash : last_backslash;

    len = (size_t)(last - path);
    if (len == 0) {
        /* Root directory */
        if (path[0] == '\\' || path[0] == '/') {
            return libcmd_sprintf_s(buf, size, "%c", path[0]) >= 0 ? 0 : -1;
        }
        return libcmd_sprintf_s(buf, size, ".") >= 0 ? 0 : -1;
    }

    /* Handle drive letter (e.g., "C:\") */
    if (len == 2 && path[1] == ':') {
        return libcmd_sprintf_s(buf, size, "%c:\\", path[0]) >= 0 ? 0 : -1;
    }

    if (len >= size)
        return -1;

    memcpy(buf, path, len);
    buf[len] = '\0';
    return 0;
}

const char *libcmd_path_basename(const char *path)
{
    const char *p;
    const char *last_slash;
    const char *last_backslash;

    if (path == NULL)
        return "";

    last_slash = strrchr(path, '/');
    last_backslash = strrchr(path, '\\');

    if (last_slash == NULL && last_backslash == NULL)
        return path;

    if (last_slash == NULL)
        p = last_backslash;
    else if (last_backslash == NULL)
        p = last_slash;
    else
        p = (last_slash > last_backslash) ? last_slash : last_backslash;

    return p + 1;
}

const char *libcmd_path_ext(const char *path)
{
    const char *base;
    const char *dot;

    if (path == NULL)
        return "";

    base = libcmd_path_basename(path);
    dot  = strrchr(base, '.');

    /* Hidden files like ".bashrc" - the leading dot is not an extension */
    if (dot == base)
        return base + strlen(base);

    return dot ? dot : base + strlen(base);
}

int libcmd_path_abs(const char *path, char *buf, size_t size)
{
    char full_path[MAX_PATH];

    if (path == NULL || buf == NULL || size == 0)
        return -1;

    if (GetFullPathNameA(path, (DWORD)size, full_path, NULL) == 0)
        return -1;

    return libcmd_sprintf_s(buf, size, "%s", full_path) >= 0 ? 0 : -1;
}

int libcmd_path_is_abs(const char *path)
{
    if (path == NULL)
        return 0;

    /* Check for drive letter (e.g., "C:\") */
    if (strlen(path) >= 3 &&
        isalpha((unsigned char)path[0]) &&
        path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/'))
        return 1;

    /* Check for UNC path (e.g., "\\server\share") */
    if (path[0] == '\\' && path[1] == '\\')
        return 1;

    return 0;
}

/*
 * Convert forward slash separators to backslash in place.
 * /usr/bin -> \usr\bin
 */
void libcmd_path_norm_sep(char *s)
{
    if (s == NULL)
        return;
    for (; *s; s++) {
        if (*s == '/')
            *s = '\\';
    }
}

/*
 * Return 1 if arg looks like a command switch ('/X') whose option letter is
 * listed in known (case-insensitive).  '/-' (e.g. /-Y) and '/?' (help) are
 * always treated as switches.  A bare '/' or an unknown letter is not a
 * switch, so Unix absolute paths like /usr/bin can be used as arguments.
 */
int libcmd_is_switch(const char *arg, const char *known)
{
    char c;

    if (arg == NULL || arg[0] != '/' || arg[1] == '\0')
        return 0;

    c = (char)toupper((unsigned char)arg[1]);
    if (c == '-' || c == '?')
        return 1;
    if (known != NULL && strchr(known, c) != NULL)
        return 1;
    return 0;
}

char *libcmd_path_normalize(char *path)
{
    char *src, *dst;

    if (path == NULL)
        return path;

    src = path;
    dst = path;

    while (*src) {
        /* Convert forward slashes to backslashes */
        if (*src == '/') {
            *dst++ = '\\';
            src++;
        }
        /* Handle double backslashes (keep single) */
        else if (*src == '\\' && src[1] == '\\') {
            *dst++ = '\\';
            src += 2;
        }
        /* Handle . and .. */
        else if (*src == '.' && (src[1] == '\\' || src[1] == '/' || src[1] == '\0')) {
            src++;
            if (*src == '\\' || *src == '/')
                src++;
        }
        else if (*src == '.' && src[1] == '.' && (src[2] == '\\' || src[2] == '/' || src[2] == '\0')) {
            /* Go up one directory */
            if (dst > path) {
                dst--;
                while (dst > path && *(dst-1) != '\\' && *(dst-1) != '/')
                    dst--;
            }
            src += 2;
            if (*src == '\\' || *src == '/')
                src++;
        }
        else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';

    /* Remove trailing backslash unless it's a root */
    if (dst > path + 1 && *(dst-1) == '\\') {
        if (!(dst - path == 3 && path[1] == ':') &&
            !(dst - path == 2 && path[0] == '\\')) {
            *(dst-1) = '\0';
        }
    }

    return path;
}

#endif
