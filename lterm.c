#if !defined(_WIN32) && !defined(_WIN64) && !defined(_MSC_VER)
/*
 * lterm.c - terminal operations
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

/*
 * CMD color nibble to ANSI color code mapping.
 * The nibble values are:
 *   0=Black  1=Blue   2=Green   3=Cyan(Aqua)
 *   4=Red    5=Magenta(Purple) 6=Yellow  7=White(Gray)
 *   8=DarkGray 9=LightBlue 10=LightGreen 11=LightCyan
 *   12=LightRed 13=LightMagenta 14=LightYellow 15=BrightWhite
 */
static const int cmd_fg_ansi[16] = {
    30, 34, 32, 36, 31, 35, 33, 37,
    90, 94, 92, 96, 91, 95, 93, 97
};

static const int cmd_bg_ansi[16] = {
    40, 44, 42, 46, 41, 45, 43, 47,
    100, 104, 102, 106, 101, 105, 103, 107
};

int libcmd_isatty(int fd)
{
    return isatty(fd);
}

int libcmd_cls(void)
{
    /* ANSI escape: clear screen and move cursor to top-left */
    return (fputs("\033[2J\033[H", stdout) >= 0) ? 0 : -1;
}

int libcmd_set_color(int fg, int bg)
{
    char buf[64];
    int pos = 0;

    buf[pos++] = '\033';
    buf[pos++] = '[';

    if (fg >= 0 && fg <= 15) {
        int code = cmd_fg_ansi[fg];
        if (code >= 100) {
            buf[pos++] = '0' + code / 100;
            buf[pos++] = '0' + (code / 10) % 10;
            buf[pos++] = '0' + code % 10;
        } else {
            buf[pos++] = '0' + code / 10;
            buf[pos++] = '0' + code % 10;
        }
    }

    if (bg >= 0 && bg <= 15) {
        int code;

        if (fg >= 0 && fg <= 15)
            buf[pos++] = ';';
        code = cmd_bg_ansi[bg];
        if (code >= 100) {
            buf[pos++] = '0' + code / 100;
            buf[pos++] = '0' + (code / 10) % 10;
            buf[pos++] = '0' + code % 10;
        } else {
            buf[pos++] = '0' + code / 10;
            buf[pos++] = '0' + code % 10;
        }
    }

    buf[pos++] = 'm';
    buf[pos]   = '\0';

    return (fputs(buf, stdout) >= 0) ? 0 : -1;
}

void libcmd_reset_color(void)
{
    fputs("\033[0m", stdout);
}

int libcmd_set_title(const char *title)
{
    /* ESC]0;title BEL */
    if (!isatty(STDOUT_FILENO))
        return -1;
    fprintf(stdout, "\033]0;%s\007", title);
    fflush(stdout);
    return 0;
}

int libcmd_get_terminal_size(int *cols, int *rows)
{
#ifdef TIOCGWINSZ
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (cols) *cols = (int)ws.ws_col;
        if (rows) *rows = (int)ws.ws_row;
        return 0;
    }
#endif
    /* Fallback */
    if (cols) *cols = 80;
    if (rows) *rows = 24;
    return -1;
}

/* -------------------------------------------------------------------------
 * Controlling-terminal Y/N prompt
 * ---------------------------------------------------------------------- */

int libcmd_tty_ask_yn(const char *msg)
{
    struct termios old, raw;
    int fd;
    int result = 1; /* default: terminate */
    int have_raw = 0;
    size_t msg_len = strlen(msg);

    /* /dev/tty is the controlling terminal: it keeps working even when
     * stdin/stdout are redirected. */
    fd = open("/dev/tty", O_RDWR);
    if (fd < 0)
        return 1;

    /* The terminal is in canonical mode: a single-key answer would
     * sit in the line buffer until Enter.  Switch to cbreak (no
     * ICANON) so one keystroke answers immediately; echo stays on. */
    if (tcgetattr(fd, &old) == 0)
    {
        raw = old;
        raw.c_lflag &= ~(tcflag_t)ICANON;
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(fd, TCSANOW, &raw) == 0)
            have_raw = 1;
    }

    for (;;)
    {
        char c = 0;
        long n;

        if (write(fd, msg, msg_len) < 0)
            break;

        do
        {
            n = read(fd, &c, 1);
        } while (n < 0 && errno == EINTR);

        if (n <= 0)
        {
            /* EOF / error on the terminal: terminate */
            break;
        }
        if (c == 'Y' || c == 'y')
        {
            result = 1;
            break;
        }
        if (c == 'N' || c == 'n')
        {
            result = 0;
            break;
        }
        /* Any other key: ask again */
    }

    if (have_raw)
        tcsetattr(fd, TCSANOW, &old);
    (void)write(fd, "\n", 1);
    close(fd);
    return result;
}
#endif
