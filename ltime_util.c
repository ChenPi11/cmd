/*
 * ltime_util.c - time and date utilities
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <stdlib.h>
#include <time.h>

static void fill_libcmd_time(const struct tm *t, libcmd_time_t *out)
{
    out->year   = t->tm_year + 1900;
    out->month  = t->tm_mon  + 1;
    out->day    = t->tm_mday;
    out->hour   = t->tm_hour;
    out->minute = t->tm_min;
    out->second = t->tm_sec;
    out->ms     = 0;
    out->wday   = t->tm_wday;
}

void libcmd_get_local_time(libcmd_time_t *t)
{
    time_t now;
    struct tm *tm_info;

    time(&now);
    tm_info = localtime(&now);
    if (tm_info)
        fill_libcmd_time(tm_info, t);
}

void libcmd_get_utc_time(libcmd_time_t *t)
{
    time_t now;
    struct tm *tm_info;

    time(&now);
    tm_info = gmtime(&now);
    if (tm_info)
        fill_libcmd_time(tm_info, t);
}

int libcmd_random(void)
{
    return rand() % 32768;
}
