#define _CRT_SECURE_NO_WARNINGS
/*
 * lwexec.c - process execution (Windows implementation)
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void fill_exit_info(DWORD exit_code, libcmd_exit_info_t *info)
{
    if (info == NULL)
        return;
    info->exited    = 1;
    info->exit_code = (int)exit_code;
    info->signaled  = 0;
    info->signal    = 0;
}

/* Resolve a child's stdio slot: fd >= 0 is an existing CRT file
 * descriptor in this process (POSIX dup2 semantics -- a file, a pipe
 * end or plain stdio), so the child writes the same underlying OS
 * handle; fd < 0 forwards this process's own standard handle. */
static HANDLE std_handle_for(int fd, DWORD std_slot)
{
    HANDLE h;

    if (fd >= 0)
        h = (HANDLE)_get_osfhandle(fd);
    else
        h = GetStdHandle(std_slot);

    /* STARTF_USESTDHANDLES hands the raw handle values to the child;
     * a value without the inherit bit reaches the child as a dead
     * slot and the child's output is simply lost.  Console
     * screen-buffer handles in particular are not inheritable by
     * default.  Force the flag on every source (errors are harmless:
     * some pseudo-handles reject SetHandleInformation). */
    if (h != NULL && h != INVALID_HANDLE_VALUE)
        SetHandleInformation(h, HANDLE_FLAG_INHERIT,
                             HANDLE_FLAG_INHERIT);
    return h;
}

/* Append one command character, tracking backslash runs. */
static void cmd_append_char(char *dst, char c)
{
    size_t n = strlen(dst);
    dst[n] = c;
    dst[n + 1] = '\0';
}

/* Append src as one quoted command token using the CRT conventions:
 * embedded quotes become \" (with every backslash before them
 * doubled), and a trailing backslash run is doubled as well so it
 * cannot escape the closing quote.  Replaces the old naive
 * quote-and-paste which mis-parsed arguments with quotes in them
 * (self /C pipeline replays lost the pattern quotes). */
static void cmd_append_quoted_arg(char *dst, const char *src)
{
    const char *p;
    int bs_since = 0;

    strcat(dst, "\"");
    for (p = src; *p != '\0'; p++) {
        if (*p == '\\') {
            bs_since++;
            cmd_append_char(dst, '\\');
            continue;
        }
        if (*p == '"') {
            while (bs_since-- > 0)
                cmd_append_char(dst, '\\');
            cmd_append_char(dst, '\\');
            cmd_append_char(dst, '"');
            bs_since = 0;
            continue;
        }
        bs_since = 0;
        cmd_append_char(dst, *p);
    }
    while (bs_since-- > 0)
        cmd_append_char(dst, '\\');
    strcat(dst, "\"");
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/* Map the START scheduling level to a Windows priority class.  The
 * levels are the plain nice() numbers builtin_start assigns
 * (/LOW 19, /BELOWNORMAL 5, /NORMAL 0, /ABOVENORMAL -5, /HIGH -10,
 * /REALTIME -20); NORMAL maps to no extra creation flag. */
static DWORD win_priority_class(int nice_level)
{
    if (nice_level <= -20)
        return REALTIME_PRIORITY_CLASS;
    if (nice_level <= -10)
        return HIGH_PRIORITY_CLASS;
    if (nice_level <= -5)
        return ABOVE_NORMAL_PRIORITY_CLASS;
    if (nice_level >= 19)
        return IDLE_PRIORITY_CLASS;
    if (nice_level >= 5)
        return BELOW_NORMAL_PRIORITY_CLASS;
    return 0;   /* NORMAL_PRIORITY_CLASS (the default) */
}

/* Append TOKEN to DST as one loader-form token, wrapped in quotes when
 * it contains whitespace (a plain wrap, used for resolved paths and
 * first tokens, which carry no quotes of their own). */
static void append_path_token(char *dst, const char *token)
{
    if (strchr(token, ' ') != NULL || strchr(token, '\t') != NULL) {
        strcat(dst, "\"");
        strcat(dst, token);
        strcat(dst, "\"");
    } else {
        strcat(dst, token);
    }
}

/* Spawn CMD_LINE in the loader form, wait for its completion and fill
 * EXIT_INFO.  The child's stdio slots follow the dup2 model: fd >= 0
 * pins the child's stream onto the OS handle behind that CRT fd, fd < 0
 * forwards this process's own standard handle.  CREATION_FLAGS must
 * already carry the scheduling class and CREATE_NO_WINDOW when the
 * child is redirected.  Returns 0, or -1 when the spawn or the wait
 * failed. */
static int spawn_and_wait(const char *cmd_line,
                          DWORD creation_flags,
                          int stdin_fd,
                          int stdout_fd,
                          int stderr_fd,
                          libcmd_exit_info_t *exit_info)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    int redirected;
    DWORD wait_result;
    DWORD exit_code;

    redirected = (stdin_fd >= 0 || stdout_fd >= 0 || stderr_fd >= 0);

    /* fd >= 0 means "put the child's stream on the OS handle behind
     * that CRT fd (POSIX dup2 semantics)" -- files and pipe ends
     * opened/dup2'd by the caller keep working without any in-between
     * pumping. */
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (redirected) {
        si.hStdInput  = std_handle_for(stdin_fd,  STD_INPUT_HANDLE);
        si.hStdOutput = std_handle_for(stdout_fd, STD_OUTPUT_HANDLE);
        si.hStdError  = std_handle_for(stderr_fd, STD_ERROR_HANDLE);
        si.dwFlags |= STARTF_USESTDHANDLES;
    }

    ZeroMemory(&pi, sizeof(pi));

    /* Create process: the loader form resolves the first token of the
     * command line; handles are inherited so the stdio mapping above
     * reaches the child. */
    if (!CreateProcessA(NULL, (char *)cmd_line, NULL, NULL, TRUE,
                        creation_flags, NULL, NULL, &si, &pi)) {
        return -1;
    }

    /* Wait for process to complete */
    wait_result = WaitForSingleObject(pi.hProcess, INFINITE);
    if (wait_result == WAIT_FAILED) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return -1;
    }

    /* Get exit code */
    GetExitCodeProcess(pi.hProcess, &exit_code);
    fill_exit_info(exit_code, exit_info);

    /* Cleanup */
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}

int libcmd_exec_sync(const char *path,
                     char *const argv[],
                     char *const envp[],
                     int stdin_fd,
                     int stdout_fd,
                     int stderr_fd,
                     int nice_level,
                     libcmd_exit_info_t *exit_info)
{
    char cmd_line[32768];
    DWORD creation_flags;
    int redirected;
    int i;

    /* envp is ignored: the child inherits this process's environment. */
    (void)envp;

    if (path == NULL)
        return -1;

    redirected = (stdin_fd >= 0 || stdout_fd >= 0 || stderr_fd >= 0);

    /* While the stdio streams are redirected the child stays unseen;
     * otherwise it shares this console so that its output is visible
     * where the shell's own output goes. */
    creation_flags = redirected ? CREATE_NO_WINDOW : 0;
    creation_flags |= win_priority_class(nice_level);

    /* Build the command line in the loader form (the way real cmd.exe
     * spawns): lpApplicationName stays NULL and the first token is the
     * resolved full path, argv[1..] follow with quoting for whitespace.
     * The first token must be written without a leading space. */
    cmd_line[0] = '\0';
    append_path_token(cmd_line, path);
    if (argv != NULL) {
        for (i = 1; argv[i] != NULL; i++) {
            strcat(cmd_line, " ");
            /* Quote any argument with spaces or embedded quotes */
            if (strchr(argv[i], ' ') != NULL ||
                strchr(argv[i], '\t') != NULL ||
                strchr(argv[i], '"') != NULL)
                cmd_append_quoted_arg(cmd_line, argv[i]);
            else
                strcat(cmd_line, argv[i]);
        }
    }

    return spawn_and_wait(cmd_line, creation_flags,
                          stdin_fd, stdout_fd, stderr_fd, exit_info);
}

/* libcmd_exec_sync_line - run one resolved command line synchronously,
 * real cmd.exe style: the child's command line is PATH followed by
 * TAIL verbatim -- the statement text minus its command word.  Quoted
 * arguments, wildcard patterns and blank runs reach the child exactly
 * as written (the parent applies redirections by dup2 and passes the
 * open fd here, or -1 to forward its own handle). */
int libcmd_exec_sync_line(const char *path,
                          const char *tail,
                          int stdin_fd,
                          int stdout_fd,
                          int stderr_fd,
                          libcmd_exit_info_t *exit_info)
{
    char cmd_line[32768];
    int redirected;

    if (path == NULL)
        return -1;

    redirected = (stdin_fd >= 0 || stdout_fd >= 0 || stderr_fd >= 0);

    /* While the stdio streams are redirected the child stays unseen;
     * otherwise it shares this console so that its output is visible
     * where the shell's own output goes. */
    cmd_line[0] = '\0';
    append_path_token(cmd_line, path);
    if (tail != NULL && tail[0] != '\0') {
        strcat(cmd_line, " ");
        strcat(cmd_line, tail);
    }

    return spawn_and_wait(cmd_line, redirected ? CREATE_NO_WINDOW : 0,
                          stdin_fd, stdout_fd, stderr_fd, exit_info);
}

int libcmd_exec_async(const char *path,
                      char *const argv[],
                      char *const envp[],
                      int stdin_fd,
                      int stdout_fd,
                      int stderr_fd,
                      int nice_level)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd_line[32768];
    DWORD creation_flags;
    int i;

    /* envp is ignored: the child inherits this process's environment. */
    (void)envp;

    if (path == NULL)
        return -1;

    /* START /B shares this console with the child (real cmd.exe's
     * behaviour); no new console and no hidden window.  The priority
     * class from /LOW.. /REALTIME rides in the same flag set. */
    creation_flags = win_priority_class(nice_level);

    /* Build command line from argv */
    cmd_line[0] = '\0';
    if (argv != NULL) {
        for (i = 0; argv[i] != NULL; i++) {
            if (i > 0)
                strcat(cmd_line, " ");
            if (strchr(argv[i], ' ') != NULL ||
                strchr(argv[i], '\t') != NULL ||
                strchr(argv[i], '"') != NULL)
                cmd_append_quoted_arg(cmd_line, argv[i]);
            else {
                strcat(cmd_line, argv[i]);
            }
        }
    }

    /* Initialize structures */
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (stdin_fd >= 0 || stdout_fd >= 0 || stderr_fd >= 0) {
        si.hStdInput  = std_handle_for(stdin_fd,  STD_INPUT_HANDLE);
        si.hStdOutput = std_handle_for(stdout_fd, STD_OUTPUT_HANDLE);
        si.hStdError  = std_handle_for(stderr_fd, STD_ERROR_HANDLE);
        si.dwFlags |= STARTF_USESTDHANDLES;
    }
    ZeroMemory(&pi, sizeof(pi));

    /* Create process */
    if (!CreateProcessA(path, cmd_line, NULL, NULL,
                        (stdin_fd >= 0 || stdout_fd >= 0 || stderr_fd >= 0)
                            ? TRUE : FALSE,
                        creation_flags, NULL, NULL, &si, &pi)) {
        return -1;
    }

    /* Close handles - process runs independently */
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (int)pi.dwProcessId;
}

/* libcmd_exec_async_line - spawn PATH with TAIL verbatim (the replayed
 * statement text) and return without waiting.  Real cmd.exe replays
 * each pipeline stage as `cmd /C <stage text>` with the stage's
 * original quoting inlined UNESCAPED on the loader command line; the
 * child reads its /C string back from the raw text, so no CRT escape
 * layer may sit in between (crt argv rules would turn "find \"x\""
 * into text the shell cannot re-quote faithfully). */
int libcmd_exec_async_line(const char *path,
                           const char *tail,
                           int stdin_fd,
                           int stdout_fd,
                           int stderr_fd)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd_line[32768];

    if (path == NULL)
        return -1;

    cmd_line[0] = '\0';
    append_path_token(cmd_line, path);
    if (tail != NULL && tail[0] != '\0') {
        strcat(cmd_line, " ");
        strcat(cmd_line, tail);
    }

    /* Initialize structures: fd >= 0 pins the child's stream onto the
     * OS handle behind that CRT fd (dup2 model), fd < 0 forwards this
     * process's own handle. */
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (stdin_fd >= 0 || stdout_fd >= 0 || stderr_fd >= 0) {
        si.hStdInput  = std_handle_for(stdin_fd,  STD_INPUT_HANDLE);
        si.hStdOutput = std_handle_for(stdout_fd, STD_OUTPUT_HANDLE);
        si.hStdError  = std_handle_for(stderr_fd, STD_ERROR_HANDLE);
        si.dwFlags |= STARTF_USESTDHANDLES;
    }
    ZeroMemory(&pi, sizeof(pi));

    /* Create process in the loader form: lpApplicationName stays NULL
     * and the first token of the command line is the resolved path. */
    if (!CreateProcessA(NULL, cmd_line, NULL, NULL,
                        (stdin_fd >= 0 || stdout_fd >= 0 || stderr_fd >= 0)
                            ? TRUE : FALSE,
                        0, NULL, NULL, &si, &pi)) {
        return -1;
    }

    /* Close handles - process runs independently */
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (int)pi.dwProcessId;
}

int libcmd_wait_pid(int pid, libcmd_exit_info_t *exit_info)
{
    HANDLE hProcess;
    DWORD wait_result;
    DWORD exit_code;

    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, (DWORD)pid);
    if (hProcess == NULL)
        return -1;

    wait_result = WaitForSingleObject(hProcess, INFINITE);
    if (wait_result == WAIT_FAILED) {
        CloseHandle(hProcess);
        return -1;
    }

    GetExitCodeProcess(hProcess, &exit_code);
    fill_exit_info(exit_code, exit_info);

    CloseHandle(hProcess);
    return 0;
}

/* Probe one directory (NULL or empty: the plain name) for an
 * executable version of NAME.  A name that already carries an
 * extension is tried as given; an extensionless name gets every
 * extension in EXTLIST (the PATHEXT list) appended, the way cmd.exe
 * and CreateProcess resolve commands.
 * Returns 0 with the resolved path in OUT, or -1. */
static int probe_exec_in_dir(const char *dir, const char *name,
                             const char *extlist,
                             char *out, size_t out_size)
{
    char candidate[4096];
    size_t dlen;
    size_t nlen;
    DWORD attrs;

    dlen = 0;
    if (dir != NULL && dir[0] != '\0') {
        dlen = strlen(dir);
        if (dlen >= sizeof(candidate) - 1)
            return -1;
        memcpy(candidate, dir, dlen);
        if (candidate[dlen - 1] != '\\' && candidate[dlen - 1] != '/')
            candidate[dlen++] = '\\';
    }
    nlen = strlen(name);
    if (dlen + nlen + 5 >= sizeof(candidate))
        return -1;
    memcpy(candidate + dlen, name, nlen + 1);

    /* The exact name first (covers names such as "mytool.exe"). */
    attrs = GetFileAttributesA(candidate);
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return libcmd_sprintf_s(out, out_size, "%s", candidate) >= 0
                   ? 0 : -1;

    /* Extensionless names try the PATHEXT extensions. */
    if (strchr(name, '.') != NULL)
        return -1;

    while (extlist != NULL && *extlist != '\0') {
        const char *ext, *eend;
        size_t elen;

        ext = extlist;
        eend = strchr(ext, ';');
        if (eend == NULL)
            eend = ext + strlen(ext);
        elen = (size_t)(eend - ext);
        extlist = (*eend == ';') ? eend + 1 : NULL;
        if (elen == 0)
            continue;
        if (dlen + nlen + elen + 1 >= sizeof(candidate))
            continue;
        memcpy(candidate + dlen + nlen, ext, elen);
        candidate[dlen + nlen + elen] = '\0';

        attrs = GetFileAttributesA(candidate);
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
            return libcmd_sprintf_s(out, out_size, "%s", candidate) >= 0
                       ? 0 : -1;
    }
    return -1;
}

/* Resolve a name that contains a directory separator: try it exactly,
 * plus ".exe" when it has no extension (CreateProcess does the same). */
static int probe_separated_name(const char *name, char *out, size_t out_size)
{
    DWORD attrs;
    char withexe[4096];

    attrs = GetFileAttributesA(name);
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return libcmd_sprintf_s(out, out_size, "%s", name) >= 0 ? 0 : -1;

    if (strchr(name, '.') != NULL)
        return -1;
    if (strlen(name) + 5 >= sizeof(withexe))
        return -1;
    strcpy(withexe, name);
    strcat(withexe, ".exe");
    attrs = GetFileAttributesA(withexe);
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return libcmd_sprintf_s(out, out_size, "%s", withexe) >= 0 ? 0 : -1;
    return -1;
}

int libcmd_find_exec(const char *name,
                     const char *path_env,
                     char *out,
                     size_t out_size)
{
    const char *extlist;
    const char *start, *end;
    char cwd[4096];
    char dir[4096];
    size_t len;

    if (name == NULL || out == NULL || out_size == 0)
        return -1;

    /* Windows searches with the PATHEXT extensions; fall back to the
     * system default list when the variable is unset or empty. */
    extlist = libcmd_getenv("PATHEXT");
    if (extlist == NULL || extlist[0] == '\0')
        extlist = ".COM;.EXE;.BAT;.CMD";

    /* Absolute or relative path with directory separator */
    if (strchr(name, '\\') != NULL || strchr(name, '/') != NULL)
        return probe_separated_name(name, out, out_size);

    if (path_env == NULL)
        path_env = "C:\\Windows\\System32;C:\\Windows;C:\\Program Files\\cmd\\bin";

    /* Check current directory first */
    if (libcmd_getcwd(cwd, sizeof(cwd)) != NULL &&
        probe_exec_in_dir(cwd, name, extlist, out, out_size) == 0)
        return 0;

    /* Search PATH */
    start = path_env;
    while (start != NULL && *start != '\0') {
        end = strchr(start, ';');
        if (end == NULL)
            end = start + strlen(start);

        if (end > start) {
            len = (size_t)(end - start);
            if (len < sizeof(dir)) {
                memcpy(dir, start, len);
                dir[len] = '\0';
                if (probe_exec_in_dir(dir, name, extlist,
                                      out, out_size) == 0)
                    return 0;
            }
        }

        start = (*end == ';') ? end + 1 : NULL;
    }

    return -1;
}

int libcmd_fork(void)
{
    /* Windows doesn't have fork. Return -1 to indicate not supported. */
    return -1;
}

/* -------------------------------------------------------------------------
 * New-console spawn (START "command")
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * New-console spawn (START "command")
 * ---------------------------------------------------------------------- */

int libcmd_exec_console(char *const argv[], char *const envp[])
{
    return libcmd_exec_console_ex(argv, envp, LIBCMD_SHOW_NORMAL, 0);
}

int libcmd_exec_console_ex(char *const argv[], char *const envp[],
                           int show_cmd, int nice_level)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmdline[32768];
    char comspec[1024];
    char *arg_str;
    DWORD creation_flags;
    int direct_spawn;
    int i;

    (void)envp;

    if (!GetEnvironmentVariableA("COMSPEC", comspec, (DWORD)sizeof(comspec)))
        return -1;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    /* Requested window state (START /MIN and /MAX) for the new
     * console.  Values arrive as Win32 SW_* constants. */
    if (show_cmd != LIBCMD_SHOW_NORMAL) {
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = (WORD)show_cmd;
    }

    creation_flags = CREATE_NEW_CONSOLE | win_priority_class(nice_level);

    /* No command: open a plain new console running the shell itself */
    if (argv == NULL || argv[0] == NULL || argv[0][0] == '\0')
    {
        if (!CreateProcessA(comspec, NULL, NULL, NULL, FALSE,
                            creation_flags, NULL, NULL, &si, &pi))
            return -1;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)pi.dwProcessId;
    }

    /* An argv[0] carrying a directory separator is a resolved disk
     * path: spawn the program itself, exactly like real cmd.exe's
     * START does for external commands.  The window state and the
     * priority then apply to the program, and GUI programs get no
     * host cmd console.  Anything else (builtin names, batch and
     * script files) is wrapped in "COMSPEC /D /C" so every form
     * still runs through one path. */
    direct_spawn = (strchr(argv[0], '\\') != NULL ||
                    strchr(argv[0], '/') != NULL);

    cmdline[0] = '\0';
    arg_str = cmdline;
    if (!direct_spawn)
        arg_str += libcmd_sprintf_s(arg_str, sizeof(cmdline), "\"%s\" /D /C", comspec);
    for (i = 0; argv[i] != NULL; i++)
    {
        /* With a NULL application name CreateProcess rejects a
         * command line with leading blank, so the first direct
         * token is written without a space. */
        if (i > 0 || !direct_spawn)
        {
            strcat(arg_str, " ");
            if (strchr(argv[i], ' ') != NULL || strchr(argv[i], '\t') != NULL)
            {
                strcat(arg_str, "\"");
                strcat(arg_str, argv[i]);
                strcat(arg_str, "\"");
            }
            else
            {
                strcat(arg_str, argv[i]);
            }
        }
        else
        {
            if (strchr(argv[i], ' ') != NULL || strchr(argv[i], '\t') != NULL)
                libcmd_sprintf_s(arg_str,
                                 sizeof(cmdline) - (size_t)(arg_str - cmdline),
                                 "\"%s\"", argv[i]);
            else
                libcmd_sprintf_s(arg_str,
                                 sizeof(cmdline) - (size_t)(arg_str - cmdline),
                                 "%s", argv[i]);
        }
        arg_str += strlen(arg_str);
    }

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        creation_flags, NULL, NULL, &si, &pi))
        return -1;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)pi.dwProcessId;
}

void libcmd_exit(int status)
{
    ExitProcess((UINT)status);
}

FILE *libcmd_popen(const char *cmd, const char *mode)
{
    return _popen(cmd, mode);
}

int libcmd_pclose(FILE *stream)
{
    if (stream == NULL)
        return -1;
    return _pclose(stream);
}

#else
int lwexec_c_dummy;
#endif
