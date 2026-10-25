#define _CRT_SECURE_NO_WARNINGS
/*
 * lwsprintf_s.c - safe sprintf implementation (Windows implementation)
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int libcmd_vsprintf_s(char *buf, size_t size, const char *fmt, va_list ap)
{
    int n;

    if (buf == NULL || size == 0 || fmt == NULL)
        return -1;

    n = vsnprintf(buf, size, fmt, ap);

    /* Terminate the string at the right place */
    if (n < 0)
        return -1;
    if ((size_t)n < size - 1)
        buf[n] = '\0';
    else
        buf[size - 1] = '\0';

    /* Return how many characters were actually written (clamped to size-1) */
    return (n < (int)(size - 1)) ? n : (int)(size - 1);
}

int libcmd_sprintf_s(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = libcmd_vsprintf_s(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

#endif
