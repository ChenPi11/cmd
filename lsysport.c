/*
 * lsysport.c - system portability shims
 *
 * Old System V targets (compiled with -DLIBCMD_SYSV) lack several
 * functions that recent POSIX / GNU libcs provide.  These wrappers
 * keep the rest of the project free of #ifdef noise while preserving
 * the same behaviour on modern systems (where the native functions are
 * used unchanged):
 *
 *   libcmd_fnmatch            fnmatch(3)                  vs  built-in matcher
 *   libcmd_set_system_time    settimeofday(2)             vs  stime(2)
 *   libcmd_set_process_priority setpriority(2)           vs  nice(2)
 *   libcmd_open_memstream     open_memstream(3) (GNU)     vs  tmpfile-backed
 *   libcmd_memstream_close
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef LIBCMD_SYSV
#include <fnmatch.h>
#include <sys/resource.h>
#include <sys/time.h>
#endif

/* -------------------------------------------------------------------------
 * fnmatch
 * ---------------------------------------------------------------------- */

#if defined(LIBCMD_SYSV)

#define LIBCMD_FNM_NOMATCH 1

/*
 * Match the pattern against the string using FNM_NOESCAPE semantics
 * (backslash is an ordinary character).  Supports '*', '?', and '[...]'
 * character classes with '!' (or '^') negation and a-z ranges.  A
 * trailing '*' matches everything; an unterminated '[' is treated as
 * a literal '[' (like glibc).  Returns 1 on match, 0 on no match.
 */
static int fnmatch_sub(const char *pattern, const char *string)
{
    const char *p = pattern;
    const char *s = string;

    while (*p) {
        if (*p == '*') {
            while (p[1] == '*')
                p++;
            if (p[1] == '\0')
                return 1;
            for (;; s++) {
                if (fnmatch_sub(p + 1, s) != 0)
                    return 1;
                if (*s == '\0')
                    return 0;
            }
        } else if (*p == '?') {
            if (*s == '\0')
                return 0;
            s++;
            p++;
        } else if (*p == '[') {
            unsigned char c = (unsigned char)*s;
            const char *q = p + 1;
            int negate = 0;
            int matched = 0;

            if (*q == '!' || *q == '^') {
                negate = 1;
                q++;
            }

            /* Lookahead: does the bracket expression ever close?  If not,
             * '[' matches itself.  A ']' as the very first character of
             * the class is literal. */
            {
                const char *r = q;
                int first = 1;
                int closed = 0;

                while (*r) {
                    if (*r == ']' && !first) {
                        closed = 1;
                        break;
                    }
                    first = 0;
                    r++;
                }
                if (!closed) {
                    if (*s != '[')
                        return 0;
                    s++;
                    p++;
                    continue;
                }
            }

            if (*s == '\0')
                return 0;

            if (*q == ']') {
                if (c == ']')
                    matched = 1;
                q++;
            }
            while (*q && *q != ']') {
                unsigned char lo = (unsigned char)q[0];

                if (q[1] == '-' && q[2] != '\0' && q[2] != ']') {
                    unsigned char hi = (unsigned char)q[2];

                    if (c >= lo && c <= hi)
                        matched = 1;
                    q += 3;
                } else {
                    if (c == lo)
                        matched = 1;
                    q++;
                }
            }

            if (negate)
                matched = (matched ? 0 : 1);
            if (!matched)
                return 0;
            s++;
            while (*q && *q != ']')
                q++;
            if (*q == ']')
                q++;
            p = q;
        } else {
            if (*s != *p || *s == '\0')
                return 0;
            s++;
            p++;
        }
    }
    return (*s == '\0');
}

int libcmd_fnmatch(const char *pattern, const char *string)
{
    if (fnmatch_sub(pattern, string) != 0)
        return 0;
    return LIBCMD_FNM_NOMATCH;
}

#else

int libcmd_fnmatch(const char *pattern, const char *string)
{
    return fnmatch(pattern, string, FNM_NOESCAPE);
}

#endif

/* -------------------------------------------------------------------------
 * System clock
 * ---------------------------------------------------------------------- */

/* System V provides stime(2); recent glibc (the host used for
 * LIBCMD_SYSV build-checks) dropped its declaration, so declare it
 * explicitly when not otherwise available. */
#if defined(LIBCMD_SYSV) && !defined(__stime_declared)
#define __stime_declared 1
int stime(const time_t *);
#endif

int libcmd_set_system_time(time_t t)
{
#if defined(LIBCMD_SYSV)
    return stime(&t);
#else
    struct timeval tv;

    tv.tv_sec  = t;
    tv.tv_usec = 0;
    return settimeofday(&tv, NULL);
#endif
}

/* -------------------------------------------------------------------------
 * Process priority
 * ---------------------------------------------------------------------- */

int libcmd_set_process_priority(int nice_level)
{
    if (nice_level == 0)
        return 0;
#if defined(LIBCMD_SYSV)
    errno = 0;
    if (nice(nice_level) == -1 && errno != 0)
        return -1;
    return 0;
#else
    return setpriority(PRIO_PROCESS, 0, nice_level);
#endif
}

/* -------------------------------------------------------------------------
 * Memory streams
 * ---------------------------------------------------------------------- */

#if defined(LIBCMD_SYSV)

static FILE *ms_stream;     /* active memstream (single-stream use) */
static char **ms_ptr;
static size_t *ms_sizeloc;

FILE *libcmd_open_memstream(char **ptr, size_t *sizeloc)
{
    FILE *f = tmpfile();

    if (f != NULL) {
        ms_stream = f;
        ms_ptr    = ptr;
        ms_sizeloc = sizeloc;
    }
    if (ptr)
        *ptr = NULL;
    if (sizeloc)
        *sizeloc = 0;
    return f;
}

int libcmd_memstream_close(FILE *stream)
{
    long sz;
    char *buf;

    if (stream == NULL)
        return -1;

    if (fflush(stream) != 0) {
        fclose(stream);
        return -1;
    }
    sz = ftell(stream);
    if (sz < 0) {
        fclose(stream);
        return -1;
    }
    rewind(stream);

    buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(stream);
        return -1;
    }
    if (sz > 0 && fread(buf, 1, (size_t)sz, stream) != (size_t)sz)
        buf[0] = '\0';                 /* read error: empty buffer */
    buf[sz] = '\0';

    if (ms_ptr)
        *ms_ptr = buf;
    if (ms_sizeloc)
        *ms_sizeloc = (size_t)sz;

    (void)ms_stream;
    fclose(stream);
    return 0;
}

#else

FILE *libcmd_open_memstream(char **ptr, size_t *sizeloc)
{
    if (ptr)
        *ptr = NULL;
    if (sizeloc)
        *sizeloc = 0;
    return open_memstream(ptr, sizeloc);
}

int libcmd_memstream_close(FILE *stream)
{
    if (stream == NULL)
        return -1;
    return fclose(stream);
}

#endif
