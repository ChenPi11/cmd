#define _CRT_SECURE_NO_WARNINGS
/*
 * lwterm.c - terminal operations (Windows implementation)
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <io.h>

/*
 * CMD color nibble to Windows console color mapping.
 * The nibble values are:
 *   0=Black  1=Blue   2=Green   3=Cyan(Aqua)
 *   4=Red    5=Magenta(Purple) 6=Yellow  7=White(Gray)
 *   8=DarkGray 9=LightBlue 10=LightGreen 11=LightCyan
 *   12=LightRed 13=LightMagenta 14=LightYellow 15=BrightWhite
 */
static const int cmd_win32_color[16] = {
    0,  /* Black -> FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED */
    1,  /* Blue -> FOREGROUND_BLUE */
    2,  /* Green -> FOREGROUND_GREEN */
    3,  /* Cyan -> FOREGROUND_BLUE | FOREGROUND_GREEN */
    4,  /* Red -> FOREGROUND_RED */
    5,  /* Magenta -> FOREGROUND_BLUE | FOREGROUND_RED */
    6,  /* Yellow -> FOREGROUND_GREEN | FOREGROUND_RED */
    7,  /* White -> FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED */
    8,  /* DarkGray -> FOREGROUND_INTENSITY */
    9,  /* LightBlue -> FOREGROUND_BLUE | FOREGROUND_INTENSITY */
    10, /* LightGreen -> FOREGROUND_GREEN | FOREGROUND_INTENSITY */
    11, /* LightCyan -> FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY */
    12, /* LightRed -> FOREGROUND_RED | FOREGROUND_INTENSITY */
    13, /* LightMagenta -> FOREGROUND_BLUE | FOREGROUND_RED | FOREGROUND_INTENSITY */
    14, /* LightYellow -> FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY */
    15  /* BrightWhite -> FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY */
};

static int current_fg = 7;  /* Default: White */
static int current_bg = 0;  /* Default: Black */

int libcmd_isatty(int fd)
{
    return _isatty(fd);
}

int libcmd_cls(void)
{
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD coord = {0, 0};
    DWORD written;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD size;

    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
        return -1;

    size = csbi.dwSize.X * csbi.dwSize.Y;
    FillConsoleOutputCharacterA(hConsole, ' ', size, coord, &written);
    FillConsoleOutputAttribute(hConsole, csbi.wAttributes, size, coord, &written);
    SetConsoleCursorPosition(hConsole, coord);

    return 0;
}

int libcmd_set_color(int fg, int bg)
{
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD attributes = 0;

    if (fg >= 0 && fg <= 15) {
        current_fg = fg;
        attributes |= cmd_win32_color[fg] & 0x0F;
    }

    if (bg >= 0 && bg <= 15) {
        current_bg = bg;
        attributes |= (cmd_win32_color[bg] & 0x0F) << 4;
    }

    return SetConsoleTextAttribute(hConsole, attributes) ? 0 : -1;
}

void libcmd_reset_color(void)
{
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

int libcmd_set_title(const char *title)
{
    return SetConsoleTitleA(title) ? 0 : -1;
}

int libcmd_get_terminal_size(int *cols, int *rows)
{
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        if (cols) *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (rows) *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return 0;
    }

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
    HANDLE h_conout;
    DWORD len, written;
    char c;
    int result = 1; /* default: terminate */

    if (msg == NULL)
        return 1;

    /* CONOUT$ keeps working when stdout is redirected, so the prompt
     * always reaches the physical console (the role /dev/tty plays on
     * Unix). */
    h_conout = CreateFileA("CONOUT$",
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           0,
                           NULL);
    if (h_conout == INVALID_HANDLE_VALUE)
        return 1; /* no controlling console: terminate without asking */

    for (;;)
    {
        if (!WriteFile(h_conout, msg, (DWORD)strlen(msg), &written, NULL))
            break;

        /* _getch waits for a single key with no line buffering and no
         * echo; that is the Windows equivalent of the cbreak mode the
         * Unix implementation switches to. */
        c = (char)_getch();

        /* Echo printable keys like a terminal with echo on would. */
        if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F)
        {
            len = 1;
            (void)WriteFile(h_conout, &c, len, &written, NULL);
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

    /* On Unix the ONLCR terminal flag turns "\n" into "\r\n"; the
     * console needs both characters explicitly so the next prompt
     * starts at column 0. */
    (void)WriteFile(h_conout, "\r\n", 2, &len, NULL);
    CloseHandle(h_conout);
    return result;
}

#endif
