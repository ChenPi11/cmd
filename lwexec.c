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

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int libcmd_exec_sync(const char *path,
                     char *const argv[],
                     char *const envp[],
                     int stdin_fd,
                     int stdout_fd,
                     int stderr_fd,
                     int nice_level,
                     libcmd_exit_info_t *exit_info)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd_line[32768];
    DWORD creation_flags = CREATE_NO_WINDOW;
    HANDLE hStdin = NULL, hStdout = NULL, hStderr = NULL;
    HANDLE hStdinRead = NULL, hStdoutWrite = NULL, hStderrWrite = NULL;
    DWORD wait_result;
    DWORD exit_code;
    int i;

    if (path == NULL)
        return -1;

    /* Build command line from argv */
    cmd_line[0] = '\0';
    if (argv != NULL) {
        for (i = 0; argv[i] != NULL; i++) {
            if (i > 0)
                strcat(cmd_line, " ");
            /* Quote argument if it contains spaces */
            if (strchr(argv[i], ' ') != NULL || strchr(argv[i], '\t') != NULL) {
                strcat(cmd_line, "\"");
                strcat(cmd_line, argv[i]);
                strcat(cmd_line, "\"");
            } else {
                strcat(cmd_line, argv[i]);
            }
        }
    }

    /* Setup stdio redirection if needed */
    if (stdin_fd >= 0 || stdout_fd >= 0 || stderr_fd >= 0) {
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        if (stdin_fd >= 0) {
            CreatePipe(&hStdinRead, &hStdin, &sa, 0);
            SetHandleInformation(hStdin, HANDLE_FLAG_INHERIT, 0);
        }
        if (stdout_fd >= 0) {
            CreatePipe(&hStdout, &hStdoutWrite, &sa, 0);
            SetHandleInformation(hStdout, HANDLE_FLAG_INHERIT, 0);
        }
        if (stderr_fd >= 0) {
            CreatePipe(&hStderr, &hStderrWrite, &sa, 0);
            SetHandleInformation(hStderr, HANDLE_FLAG_INHERIT, 0);
        }
    }

    /* Initialize structures */
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (stdin_fd >= 0) si.hStdInput = hStdinRead;
    if (stdout_fd >= 0) si.hStdOutput = hStdoutWrite;
    if (stderr_fd >= 0) si.hStdError = hStderrWrite;
    if (stdin_fd >= 0 || stdout_fd >= 0 || stderr_fd >= 0)
        si.dwFlags |= STARTF_USESTDHANDLES;

    ZeroMemory(&pi, sizeof(pi));

    /* Create process */
    if (!CreateProcessA(path, cmd_line, NULL, NULL, TRUE,
                        creation_flags, NULL, NULL, &si, &pi)) {
        if (hStdinRead) CloseHandle(hStdinRead);
        if (hStdin) CloseHandle(hStdin);
        if (hStdoutWrite) CloseHandle(hStdoutWrite);
        if (hStdout) CloseHandle(hStdout);
        if (hStderrWrite) CloseHandle(hStderrWrite);
        if (hStderr) CloseHandle(hStderr);
        return -1;
    }

    /* Close parent's ends of pipes */
    if (hStdinRead) CloseHandle(hStdinRead);
    if (hStdoutWrite) CloseHandle(hStdoutWrite);
    if (hStderrWrite) CloseHandle(hStderrWrite);

    /* Wait for process to complete */
    wait_result = WaitForSingleObject(pi.hProcess, INFINITE);
    if (wait_result == WAIT_FAILED) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (hStdin) CloseHandle(hStdin);
        if (hStdout) CloseHandle(hStdout);
        if (hStderr) CloseHandle(hStderr);
        return -1;
    }

    /* Get exit code */
    GetExitCodeProcess(pi.hProcess, &exit_code);
    fill_exit_info(exit_code, exit_info);

    /* Cleanup */
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (hStdin) CloseHandle(hStdin);
    if (hStdout) CloseHandle(hStdout);
    if (hStderr) CloseHandle(hStderr);

    return 0;
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
    DWORD creation_flags = CREATE_NEW_CONSOLE;
    int i;

    if (path == NULL)
        return -1;

    /* Build command line from argv */
    cmd_line[0] = '\0';
    if (argv != NULL) {
        for (i = 0; argv[i] != NULL; i++) {
            if (i > 0)
                strcat(cmd_line, " ");
            if (strchr(argv[i], ' ') != NULL || strchr(argv[i], '\t') != NULL) {
                strcat(cmd_line, "\"");
                strcat(cmd_line, argv[i]);
                strcat(cmd_line, "\"");
            } else {
                strcat(cmd_line, argv[i]);
            }
        }
    }

    /* Initialize structures */
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    /* Create process */
    if (!CreateProcessA(path, cmd_line, NULL, NULL, FALSE,
                        creation_flags, NULL, NULL, &si, &pi)) {
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
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmdline[32768];
    char comspec[1024];
    char *arg_str;
    int i;

    (void)envp;

    if (!GetEnvironmentVariableA("COMSPEC", comspec, (DWORD)sizeof(comspec)))
        return -1;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    /* No command: open a plain new console running the shell itself */
    if (argv == NULL || argv[0] == NULL || argv[0][0] == '\0')
    {
        if (!CreateProcessA(comspec, NULL, NULL, NULL, FALSE,
                            CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
            return -1;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)pi.dwProcessId;
    }

    /* Otherwise the command runs inside "COMSPEC /D /C <args>": the cmd
     * wrapper makes external executables, builtins and batch files all
     * work through one path.  Quoting matches the helpers above. */
    cmdline[0] = '\0';
    arg_str = cmdline;
    arg_str += libcmd_sprintf_s(arg_str, sizeof(cmdline), "\"%s\" /D /C", comspec);
    for (i = 0; argv[i] != NULL; i++)
    {
        if (strchr(argv[i], ' ') != NULL || strchr(argv[i], '\t') != NULL)
        {
            strcat(arg_str, " \"");
            strcat(arg_str, argv[i]);
            strcat(arg_str, "\"");
        }
        else
        {
            strcat(arg_str, " ");
            strcat(arg_str, argv[i]);
        }
        arg_str += strlen(arg_str);
    }

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
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

#endif
