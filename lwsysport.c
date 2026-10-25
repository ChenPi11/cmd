#define _CRT_SECURE_NO_WARNINGS
/*
 * lwsysport.c - system portability shims (Windows implementation)
 *
 * Windows-specific implementations for portability functions.
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <windows.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * fnmatch (Windows implementation using simple pattern matching)
 * ---------------------------------------------------------------------- */

static int fnmatch_sub(const char *pattern, const char *string)
{
    const char *p = pattern;
    const char *s = string;

    while (*p) {
        if (*p == '*') {
            while (p[1] == '*')
                p++;
            if (p[1] == '\0')
                return 1;
            for (;; s++) {
                if (fnmatch_sub(p + 1, s) != 0)
                    return 1;
                if (*s == '\0')
                    return 0;
            }
        } else if (*p == '?') {
            if (*s == '\0')
                return 0;
            s++;
            p++;
        } else if (*p == '[') {
            unsigned char c = (unsigned char)*s;
            const char *q = p + 1;
            int negate = 0;
            int matched = 0;

            if (*q == '!' || *q == '^') {
                negate = 1;
                q++;
            }

            /* Lookahead: does the bracket expression ever close? */
            {
                const char *r = q;
                int first = 1;
                int closed = 0;

                while (*r) {
                    if (*r == ']' && !first) {
                        closed = 1;
                        break;
                    }
                    first = 0;
                    r++;
                }
                if (!closed) {
                    if (*s != '[')
                        return 0;
                    s++;
                    p++;
                    continue;
                }
            }

            if (*s == '\0')
                return 0;

            if (*q == ']') {
                if (c == ']')
                    matched = 1;
                q++;
            }
            while (*q && *q != ']') {
                unsigned char lo = (unsigned char)q[0];

                if (q[1] == '-' && q[2] != '\0' && q[2] != ']') {
                    unsigned char hi = (unsigned char)q[2];

                    if (c >= lo && c <= hi)
                        matched = 1;
                    q += 3;
                } else {
                    if (c == lo)
                        matched = 1;
                    q++;
                }
            }

            if (negate)
                matched = (matched ? 0 : 1);
            if (!matched)
                return 0;
            s++;
            while (*q && *q != ']')
                q++;
            if (*q == ']')
                q++;
            p = q;
        } else {
            if (*s != *p || *s == '\0')
                return 0;
            s++;
            p++;
        }
    }
    return (*s == '\0');
}

int libcmd_fnmatch(const char *pattern, const char *string)
{
    if (fnmatch_sub(pattern, string) != 0)
        return 0;
    return 1; /* FNM_NOMATCH equivalent */
}

/* -------------------------------------------------------------------------
 * System clock
 * ---------------------------------------------------------------------- */

int libcmd_set_system_time(time_t t)
{
    SYSTEMTIME st;
    struct tm *tm_info;

    tm_info = localtime(&t);
    if (tm_info == NULL)
        return -1;

    st.wYear = (WORD)(tm_info->tm_year + 1900);
    st.wMonth = (WORD)(tm_info->tm_mon + 1);
    st.wDay = (WORD)tm_info->tm_mday;
    st.wHour = (WORD)tm_info->tm_hour;
    st.wMinute = (WORD)tm_info->tm_min;
    st.wSecond = (WORD)tm_info->tm_sec;
    st.wMilliseconds = 0;

    return SetSystemTime(&st) ? 0 : -1;
}

/* -------------------------------------------------------------------------
 * Process priority
 * ---------------------------------------------------------------------- */

int libcmd_set_process_priority(int nice_level)
{
    HANDLE hProcess;
    int priority;

    if (nice_level == 0)
        return 0;

    hProcess = GetCurrentProcess();

    /* Map nice level to Windows priority class */
    if (nice_level < -5)
        priority = HIGH_PRIORITY_CLASS;
    else if (nice_level < 0)
        priority = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (nice_level == 0)
        priority = NORMAL_PRIORITY_CLASS;
    else if (nice_level <= 5)
        priority = BELOW_NORMAL_PRIORITY_CLASS;
    else
        priority = IDLE_PRIORITY_CLASS;

    return SetPriorityClass(hProcess, priority) ? 0 : -1;
}

/* -------------------------------------------------------------------------
 * Memory streams (Windows implementation using tmpfile)
 * ---------------------------------------------------------------------- */

static FILE *ms_stream;     /* active memstream (single-stream use) */
static char **ms_ptr;
static size_t *ms_sizeloc;

FILE *libcmd_open_memstream(char **ptr, size_t *sizeloc)
{
    FILE *f = tmpfile();

    if (f != NULL) {
        ms_stream = f;
        ms_ptr    = ptr;
        ms_sizeloc = sizeloc;
    }
    if (ptr)
        *ptr = NULL;
    if (sizeloc)
        *sizeloc = 0;
    return f;
}

int libcmd_memstream_close(FILE *stream)
{
    long sz;
    char *buf;

    if (stream == NULL)
        return -1;

    if (fflush(stream) != 0) {
        fclose(stream);
        return -1;
    }
    sz = ftell(stream);
    if (sz < 0) {
        fclose(stream);
        return -1;
    }
    rewind(stream);

    buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(stream);
        return -1;
    }
    if (sz > 0 && fread(buf, 1, (size_t)sz, stream) != (size_t)sz)
        buf[0] = '\0';
    buf[sz] = '\0';

    if (ms_ptr)
        *ms_ptr = buf;
    if (ms_sizeloc)
        *ms_sizeloc = (size_t)sz;

    (void)ms_stream;
    fclose(stream);
    return 0;
}

/* -------------------------------------------------------------------------
 * uname
 * ---------------------------------------------------------------------- */

/*
 * Approximates pteb's RTL_OSVERSIONINFOW; only the fields RtlGetVersion
 * fills in are used.  Defining it here avoids depending on winternl.h
 * (a DDK header not present in userland builds).
 */
typedef struct lw_osversioninfo {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    char  szCSDVersion[128];
} lw_osversioninfo_t;

typedef LONG (WINAPI *lw_rtl_get_version_t)(lw_osversioninfo_t *info);

int libcmd_uname(libcmd_utsname_t *buf)
{
    lw_osversioninfo_t vi;
    lw_rtl_get_version_t rtl_get_version;
    HMODULE ntdll;
    SYSTEM_INFO si;
    char hostname[LIBCMD_UTSNAME_LEN];
    DWORD host_len;

    memset(buf, 0, sizeof(*buf));
    strcpy(buf->sysname, "Windows_NT");

    /* GetVersionExA lies (reports 6.2 on Windows 8.1+ unless the exe
     * carries a compatibility manifest), so the version comes from
     * ntdll!RtlGetVersion instead of a deprecated userland API. */
    rtl_get_version = NULL;
    ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll != NULL)
        rtl_get_version = (lw_rtl_get_version_t)GetProcAddress(ntdll,
                                                              "RtlGetVersion");
    memset(&vi, 0, sizeof(vi));
    if (rtl_get_version != NULL)
    {
        vi.dwOSVersionInfoSize = (DWORD)sizeof(vi);
        if (rtl_get_version(&vi) == 0)
        {
            libcmd_sprintf_s(buf->release, sizeof(buf->release),
                             "%lu.%lu.%lu",
                             (unsigned long)vi.dwMajorVersion,
                             (unsigned long)vi.dwMinorVersion,
                             (unsigned long)vi.dwBuildNumber);
            libcmd_sprintf_s(buf->version, sizeof(buf->version), "%s",
                             vi.szCSDVersion);
        }
    }
    /* A failure leaves release empty; the caller's uname fallback
     * (see cmd.c: "Unknown") then behaves exactly like on Unix. */

    host_len = (DWORD)sizeof(hostname);
    if (GetComputerNameA(hostname, &host_len) && host_len > 0)
        libcmd_sprintf_s(buf->nodename, sizeof(buf->nodename), "%s",
                         hostname);

    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:
        strcpy(buf->machine, "AMD64");
        break;
    case PROCESSOR_ARCHITECTURE_ARM64:
        strcpy(buf->machine, "ARM64");
        break;
    case PROCESSOR_ARCHITECTURE_INTEL:
        strcpy(buf->machine, "x86");
        break;
    default:
        break;
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Signals
 * ---------------------------------------------------------------------- */

/* Virtual dispositions for SIGTSTP (slot 0) and SIGPIPE (slot 1) so
 * install/restore bookkeeping keeps working even though Windows can
 * never deliver them. */
static libcmd_sig_handler_t lw_virtual_handler[2];

int libcmd_sigaction(int sig,
                     const libcmd_sigaction_t *act,
                     libcmd_sigaction_t *oldact)
{
    void (*prev)(int);
    int slot;

    if (sig == LIBCMD_SIGINT)
    {
        prev = signal(SIGINT, act == NULL ? SIG_DFL : act->handler);
        if (prev == SIG_ERR)
            return -1;
        if (oldact != NULL)
            oldact->handler = (libcmd_sig_handler_t)prev;
        return 0;
    }

    /* SIGTSTP never fires (no job control on Windows) and closing a
     * pipe only makes writes fail with an error, so the POSIX bookkeeping
     * is reproduced in the virtual slots. */
    if (sig == LIBCMD_SIGTSTP)
        slot = 0;
    else if (sig == LIBCMD_SIGPIPE)
        slot = 1;
    else
        return -1;

    if (oldact != NULL)
        oldact->handler = lw_virtual_handler[slot];
    if (act != NULL)
        lw_virtual_handler[slot] = act->handler;
    return 0;
}

void libcmd_reset_child_signals(void)
{
    /* Only SIGINT exists on Windows; SIGTSTP/SIGPIPE are virtual. */
    signal(SIGINT, SIG_DFL);
}

/* -------------------------------------------------------------------------
 * Graphical session detection
 * ---------------------------------------------------------------------- */

int libcmd_has_display(void)
{
    /* START never needs a display server on Windows: every spawned
     * process gets its own console (and GUI processes their window
     * station), so graphics availability is not a caller concern. */
    return 1;
}

#endif
