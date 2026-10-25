#define _CRT_SECURE_NO_WARNINGS
/*
 * lwreadline.c - line editing with linenoise (Windows implementation)
 *
 * Provides line reading with history and tab completion support
 * using the bundled linenoise library.
 * History is saved to %USERPROFILE%\.cmd_history
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"
#include "glinenoise.h"

#include <windows.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

static char *history_file = NULL;
static int readline_initialized = 0;

/* Callback invoked when a signal interrupts the terminal read. */
static int (*libcmd_rl_signal_cb)(void) = NULL;

/* -------------------------------------------------------------------------
 * Signal handling while the terminal read is in progress
 * ---------------------------------------------------------------------- */

void libcmd_readline_set_signal_hook(int (*hook)(void))
{
    libcmd_rl_signal_cb = hook;
}

/* -------------------------------------------------------------------------
 * Tab completion callback (linenoise)
 * ---------------------------------------------------------------------- */

static void linenoise_completion_callback(const char *line,
                                          linenoiseCompletions *lc)
{
    const char *word_start;
    const char *word_end;
    const char *last_sep;
    size_t dir_len, base_len;
    char sep;
    char *norm_dir = NULL;
    libcmd_dir_t d;
    libcmd_dirent_t entry;
    size_t i;

    /* The word being completed is the last space/tab-delimited token. */
    word_end = line + strlen(line);
    word_start = word_end;
    while (word_start > line &&
           word_start[-1] != ' ' && word_start[-1] != '\t')
        word_start--;

    /* Split into directory part and file prefix. */
    last_sep = NULL;
    sep = '\\';
    for (i = 0; i < (size_t)(word_end - word_start); i++) {
        if (word_start[i] == '/') {
            last_sep = word_start + i;
            sep = '/';
        } else if (word_start[i] == '\\') {
            last_sep = word_start + i;
            sep = '\\';
        }
    }
    if (last_sep) {
        dir_len  = (size_t)(last_sep - word_start) + 1;
        base_len = (size_t)(word_end - last_sep) - 1;
    } else {
        dir_len  = 0;
        base_len = (size_t)(word_end - word_start);
    }

    /* Open the target directory */
    if (dir_len > 0) {
        norm_dir = (char *)malloc(dir_len + 1);
        if (norm_dir == NULL) return;
        memcpy(norm_dir, word_start, dir_len);
        norm_dir[dir_len] = '\0';
        libcmd_path_norm_sep(norm_dir);
        d = libcmd_opendir(norm_dir);
    } else {
        d = libcmd_opendir(".");
    }
    if (d == NULL) {
        free(norm_dir);
        return;
    }

    while (libcmd_readdir(d, &entry) == 0) {
        char full[LIBCMD_NAME_MAX * 2 + 2];
        libcmd_stat_t st;
        int is_dir;
        size_t head_len;
        size_t cand_len;
        char *cand;

        if (strcmp(entry.name, ".") == 0 ||
            strcmp(entry.name, "..") == 0)
            continue;
        /* Hidden files, unless the prefix itself starts with a dot */
        if (base_len > 0 && word_start[dir_len] != '.' &&
            entry.name[0] == '.')
            continue;
        /* Prefix match, case-insensitive like Windows */
        if (base_len > 0 &&
            libcmd_strncasecmp(entry.name, word_start + dir_len, base_len) != 0)
            continue;

        /* Stat the full candidate path */
        if (norm_dir && norm_dir[0])
            libcmd_sprintf_s(full, sizeof(full), "%s\\%s", norm_dir,
                             entry.name);
        else
            libcmd_sprintf_s(full, sizeof(full), "%s", entry.name);
        is_dir = 0;
        if (libcmd_stat(full, &st, 1) == 0 && st.is_dir)
            is_dir = 1;

        /* Build the full replacement line */
        head_len = (size_t)(word_start - line);
        cand_len = head_len + dir_len + strlen(entry.name) + 2;
        cand = (char *)malloc(cand_len);
        if (cand == NULL) break;
        memcpy(cand, line, head_len);
        memcpy(cand + head_len, word_start, dir_len);
        strcpy(cand + head_len + dir_len, entry.name);
        if (is_dir) {
            size_t l = head_len + dir_len + strlen(entry.name);
            cand[l]     = sep;
            cand[l + 1] = '\0';
        }

        linenoiseAddCompletion(lc, cand);
        free(cand);
    }
    libcmd_closedir(d);
    free(norm_dir);
}

/* -------------------------------------------------------------------------
 * Initialization and cleanup
 * ---------------------------------------------------------------------- */

void libcmd_readline_init(void)
{
    const char *home;
    size_t pathlen;

    if (readline_initialized)
        return;

    /* Get USERPROFILE directory */
    home = libcmd_getenv("USERPROFILE");
    if (!home)
        home = libcmd_getenv("HOMEDRIVE");
    if (!home)
        home = "C:";

    /* Construct history file path: %USERPROFILE%\.cmd_history */
    pathlen = strlen(home) + strlen("\\.cmd_history") + 1;
    history_file = (char *)malloc(pathlen);
    if (!history_file) {
        fprintf(stderr, "readline_init: out of memory\n");
        return;
    }
    libcmd_sprintf_s(history_file, pathlen, "%s\\.cmd_history", home);

    /* Load existing history from file */
    linenoiseHistoryLoad(history_file);

    /* Register tab completion callback */
    linenoiseSetCompletionCallback(linenoise_completion_callback);

    /* Enable multi-line mode */
    linenoiseSetMultiLine(1);

    readline_initialized = 1;
}

void libcmd_readline_set_file_completion(int on)
{
    linenoiseSetCompletionEnabled(on);
}

void libcmd_readline_shutdown(void)
{
    if (!readline_initialized)
        return;

    if (history_file) {
        linenoiseHistorySave(history_file);
        free(history_file);
        history_file = NULL;
    }

    readline_initialized = 0;
}

/* -------------------------------------------------------------------------
 * Line reading
 * ---------------------------------------------------------------------- */

char *libcmd_readline(const char *prompt, char *buf, size_t size)
{
    char *line;

    if (!readline_initialized)
        libcmd_readline_init();

    line = linenoise(prompt);

    if (line == NULL) {
        /* EOF or error */
        return NULL;
    }

    /* Copy the line to the caller's buffer, ensuring null termination */
    if (size > 0) {
        size_t len = strlen(line);
        if (len >= size)
            len = size - 1;
        memcpy(buf, line, len);
        buf[len] = '\0';
    }

    /* Add non-empty lines to history */
    if (*line != '\0')
        linenoiseHistoryAdd(line);

    /* Free the linenoise-allocated buffer */
    linenoiseFree(line);

    return buf;
}

#endif
