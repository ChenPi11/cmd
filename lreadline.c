/*
 * lreadline.c - line editing with linenoise
 *
 * Provides line reading with history and tab completion support
 * using the bundled linenoise library.
 * History is saved to ~/.cmd_history
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"
#include "glinenoise.h"

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

/* Callback invoked when a signal interrupts the terminal read.
 * Must return the signal number that interrupted the read (e.g. SIGINT,
 * SIGTSTP), or 0 if nothing was consumed.  The UI echo (^C / ^Z / CRLF)
 * is handled entirely below with POSIX stdio, so the same code path works
 * whether readline, linenoise or plain fgets is used under the hood. */
static int (*libcmd_rl_signal_cb)(void) = NULL;

/* -------------------------------------------------------------------------
 * POSIX-only signal echo helper (no readline / Linux-only calls)
 * ---------------------------------------------------------------------- */

/* Echo the canonical "^X" visual for the caught signal, then CRLF, and
 * set errno to EINTR so the caller can distinguish signal interrupts
 * from genuine EOF.  The sig number comes from the interpreter hook;
 * only SIGINT and SIGTSTP get named visuals, anything else falls back
 * to the ASCII control-character convention ('@' + sig). */
static void libcmd_echo_signal_and_crlf(int sig)
{
    char visual[8];
    int  vlen = 0;

    visual[vlen++] = '^';
    if (sig == SIGINT)
        visual[vlen++] = 'C';
    else if (sig == SIGTSTP)
        visual[vlen++] = 'Z';
    else if (sig >= 1 && sig <= 26)
        visual[vlen++] = (char)('@' + sig);
    else
        visual[vlen - 1] = '?'; /* should not happen */
    visual[vlen] = '\0';
    fputs(visual, stdout);
    fputs("\r\n", stdout);
    fflush(stdout);
    errno = EINTR;
}

/* -------------------------------------------------------------------------
 * Signal handling while the terminal read is in progress
 * ---------------------------------------------------------------------- */

void libcmd_readline_set_signal_hook(int (*hook)(void))
{
    libcmd_rl_signal_cb = hook;
}

/* -------------------------------------------------------------------------
 * Tab completion callback (linenoise)
 * ----------------------------------------------------------------------
 *
 * linenoise's completion model works differently from readline's: the
 * callback receives the full line buffer and must add *full replacement
 * lines* as completions.  When the user commits a completion (by typing
 * any non-TAB character), linenoise replaces the entire buffer with the
 * selected completion.
 *
 * Both '/' and '\' work as path separators; directories get a trailing
 * separator matching the style used in the word being completed.
 */

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

    /* The word being completed is the last space/tab-delimited token.
     * Scan backwards from the end of the line. */
    word_end = line + strlen(line);
    word_start = word_end;
    while (word_start > line &&
           word_start[-1] != ' ' && word_start[-1] != '\t')
        word_start--;

    /* Split into directory part and file prefix.  Both '/' and '\' act
     * as separators; sep remembers the style used in the word. */
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

    /* Open the target directory ('\'-separators normalized) */
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

        /* Stat the full candidate path (without chdir'ing) */
        if (norm_dir && norm_dir[0])
            libcmd_sprintf_s(full, sizeof(full), "%s/%s", norm_dir,
                             entry.name);
        else
            libcmd_sprintf_s(full, sizeof(full), "%s", entry.name);
        is_dir = 0;
        if (libcmd_stat(full, &st, 1) == 0 && st.is_dir)
            is_dir = 1;

        /* Build the full replacement line:
         *   everything before the word
         *   + original directory part
         *   + entry name
         *   + trailing separator if directory */
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

/*
 * Initialize line editing with history file support.
 * Constructs history file path as ~/.cmd_history
 */
void libcmd_readline_init(void)
{
    const char *home;
    size_t pathlen;

    if (readline_initialized)
        return;

    /* Get HOME directory */
    home = libcmd_getenv("HOME");
    if (!home)
        home = "/root";  /* fallback if HOME not set */

    /* Construct history file path: ~/.cmd_history */
    pathlen = strlen(home) + strlen("/.cmd_history") + 1;
    history_file = (char *)malloc(pathlen);
    if (!history_file) {
        fprintf(stderr, "readline_init: out of memory\n");
        return;
    }
    libcmd_sprintf_s(history_file, pathlen, "%s/.cmd_history", home);

    /* Load existing history from file */
    linenoiseHistoryLoad(history_file);

    /* Register tab completion callback */
    linenoiseSetCompletionCallback(linenoise_completion_callback);

    /* Enable multi-line mode so long commands that wrap to the next
     * terminal row are handled correctly (cursor positioning across
     * rows uses CUU/CUD escape sequences instead of assuming the
     * entire line stays on a single row). */
    linenoiseSetMultiLine(1);

    readline_initialized = 1;
}

/* Enable/disable completion keys (cmd /f:on).  Must be called after
 * libcmd_readline_init(). */
void libcmd_readline_set_file_completion(int on)
{
    linenoiseSetCompletionEnabled(on);
}

/*
 * Save history and shutdown line editing.
 * Should be called before exit to persist history.
 */
void libcmd_readline_shutdown(void)
{
    if (!readline_initialized)
        return;

    if (history_file) {
        /* Persist current session history to file */
        linenoiseHistorySave(history_file);
        free(history_file);
        history_file = NULL;
    }

    readline_initialized = 0;
}

/* -------------------------------------------------------------------------
 * Line reading
 * ---------------------------------------------------------------------- */

/*
 * Check the registered signal hook after a NULL read.
 * Returns 1 if a signal was caught and handled (caller should treat this
 * as an interrupted read, not EOF), 0 otherwise.
 * Uses POSIX-only calls: libcmd_rl_signal_cb + stdio fputs; errno = EINTR.
 */
static int libcmd_handle_possible_signal_interrupt(void)
{
    int sig;

    if (libcmd_rl_signal_cb == NULL)
        return 0;

    sig = (*libcmd_rl_signal_cb)();
    if (sig <= 0)
        return 0;

    libcmd_echo_signal_and_crlf(sig);
    return 1;
}

/*
 * Read a line using linenoise with history support.
 * prompt: prompt string to display
 * buf: buffer to store the read line
 * size: size of buffer
 *
 * Returns:
 *   - buf on successful read
 *   - NULL on EOF
 *   - NULL with errno == EINTR on signal interrupt (caller should retry)
 *   - In case of error, still returns buf with an empty line
 *
 * The signal interrupt path (^C / ^Z echo + CRLF) is 100% POSIX stdio and
 * does not call any readline / Linux-specific API, so the exact same
 * behaviour is produced regardless of which line-editing backend is
 * active below (readline / linenoise / fgets).
 */
char *libcmd_readline(const char *prompt, char *buf, size_t size)
{
    char *line;

    if (!readline_initialized)
        libcmd_readline_init();

    line = linenoise(prompt);

    if (line == NULL) {
        /*
         * linenoise returns NULL on:
         *   - Ctrl+C (interrupt) : errno = EAGAIN
         *   - Ctrl+Z (suspend)   : errno = EINTR
         *   - Ctrl+D (EOF)       : errno = ENOENT
         *   - General error
         *
         * linenoise disables ISIG in raw mode, so our SIGINT/SIGTSTP
         * handlers are never invoked.  Detect the signals via errno
         * and emit "^C\r\n" / "^Z\r\n" with pure POSIX stdio.
         */
        if (errno == EAGAIN) {
            libcmd_echo_signal_and_crlf(SIGINT);
            return NULL;
        }
        if (errno == EINTR) {
            libcmd_echo_signal_and_crlf(SIGTSTP);
            return NULL;
        }
        /* Also consult the signal hook (for non-linenoise backends) */
        if (libcmd_handle_possible_signal_interrupt())
            return NULL;
        /* Genuine EOF: linenoise won't have output a newline, so emit
         * one now so the parent shell's prompt starts on a fresh line. */
        fputs("\r\n", stdout);
        fflush(stdout);
        return NULL;
    }

    /*
     * Fallback: if Ctrl+Z (0x1A) slipped through (e.g. non-tty input),
     * detect it and treat it like Ctrl+C.  Normally handled by the
     * errno == EINTR path above when linenoise is in raw mode.
     */
    if (strchr(line, '\032')) {
        linenoiseFree(line);
        libcmd_echo_signal_and_crlf(SIGTSTP);
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
