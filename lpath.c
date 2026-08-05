/*
 * lpath.c - path manipulation utilities
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    if (blen > 0 && base[blen - 1] == '/') {
        return libcmd_sprintf_s(buf, size, "%s%s", base, rel) >= 0 ? 0 : -1;
    } else {
        return libcmd_sprintf_s(buf, size, "%s/%s", base, rel) >= 0 ? 0 : -1;
    }
}

int libcmd_path_dirname(const char *path, char *buf, size_t size)
{
    const char *last;
    size_t len;

    if (path == NULL || buf == NULL || size == 0)
        return -1;

    last = strrchr(path, '/');
    if (last == NULL) {
        return libcmd_sprintf_s(buf, size, ".") >= 0 ? 0 : -1;
    }

    len = (size_t)(last - path);
    if (len == 0) {
        return libcmd_sprintf_s(buf, size, "/") >= 0 ? 0 : -1;
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

    if (path == NULL)
        return "";

    p = strrchr(path, '/');
    return p ? p + 1 : path;
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
    if (libcmd_path_is_abs(path)) {
        if (strlen(path) + 1 > size)
            return -1;
        memcpy(buf, path, strlen(path) + 1);
    } else {
        char cwd[4096];
        if (libcmd_getcwd(cwd, sizeof(cwd)) == NULL)
            return -1;
        if (libcmd_path_join(cwd, path, buf, size) < 0)
            return -1;
    }
    libcmd_path_normalize(buf);
    return 0;
}

int libcmd_path_is_abs(const char *path)
{
    if (path == NULL)
        return 0;
    return path[0] == '/';
}

/*
 * Convert Windows-style backslash separators to forward slashes in place.
 * \usr\bin -> /usr/bin
 */
void libcmd_path_norm_sep(char *s)
{
    if (s == NULL)
        return;
    for (; *s; s++) {
        if (*s == '\\')
            *s = '/';
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
    char *parts[512];
    int n = 0;
    int i;
    int is_abs;

    if (path == NULL)
        return path;

    is_abs = (path[0] == '/');
    src = path;
    dst = path;

    /* Tokenize on '/' */
    while (*src) {
        if (*src == '/') {
            src++;
            continue;
        }
        parts[n++] = src;
        while (*src && *src != '/')
            src++;
        if (*src == '/')
            *src++ = '\0';
        /* else end of string */
    }

    /* Process each component */
    {
        char *stack[512];
        int top = 0;

        for (i = 0; i < n; i++) {
            if (strcmp(parts[i], ".") == 0) {
                continue;
            } else if (strcmp(parts[i], "..") == 0) {
                if (top > 0)
                    top--;
            } else {
                stack[top++] = parts[i];
            }
        }

        /* Reconstruct */
        if (is_abs)
            *dst++ = '/';

        for (i = 0; i < top; i++) {
            if (i > 0)
                *dst++ = '/';
            {
                const char *p = stack[i];
                while (*p)
                    *dst++ = *p++;
            }
        }

        if (dst == path || (dst == path + 1 && is_abs))
            *dst++ = (is_abs ? '\0' : '.');

        *dst = '\0';
    }

    return path;
}
