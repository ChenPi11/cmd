#define _CRT_SECURE_NO_WARNINGS
/*
 * lwio.c - low-level I/O operations (Windows implementation)
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

/* -------------------------------------------------------------------------
 * Map libcmd flags to Windows open flags
 * ---------------------------------------------------------------------- */

static int map_open_flags(int libcmd_flags)
{
    int flags = _O_BINARY;
    int rw = libcmd_flags & 0x0003;

    switch (rw) {
    case LIBCMD_O_RDONLY: flags |= _O_RDONLY; break;
    case LIBCMD_O_WRONLY: flags |= _O_WRONLY; break;
    case LIBCMD_O_RDWR:   flags |= _O_RDWR;   break;
    default:              flags |= _O_RDONLY; break;
    }

    if (libcmd_flags & LIBCMD_O_CREAT)  flags |= _O_CREAT;
    if (libcmd_flags & LIBCMD_O_TRUNC)  flags |= _O_TRUNC;
    if (libcmd_flags & LIBCMD_O_APPEND) flags |= _O_APPEND;
    if (libcmd_flags & LIBCMD_O_EXCL)   flags |= _O_EXCL;

    return flags;
}

/* -------------------------------------------------------------------------
 * File descriptor operations
 * ---------------------------------------------------------------------- */

int libcmd_open(const char *path, int flags, unsigned int mode)
{
    return _open(path, map_open_flags(flags), mode);
}

int libcmd_close(int fd)
{
    return _close(fd);
}

int libcmd_dup(int fd)
{
    return _dup(fd);
}

int libcmd_dup2(int fd, int new_fd)
{
    return _dup2(fd, new_fd);
}

int libcmd_pipe(int fds[2])
{
    return _pipe(fds, 65536, _O_BINARY);
}

long libcmd_read(int fd, void *buf, size_t count)
{
    long n = (long)_read(fd, buf, (unsigned int)count);
    return n;
}

long libcmd_write(int fd, const void *buf, size_t count)
{
    long n = (long)_write(fd, buf, (unsigned int)count);
    return n;
}

FILE *libcmd_fdopen(int fd, const char *mode)
{
    return _fdopen(fd, mode);
}

#endif
