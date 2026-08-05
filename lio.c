/*
 * lio.c - low-level I/O operations
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Map libcmd flags to POSIX open(2) flags
 * ---------------------------------------------------------------------- */

static int map_open_flags(int libcmd_flags)
{
    int flags = 0;
    int rw = libcmd_flags & 0x0003;

    switch (rw) {
    case LIBCMD_O_RDONLY: flags = O_RDONLY; break;
    case LIBCMD_O_WRONLY: flags = O_WRONLY; break;
    case LIBCMD_O_RDWR:   flags = O_RDWR;   break;
    default:              flags = O_RDONLY; break;
    }

    if (libcmd_flags & LIBCMD_O_CREAT)  flags |= O_CREAT;
    if (libcmd_flags & LIBCMD_O_TRUNC)  flags |= O_TRUNC;
    if (libcmd_flags & LIBCMD_O_APPEND) flags |= O_APPEND;
    if (libcmd_flags & LIBCMD_O_EXCL)   flags |= O_EXCL;

    return flags;
}

/* -------------------------------------------------------------------------
 * File descriptor operations
 * ---------------------------------------------------------------------- */

int libcmd_open(const char *path, int flags, unsigned int mode)
{
    return open(path, map_open_flags(flags), (mode_t)mode);
}

int libcmd_close(int fd)
{
    return close(fd);
}

int libcmd_dup(int fd)
{
    return dup(fd);
}

int libcmd_dup2(int fd, int new_fd)
{
    return dup2(fd, new_fd);
}

int libcmd_pipe(int fds[2])
{
    return pipe(fds);
}

long libcmd_read(int fd, void *buf, size_t count)
{
    long n = (long)read(fd, buf, count);
    return n;
}

long libcmd_write(int fd, const void *buf, size_t count)
{
    long n = (long)write(fd, buf, count);
    return n;
}

FILE *libcmd_fdopen(int fd, const char *mode)
{
    return fdopen(fd, mode);
}
