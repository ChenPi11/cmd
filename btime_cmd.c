/*
 * btime_cmd.c - TIME builtin
 *
 * TIME [/T | time]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int parse_time(const char *s, libcmd_time_t *out)
{
    int hour, minute, second = 0;
    int n = 0;

    if (sscanf(s, "%d:%d:%d%n", &hour, &minute, &second, &n) < 2)
        if (sscanf(s, "%d:%d%n", &hour, &minute, &n) < 2)
            return -1;
    if (s[n] != '\0' && s[n] != ' ' && s[n] != '\n')
        return -1;

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59)
        return -1;

    out->hour   = hour;
    out->minute = minute;
    out->second = second;
    return 0;
}

static int set_system_time(const char *s)
{
    libcmd_time_t now, tm_val;
    struct tm tm_buf;
    time_t t;

    libcmd_get_local_time(&now);
    if (parse_time(s, &tm_val) < 0)
        return -1;

    /* Reuse today's date */
    memset(&tm_buf, 0, sizeof(tm_buf));
    tm_buf.tm_year = now.year - 1900;
    tm_buf.tm_mon  = now.month - 1;
    tm_buf.tm_mday = now.day;
    tm_buf.tm_hour = tm_val.hour;
    tm_buf.tm_min  = tm_val.minute;
    tm_buf.tm_sec  = tm_val.second;

    t = mktime(&tm_buf);
    if (t == (time_t)-1)
        return -1;

    if (libcmd_set_system_time(t) < 0)
        return -2; /* EPERM etc. */
    return 0;
}

int builtin_time_cmd(cmd_context_t *ctx, int argc, char **argv)
{
    int opt_t = 0;
    const char *set_arg = NULL;
    int i;

    (void)ctx;

    for (i = 1; i < argc; i++) {
        if (libcmd_strcasecmp(argv[i], "/?") == 0) {
            fputs(cmd_gettext(MSG_HELP_TIME), stdout);
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
        libcmd_get_local_time(&t);
        printf(cmd_gettext(MSG_INFO_CURRENT_TIME),
               t.hour, t.minute, t.second, t.ms / 10);
    }

    /* /T: display only */
    if (opt_t)
        return 0;

    if (set_arg == NULL) {
        char line[64];
        printf("%s", cmd_gettext(MSG_ASK_ENTER_TIME));
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL)
            return 1;
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0')
            return 0; /* keep current time */
        set_arg = line;
    }

    {
        int rc = set_system_time(set_arg);
        if (rc < 0) {
            if (rc == -2)
                fprintf(stderr, cmd_gettext(MSG_ERR_SET_TIME),
                        libcmd_strerror());
            else
                fprintf(stderr, "%s", cmd_gettext(MSG_ERR_BAD_TIME));
            return 1;
        }
    }

    return 0;
}
