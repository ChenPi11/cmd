/*
 * bstart.c - START builtin
 *
 * START ["title"] [/D path] [/I] [/MIN] [/MAX] [/WAIT] [/B]
 *       [/ABOVENORMAL | /BELOWNORMAL | /HIGH | /LOW | /NORMAL | /REALTIME]
 *       command [parameters]
 *
 * On Unix, START behaves as follows:
 *   - If the argument is an existing file or directory, xdg-open(1) is
 *     invoked to open it with the desktop default application.
 *   - If the argument is a command, it is looked up in PATH and run in a
 *     new terminal window.
 *   - If no arguments are given, a new terminal window running the
 *     current cmd binary is opened.  If the binary's own path cannot be
 *     resolved, the terminal is opened without a command instead.
 *
 *   /B   Suppress the new terminal window; run the command directly.
 *   /WAIT  Wait for the launched process to finish.
 *   /D path  Set the working directory for the child.
 *
 * On Windows, START follows cmd.exe: folders and documents are opened
 * through ShellExecute (Explorer for directories, the registered
 * default application for documents), commands are run in a newly
 * created console window, and with no arguments a new console running
 * this shell is opened.
 *
 * On Unix, path arguments are normalised before being forwarded to
 * xdg-open or the terminal emulator:
 *   - Bare names (no / or \) that exist in the current directory get
 *     "./" prepended (e.g. "cmd.exe" becomes "./cmd.exe").
 *   - Windows-style backslashes are converted to forward slashes
 *     (e.g. "dir\\subdir\\file" becomes "dir/subdir/file").
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Startup helpers (platform split)                                   */
/* ------------------------------------------------------------------ */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

/*
 * Windows: no terminal emulator is involved.  A STARTed command runs
 * in a newly opened console window (the way cmd.exe's START works);
 * with no command a plain new console is opened.
 * Returns the child PID on success, -1 on failure.
 */
static int start_in_terminal(char *const argv[])
{
    return libcmd_exec_console(argv, libcmd_get_environ());
}

#else

/* Candidates tried in order; the first one found in PATH wins. */
static const char *const term_candidates[] = {
    "x-terminal-emulator", "konsole", "gnome-terminal",
    "xfce4-terminal", "kitty", "alacritty", "wezterm", "xterm",
    "dtterm", "rxvt",
    NULL
};

/*
 * Find a terminal emulator.  Writes the resolved path into 'buf'
 * and returns 0 on success, -1 if no terminal could be found.
 * Prefers $TERMINAL, then tries the candidate list.
 */
static int find_terminal(char *buf, size_t size)
{
    const char *path_env = libcmd_getenv("PATH");
    const char *term;
    int i;

    if (!path_env) path_env = "/usr/bin:/bin";

    term = libcmd_getenv("TERMINAL");
    if (term && term[0] &&
        libcmd_find_exec(term, path_env, buf, size) == 0)
        return 0;

    for (i = 0; term_candidates[i]; i++) {
        if (libcmd_find_exec(term_candidates[i], path_env,
                             buf, size) == 0)
            return 0;
    }
    return -1;
}

/* Forward declaration -- defined below. */
static char *posix_path_dup(const char *arg);

/* Launch 'argv' (null-terminated) inside a new terminal window.
 * Arguments are normalised to POSIX paths before being forwarded.
 * If argv is NULL or argv[0] is NULL, the terminal is opened without
 * any command.
 * Returns child PID on success, -1 on failure. */
static int start_in_terminal(char *const argv[])
{
    char term_path[CMD_MAX_PATH];
    const char *term_name;
    int n, i, j;
    char **nargv;
    int pid;

    if (!libcmd_has_display())
        return -1;
    if (find_terminal(term_path, sizeof(term_path)) < 0)
        return -1;

    /* Use the basename as argv[0] for the terminal */
    term_name = strrchr(term_path, '/');
    term_name = term_name ? term_name + 1 : term_path;

    for (n = 0; argv && argv[n]; n++);

    /* argv[0]=terminal name, then "-e" + command if a command is given */
    nargv = (char **)malloc((size_t)(n ? n + 3 : 1) * sizeof(char *));
    if (nargv == NULL)
        return -1;

    i = 0;
    nargv[i++] = (char *)term_name;
    if (n > 0) {
        nargv[i++] = (char *)"-e";
        for (j = 0; j < n; j++)
            nargv[i + j] = posix_path_dup(argv[j]);
        i += n;
    }
    nargv[i] = NULL;

    pid = libcmd_exec_async(term_path, nargv, libcmd_get_environ(),
                            -1, -1, -1, 0);

    for (j = 0; j < n; j++)
        free(nargv[2 + j]);
    free(nargv);
    return pid;
}

/*
 * Normalise a path argument for POSIX consumers (xdg-open, terminal).
 * - Bare names (no / or \) that exist in the current directory get
 *   "./" prepended so the consumer can find them.
 * - All backslashes are converted to forward slashes.
 * Returns a freshly allocated string; the caller must free it.
 */
static char *posix_path_dup(const char *arg)
{
    char buf[CMD_MAX_PATH];
    int has_sep;

    has_sep = (strchr(arg, '/') != NULL || strchr(arg, '\\') != NULL);

    if (!has_sep) {
        libcmd_stat_t st;
        libcmd_sprintf_s(buf, sizeof(buf), "./%s", arg);
        if (libcmd_stat(buf, &st, 1) == 0) {
            libcmd_path_norm_sep(buf);
            return libcmd_strdup(buf);
        }
    }

    libcmd_sprintf_s(buf, sizeof(buf), "%s", arg);
    libcmd_path_norm_sep(buf);
    return libcmd_strdup(buf);
}

#endif

/* ------------------------------------------------------------------ */
/*  builtin_start                                                      */
/* ------------------------------------------------------------------ */

int builtin_start(cmd_context_t *ctx, int argc, char **argv)
{
    int opt_wait     = 0;
    int opt_b        = 0;
    int nice_level   = 0;
    int argi         = 1;
    char exec_path[CMD_MAX_PATH];
    const char *path_env;
    char saved_cwd[CMD_MAX_PATH];
    const char *opt_d = NULL;
    libcmd_exit_info_t exit_info;

    (void)ctx;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_START), stdout);
        return 0;
    }

    /* Skip optional title string in quotes */
    if (argi < argc && argv[argi][0] == '"')
        argi++;

    /* Parse flags.  Only exact switch spellings are consumed; any other
     * '/'-leading token is treated as the command.  This matters on Unix,
     * where absolute paths such as /bin/echo start with '/' and their
     * second letter can coincide with a switch letter (e.g. /B..). */
    while (argi < argc && libcmd_is_switch(argv[argi], "WBDIMHLNRA")) {
        const char *sw = argv[argi];

        if (libcmd_strcasecmp(sw, "/WAIT") == 0)
            { opt_wait = 1; argi++; }
        else if (libcmd_strcasecmp(sw, "/B") == 0)
            { opt_b = 1; argi++; }
        else if (libcmd_strcasecmp(sw, "/D") == 0) {
            argi++;
            if (argi < argc) opt_d = argv[argi++];
        }
        else if (libcmd_strcasecmp(sw, "/LOW") == 0)
            { nice_level = 19; argi++; }
        else if (libcmd_strcasecmp(sw, "/BELOWNORMAL") == 0)
            { nice_level = 5; argi++; }
        else if (libcmd_strcasecmp(sw, "/NORMAL") == 0)
            { nice_level = 0; argi++; }
        else if (libcmd_strcasecmp(sw, "/ABOVENORMAL") == 0)
            { nice_level = -5; argi++; }
        else if (libcmd_strcasecmp(sw, "/HIGH") == 0)
            { nice_level = -10; argi++; }
        else if (libcmd_strcasecmp(sw, "/REALTIME") == 0)
            { nice_level = -20; argi++; }
        else if (libcmd_strcasecmp(sw, "/MIN") == 0 ||
                 libcmd_strcasecmp(sw, "/MAX") == 0 ||
                 libcmd_strcasecmp(sw, "/I") == 0 ||
                 libcmd_strcasecmp(sw, "/SHARED") == 0 ||
                 libcmd_strcasecmp(sw, "/SEPARATE") == 0)
            /* Accepted for compatibility; no-op on Unix. */
            { argi++; }
        else
            break;
    }

    /* Save current directory and chdir to /D target if given */
    if (opt_d) {
        if (libcmd_getcwd(saved_cwd, sizeof(saved_cwd)) == NULL)
            saved_cwd[0] = '\0';
        if (libcmd_chdir(opt_d) != 0) {
            fprintf(stderr, cmd_gettext(MSG_ERR_START_CHDIR), opt_d);
            return 1;
        }
    }

    /* ------------------------------------------------------------------
     * Case 1: no arguments -- open a new terminal, running the current
     * cmd binary when its own path can be resolved.
     * ------------------------------------------------------------------ */
    if (argi >= argc) {
        const char *self_path;
        char *cmd_argv[2];

        if (opt_b) {
            if (saved_cwd[0]) libcmd_chdir(saved_cwd);
            return 0;
        }

        if (!libcmd_has_display()) {
            if (saved_cwd[0]) libcmd_chdir(saved_cwd);
            fputs(cmd_gettext(MSG_ERR_START_NO_DISPLAY), stderr);
            return 1;
        }

        self_path = libcmd_get_self_path();
        cmd_argv[0] = (char *)self_path;  /* may be NULL */
        cmd_argv[1] = NULL;

        {
            int pid = start_in_terminal(cmd_argv);
            if (pid > 0) {
                if (opt_wait) {
                    libcmd_wait_pid(pid, &exit_info);
                    if (saved_cwd[0]) libcmd_chdir(saved_cwd);
                    return exit_info.exit_code;
                }
                if (saved_cwd[0]) libcmd_chdir(saved_cwd);
                return 0;
            }
        }
        if (saved_cwd[0]) libcmd_chdir(saved_cwd);
        return 0;
    }

    /* ------------------------------------------------------------------
     * Case 2: argument is an existing file or directory.
     *   Executable regular files are treated as command (Case 3).
     *   Directories and non-executable files are opened with the
     *   desktop default application (xdg-open / ShellExecute).
     * ------------------------------------------------------------------ */
    {
        libcmd_stat_t st;
        int i;

        if (libcmd_stat(argv[argi], &st, 1) == 0) {
            /* Executable regular file: fall through to Case 3 */
            if (libcmd_is_executable_file(argv[argi]))
                goto run_command;

            /* Directory or non-executable file: open it with the
             * platform's way of presenting filesystem objects.  Each
             * argument is opened on its own; /WAIT waits on the first
             * one (the launcher's process). */
            if (!libcmd_has_display()) {
                if (saved_cwd[0]) libcmd_chdir(saved_cwd);
                fputs(cmd_gettext(MSG_ERR_START_NO_DISPLAY), stderr);
                return 1;
            }

            for (i = argi; argv[i]; i++) {
                if (libcmd_shell_open(argv[i], opt_wait && i == argi,
                                      &exit_info) < 0) {
                    if (saved_cwd[0]) libcmd_chdir(saved_cwd);
                    fprintf(stderr, cmd_gettext(MSG_ERR_START_FAILED),
                            argv[i]);
                    return 1;
                }
            }

            if (opt_wait) {
                if (saved_cwd[0]) libcmd_chdir(saved_cwd);
                return exit_info.exit_code;
            }

            if (saved_cwd[0]) libcmd_chdir(saved_cwd);
            return 0;
        }
    }

    /* ------------------------------------------------------------------
     * Case 3: argument is a command -- run in a terminal window
     * ------------------------------------------------------------------ */
run_command:
    path_env = libcmd_getenv("PATH");
    /* cmd.exe's START also runs internal commands ("start dir",
     * "start echo hi"); those are not on disk, so only require the
     * PATH lookup for names that are not builtins. */
    {
        int found;

        if (cmd_find_builtin(argv[argi]) != NULL)
            found = 2;              /* internal command: START runs it */
        else
            found = (libcmd_find_exec(argv[argi], path_env,
                                      exec_path,
                                      sizeof(exec_path)) >= 0) ? 1 : 0;

        /* /B runs directly in this console: a missing program stays a
         * console error there (real cmd does the same for /B). */
        if (opt_b && found != 1) {
            if (saved_cwd[0]) libcmd_chdir(saved_cwd);
            fprintf(stderr, cmd_gettext(MSG_ERR_START_NOT_FOUND),
                    argv[argi]);
            return 1;
        }
        if (!opt_b && found == 0) {
#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)
            /* cmd.exe's START hands unresolved names to the shell and
             * lets Windows show its standard "Windows cannot find"
             * dialog.  ShellExecute reports failure only after the
             * user dismisses it. */
            {
                int sret;
                sret = libcmd_shell_open(argv[argi], opt_wait, &exit_info);
                if (saved_cwd[0]) libcmd_chdir(saved_cwd);
                return (sret == 0) ? (opt_wait ? exit_info.exit_code : 0)
                                   : 1;
            }
#else
            if (saved_cwd[0]) libcmd_chdir(saved_cwd);
            fprintf(stderr, cmd_gettext(MSG_ERR_START_NOT_FOUND),
                    argv[argi]);
            return 1;
#endif
        }
    }

    if (opt_b) {
        /* /B: run directly in the current console, no terminal window.
         * Pass the original arguments verbatim (backslashes were already
         * normalised by the dispatcher); no path rewriting is wanted for
         * direct execution.  The child inherits the console's output
         * streams so its output is visible like cmd /c, but stdin stays
         * detached (/dev/null) so it cannot fight the shell for input. */
        if (opt_wait) {
            libcmd_exec_sync(exec_path, argv + argi, libcmd_get_environ(),
                             -1, -1, -1, nice_level, &exit_info);
            if (saved_cwd[0]) libcmd_chdir(saved_cwd);
            return exit_info.exit_code;
        } else {
            int pid = libcmd_exec_async(exec_path, argv + argi,
                                        libcmd_get_environ(),
                                        -1,
                                        LIBCMD_STDOUT_FILENO,
                                        LIBCMD_STDERR_FILENO,
                                        nice_level);
            if (saved_cwd[0]) libcmd_chdir(saved_cwd);
            if (pid < 0) {
                fprintf(stderr, cmd_gettext(MSG_ERR_START_FAILED),
                        argv[argi]);
                return 1;
            }
            return 0;
        }
    }

    /* Normal case: run in a new terminal window */
    {
        int pid;

        if (!libcmd_has_display()) {
            if (saved_cwd[0]) libcmd_chdir(saved_cwd);
            fputs(cmd_gettext(MSG_ERR_START_NO_DISPLAY), stderr);
            return 1;
        }

        pid = start_in_terminal(argv + argi);
        if (pid < 0) {
            if (saved_cwd[0]) libcmd_chdir(saved_cwd);
            fprintf(stderr, cmd_gettext(MSG_ERR_START_FAILED), argv[argi]);
            return 1;
        }
        if (opt_wait) {
            libcmd_wait_pid(pid, &exit_info);
            if (saved_cwd[0]) libcmd_chdir(saved_cwd);
            return exit_info.exit_code;
        }
        if (saved_cwd[0]) libcmd_chdir(saved_cwd);
        return 0;
    }
}
