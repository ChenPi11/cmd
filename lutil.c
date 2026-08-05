/*
 * lutil.c - string utility functions
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int libcmd_strcasecmp(const char *a, const char *b)
{
    unsigned char ca, cb;

    do {
        ca = (unsigned char)tolower((unsigned char)*a++);
        cb = (unsigned char)tolower((unsigned char)*b++);
    } while (ca && ca == cb);

    return (int)ca - (int)cb;
}

int libcmd_strncasecmp(const char *a, const char *b, size_t n)
{
    unsigned char ca, cb;

    if (n == 0)
        return 0;

    do {
        ca = (unsigned char)tolower((unsigned char)*a++);
        cb = (unsigned char)tolower((unsigned char)*b++);
        if (--n == 0)
            break;
    } while (ca && ca == cb);

    return (int)ca - (int)cb;
}

char *libcmd_strdup(const char *s)
{
    size_t len;
    char *copy;

    if (s == NULL)
        return NULL;

    len  = strlen(s);
    copy = (char *)malloc(len + 1);
    if (copy == NULL)
        return NULL;

    memcpy(copy, s, len + 1);
    return copy;
}

char *libcmd_strndup(const char *s, size_t n)
{
    size_t len;
    char *copy;

    if (s == NULL)
        return NULL;

    len = strlen(s);
    if (len > n)
        len = n;

    copy = (char *)malloc(len + 1);
    if (copy == NULL)
        return NULL;

    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

char *libcmd_strtrim(char *s)
{
    char *end;

    if (s == NULL)
        return NULL;

    /* Trim leading whitespace */
    while (*s && isspace((unsigned char)*s))
        s++;

    /* Trim trailing whitespace */
    if (*s) {
        end = s + strlen(s) - 1;
        while (end > s && isspace((unsigned char)*end))
            end--;
        end[1] = '\0';
    }

    return s;
}

const char *libcmd_strerror(void)
{
    return strerror(errno);
}
