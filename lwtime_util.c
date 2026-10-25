#define _CRT_SECURE_NO_WARNINGS
/*
 * lwtime_util.c - time and date utilities (Windows implementation)
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <windows.h>
#include <stdlib.h>
#include <time.h>

static void fill_libcmd_time(const SYSTEMTIME *st, libcmd_time_t *out)
{
    out->year   = st->wYear;
    out->month  = st->wMonth;
    out->day    = st->wDay;
    out->hour   = st->wHour;
    out->minute = st->wMinute;
    out->second = st->wSecond;
    out->ms     = st->wMilliseconds;
    out->wday   = st->wDayOfWeek;
}

void libcmd_get_local_time(libcmd_time_t *t)
{
    SYSTEMTIME st;

    GetLocalTime(&st);
    fill_libcmd_time(&st, t);
}

void libcmd_get_utc_time(libcmd_time_t *t)
{
    SYSTEMTIME st;

    GetSystemTime(&st);
    fill_libcmd_time(&st, t);
}

int libcmd_random(void)
{
    return rand() % 32768;
}

#endif
