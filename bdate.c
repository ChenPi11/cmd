/*
 * bdate.c - DATE builtin
 *
 * DATE [/T | date]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int parse_date(const char *s, libcmd_time_t *out)
{
    int month, day, year;
    int n = 0;

    /* Accept mm-dd-yy, mm-dd-yyyy, mm/dd/yy, mm/dd/yyyy */
    if (sscanf(s, "%d-%d-%d%n", &month, &day, &year, &n) < 3)
        if (sscanf(s, "%d/%d/%d%n", &month, &day, &year, &n) < 3)
            return -1;
    if (s[n] != '\0' && s[n] != ' ' && s[n] != '\n')
        return -1;

    if (month < 1 || month > 12 || day < 1 || day > 31)
        return -1;
    if (year < 80)
        year += 2000;
    else if (year < 100)
        year += 1900;

    out->year  = year;
    out->month = month;
    out->day   = day;
    return 0;
}

static int set_system_date(const char *s)
{
    libcmd_time_t now, dt;
    struct tm tm_buf;
    time_t t;

    libcmd_get_local_time(&now);
    if (parse_date(s, &dt) < 0)
        return -1;

    /* Reuse the current time-of-day */
    memset(&tm_buf, 0, sizeof(tm_buf));
    tm_buf.tm_year = dt.year - 1900;
    tm_buf.tm_mon  = dt.month - 1;
    tm_buf.tm_mday = dt.day;
    tm_buf.tm_hour = now.hour;
    tm_buf.tm_min  = now.minute;
    tm_buf.tm_sec  = now.second;

    t = mktime(&tm_buf);
    if (t == (time_t)-1)
        return -1;

    if (libcmd_set_system_time(t) < 0)
        return -2; /* EPERM etc. */
    return 0;
}

int builtin_date(cmd_context_t *ctx, int argc, char **argv)
{
    int opt_t = 0;
    const char *set_arg = NULL;
    int i;

    (void)ctx;

    for (i = 1; i < argc; i++) {
        if (libcmd_strcasecmp(argv[i], "/?") == 0) {
            fputs(cmd_gettext(MSG_HELP_DATE), stdout);
            return 0;
        }
        if (libcmd_is_switch(argv[i], "T")) {
            if (argv[i][1] == 'T' || argv[i][1] == 't')
                opt_t = 1;
        } else {
            set_arg = argv[i];
        }
    }

    {
        libcmd_time_t t;
        static const char *wday_names[] = {
            "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
        };
        libcmd_get_local_time(&t);

        printf(cmd_gettext(MSG_INFO_CURRENT_DATE),
               wday_names[t.wday], t.month, t.day, t.year);
    }

    /* /T: display only */
    if (opt_t)
        return 0;

    if (set_arg == NULL && argc > 1) {
        /* A non-/T, non-/? argument was consumed by is_switch fallback */
        return 0;
    }

    if (set_arg == NULL) {
        char line[64];
        printf("%s", cmd_gettext(MSG_ASK_ENTER_DATE));
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL)
            return 1;
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0')
            return 0; /* keep current date */
        set_arg = line;
    }

    {
        int rc = set_system_date(set_arg);
        if (rc < 0) {
            if (rc == -2)
                fprintf(stderr, cmd_gettext(MSG_ERR_SET_DATE),
                        libcmd_strerror());
            else
                fprintf(stderr, "%s", cmd_gettext(MSG_ERR_BAD_DATE));
            return 1;
        }
    }

    return 0;
}
