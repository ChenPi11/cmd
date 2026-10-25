#define _CRT_SECURE_NO_WARNINGS
/*
 * lwlinenoise.c - line editing library (Windows implementation)
 *
 * Windows implementation using Console API for line editing.
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"
#include "glinenoise.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* History */
#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096

static char **history = NULL;
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;

/* Completion */
static linenoiseCompletionCallback *completionCallback = NULL;
static int linenoiseCompletionEnabled = 0;

/* Console handles */
static HANDLE hConsoleIn = NULL;
static HANDLE hConsoleOut = NULL;
static DWORD originalConsoleMode = 0;

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void init_console(void)
{
    if (hConsoleIn == NULL) {
        hConsoleIn = GetStdHandle(STD_INPUT_HANDLE);
        hConsoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleMode(hConsoleIn, &originalConsoleMode);
    }
}

static void set_raw_mode(int enable)
{
    DWORD mode;

    init_console();

    if (enable) {
        mode = originalConsoleMode;
        mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        SetConsoleMode(hConsoleIn, mode);
    } else {
        SetConsoleMode(hConsoleIn, originalConsoleMode);
    }
}

static int read_console_input(INPUT_RECORD *ir)
{
    DWORD count;
    init_console();
    return ReadConsoleInputA(hConsoleIn, ir, 1, &count) ? 1 : 0;
}

static void write_console(const char *str, int len)
{
    DWORD written;
    init_console();
    WriteConsoleA(hConsoleOut, str, (DWORD)len, &written, NULL);
}

static void write_console_char(char c)
{
    write_console(&c, 1);
}

/* -------------------------------------------------------------------------
 * History
 * ---------------------------------------------------------------------- */

int linenoiseHistoryAdd(const char *line)
{
    char **new_history;
    int new_len;

    if (history_max_len == 0)
        return 0;

    if (line == NULL || *line == '\0')
        return 0;

    new_len = history_len + 1;
    if (new_len > history_max_len)
        new_len = history_max_len;

    new_history = (char **)realloc(history, sizeof(char *) * (size_t)new_len);
    if (new_history == NULL)
        return -1;

    history = new_history;
    history[history_len] = libcmd_strdup(line);
    if (history[history_len] == NULL)
        return -1;

    history_len++;
    if (history_len > history_max_len)
        history_len = history_max_len;

    return 0;
}

int linenoiseHistorySetMaxLen(int len)
{
    if (len < 0)
        return -1;
    history_max_len = len;
    if (history_len > history_max_len)
        history_len = history_max_len;
    return 0;
}

int linenoiseHistorySave(const char *filename)
{
    FILE *fp;
    int i;

    if (filename == NULL)
        return -1;

    fp = fopen(filename, "w");
    if (fp == NULL)
        return -1;

    for (i = 0; i < history_len; i++) {
        fprintf(fp, "%s\n", history[i]);
    }

    fclose(fp);
    return 0;
}

int linenoiseHistoryLoad(const char *filename)
{
    FILE *fp;
    char buf[LINENOISE_MAX_LINE];

    if (filename == NULL)
        return -1;

    fp = fopen(filename, "r");
    if (fp == NULL)
        return -1;

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
        if (len > 0)
            linenoiseHistoryAdd(buf);
    }

    fclose(fp);
    return 0;
}

/* -------------------------------------------------------------------------
 * Completion
 * ---------------------------------------------------------------------- */

void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn)
{
    completionCallback = fn;
}

void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str)
{
    char **new_completions;
    size_t new_count;

    if (lc == NULL || str == NULL)
        return;

    new_count = lc->len + 1;
    new_completions = (char **)realloc(lc->cvec,
                                       sizeof(char *) * new_count);
    if (new_completions == NULL)
        return;

    lc->cvec = new_completions;
    lc->cvec[lc->len] = libcmd_strdup(str);
    if (lc->cvec[lc->len] == NULL)
        return;

    lc->len++;
}

void linenoiseSetCompletionEnabled(int on)
{
    linenoiseCompletionEnabled = on;
}

/* -------------------------------------------------------------------------
 * Multi-line
 * ---------------------------------------------------------------------- */

void linenoiseSetMultiLine(int ml)
{
    /* Windows console handles multi-line differently */
    (void)ml;
}

/* -------------------------------------------------------------------------
 * Main line editing function
 * ---------------------------------------------------------------------- */

char *linenoise(const char *prompt)
{
    char *line;
    int pos = 0;
    int len = 0;
    INPUT_RECORD ir;
    DWORD count;
    int history_index = history_len;

    init_console();

    line = (char *)malloc(LINENOISE_MAX_LINE);
    if (line == NULL)
        return NULL;

    line[0] = '\0';

    /* Display prompt */
    if (prompt)
        write_console(prompt, (int)strlen(prompt));

    set_raw_mode(1);

    while (1) {
        if (!ReadConsoleInputA(hConsoleIn, &ir, 1, &count) || count == 0) {
            set_raw_mode(0);
            free(line);
            return NULL;
        }

        if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown)
            continue;

        {
            KEY_EVENT_RECORD *ker = &ir.Event.KeyEvent;
            char ch = ker->uChar.AsciiChar;
            DWORD ctrl = ker->dwControlKeyState;

            if (ch == '\r' || ch == '\n') {
                /* Enter - return line */
                write_console("\r\n", 2);
                break;
            }
            else if (ch == '\b' || ker->wVirtualKeyCode == VK_BACK) {
                /* Backspace */
                if (pos > 0) {
                    memmove(&line[pos-1], &line[pos], (size_t)(len - pos + 1));
                    pos--;
                    len--;
                    /* Redisplay line */
                    write_console("\b", 1);
                    write_console(&line[pos], len - pos);
                    write_console(" ", 1);
                    {
                        int i;
                        char buf[32];
                        int n = libcmd_sprintf_s(buf, sizeof(buf), "\r");
                        write_console(buf, n);
                        if (prompt)
                            write_console(prompt, (int)strlen(prompt));
                        write_console(line, len);
                        /* Position cursor */
                        n = libcmd_sprintf_s(buf, sizeof(buf), "\x1b[%dC", pos);
                        write_console(buf, n);
                    }
                }
            }
            else if (ch == 0x1B) {
                /* Escape sequence or Ctrl+C */
                if (ker->wVirtualKeyCode == VK_ESCAPE) {
                    /* Ctrl+C */
                    write_console("^C\r\n", 4);
                    set_raw_mode(0);
                    free(line);
                    errno = EAGAIN;
                    return NULL;
                }
            }
            else if (ch == 0x03) {
                /* Ctrl+C */
                write_console("^C\r\n", 4);
                set_raw_mode(0);
                free(line);
                errno = EAGAIN;
                return NULL;
            }
            else if (ch == 0x04) {
                /* Ctrl+D - EOF */
                if (len == 0) {
                    set_raw_mode(0);
                    free(line);
                    return NULL;
                }
            }
            else if (ch == 0x0C) {
                /* Ctrl+L - clear screen */
                write_console("\033[2J\033[H", 7);
                if (prompt)
                    write_console(prompt, (int)strlen(prompt));
                write_console(line, len);
            }
            else if (ker->wVirtualKeyCode == VK_UP) {
                /* Up arrow - previous history */
                if (history_len > 0 && history_index > 0) {
                    history_index--;
                    /* Clear current line */
                    write_console("\r", 1);
                    if (prompt)
                        write_console(prompt, (int)strlen(prompt));
                    {
                        int i;
                        for (i = 0; i < len; i++)
                            write_console(" ", 1);
                    }
                    write_console("\r", 1);
                    if (prompt)
                        write_console(prompt, (int)strlen(prompt));

                    strcpy(line, history[history_index]);
                    len = (int)strlen(line);
                    pos = len;
                    write_console(line, len);
                }
            }
            else if (ker->wVirtualKeyCode == VK_DOWN) {
                /* Down arrow - next history */
                if (history_index < history_len - 1) {
                    history_index++;
                    /* Clear current line */
                    write_console("\r", 1);
                    if (prompt)
                        write_console(prompt, (int)strlen(prompt));
                    {
                        int i;
                        for (i = 0; i < len; i++)
                            write_console(" ", 1);
                    }
                    write_console("\r", 1);
                    if (prompt)
                        write_console(prompt, (int)strlen(prompt));

                    strcpy(line, history[history_index]);
                    len = (int)strlen(line);
                    pos = len;
                    write_console(line, len);
                }
                else if (history_index == history_len - 1) {
                    history_index++;
                    /* Clear current line */
                    write_console("\r", 1);
                    if (prompt)
                        write_console(prompt, (int)strlen(prompt));
                    {
                        int i;
                        for (i = 0; i < len; i++)
                            write_console(" ", 1);
                    }
                    write_console("\r", 1);
                    if (prompt)
                        write_console(prompt, (int)strlen(prompt));

                    line[0] = '\0';
                    len = 0;
                    pos = 0;
                }
            }
            else if (ker->wVirtualKeyCode == VK_LEFT) {
                /* Left arrow */
                if (pos > 0) {
                    pos--;
                    write_console("\b", 1);
                }
            }
            else if (ker->wVirtualKeyCode == VK_RIGHT) {
                /* Right arrow */
                if (pos < len) {
                    write_console(&line[pos], 1);
                    pos++;
                }
            }
            else if (ch == '\t' && linenoiseCompletionEnabled && completionCallback) {
                /* Tab completion */
                linenoiseCompletions lc = {0, NULL};
                completionCallback(line, &lc);
                if (lc.len > 0) {
                    /* Replace line with completion */
                    free(line);
                    line = libcmd_strdup(lc.cvec[0]);
                    if (line == NULL) {
                        set_raw_mode(0);
                        return NULL;
                    }
                    len = (int)strlen(line);
                    pos = len;

                    /* Clear and redisplay */
                    write_console("\r", 1);
                    if (prompt)
                        write_console(prompt, (int)strlen(prompt));
                    write_console(line, len);
                }
                /* Free completions */
                {
                    size_t i;
                    for (i = 0; i < lc.len; i++)
                        free(lc.cvec[i]);
                    free(lc.cvec);
                }
            }
            else if (ch >= 32 && ch < 127) {
                /* Printable character */
                if (len < LINENOISE_MAX_LINE - 1) {
                    memmove(&line[pos+1], &line[pos], (size_t)(len - pos + 1));
                    line[pos] = ch;
                    pos++;
                    len++;
                    /* Redisplay from cursor */
                    write_console(&line[pos-1], len - pos + 1);
                    if (pos < len) {
                        /* Move cursor back to correct position */
                        char buf[32];
                        int n = libcmd_sprintf_s(buf, sizeof(buf), "\x1b[%dD", len - pos);
                        write_console(buf, n);
                    }
                }
            }
        }
    }

    set_raw_mode(0);

    if (len == 0) {
        free(line);
        return NULL;
    }

    return line;
}

void linenoiseFree(void *ptr)
{
    free(ptr);
}

#endif
