/*
 * lfs.c - filesystem operations
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef LIBCMD_SYSV
#include <fnmatch.h>
#endif

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void stat_to_libcmd(const struct stat *st, libcmd_stat_t *out)
{
    struct tm *tm_info;
    time_t t;

    out->is_regular = S_ISREG(st->st_mode)  ? LIBCMD_TRUE : LIBCMD_FALSE;
    out->is_dir     = S_ISDIR(st->st_mode)  ? LIBCMD_TRUE : LIBCMD_FALSE;
    out->is_link    = S_ISLNK(st->st_mode)  ? LIBCMD_TRUE : LIBCMD_FALSE;
    out->size       = st->st_size;
    out->mode       = (unsigned int)st->st_mode;

    t = st->st_mtime;
    tm_info = localtime(&t);
    if (tm_info) {
        out->mtime.year   = tm_info->tm_year + 1900;
        out->mtime.month  = tm_info->tm_mon  + 1;
        out->mtime.day    = tm_info->tm_mday;
        out->mtime.hour   = tm_info->tm_hour;
        out->mtime.minute = tm_info->tm_min;
        out->mtime.second = tm_info->tm_sec;
        out->mtime.ms     = 0;
        out->mtime.wday   = tm_info->tm_wday;
    }

    t = st->st_atime;
    tm_info = localtime(&t);
    if (tm_info) {
        out->atime.year   = tm_info->tm_year + 1900;
        out->atime.month  = tm_info->tm_mon  + 1;
        out->atime.day    = tm_info->tm_mday;
        out->atime.hour   = tm_info->tm_hour;
        out->atime.minute = tm_info->tm_min;
        out->atime.second = tm_info->tm_sec;
        out->atime.ms     = 0;
        out->atime.wday   = tm_info->tm_wday;
    }

    t = st->st_ctime;
    tm_info = localtime(&t);
    if (tm_info) {
        out->ctime.year   = tm_info->tm_year + 1900;
        out->ctime.month  = tm_info->tm_mon  + 1;
        out->ctime.day    = tm_info->tm_mday;
        out->ctime.hour   = tm_info->tm_hour;
        out->ctime.minute = tm_info->tm_min;
        out->ctime.second = tm_info->tm_sec;
        out->ctime.ms     = 0;
        out->ctime.wday   = tm_info->tm_wday;
    }
}

/* -------------------------------------------------------------------------
 * Basic filesystem operations
 * ---------------------------------------------------------------------- */

int libcmd_chdir(const char *path)
{
    return chdir(path);
}

char *libcmd_getcwd(char *buf, size_t size)
{
    if (buf == NULL) {
        /* Allocate a buffer large enough */
        size_t alloc_size = 4096;
        char *tmp = (char *)malloc(alloc_size);
        if (tmp == NULL)
            return NULL;
        while (getcwd(tmp, alloc_size) == NULL) {
            if (errno != ERANGE) {
                free(tmp);
                return NULL;
            }
            alloc_size *= 2;
            {
                char *p = (char *)realloc(tmp, alloc_size);
                if (p == NULL) {
                    free(tmp);
                    return NULL;
                }
                tmp = p;
            }
        }
        return tmp;
    }
    return getcwd(buf, size);
}

int libcmd_mkdir(const char *path, int make_parents)
{
    if (!make_parents) {
        return mkdir(path, 0755);
    }

    /* mkdir -p: create each component */
    {
        char *tmp = libcmd_strdup(path);
        char *p;
        int ret = 0;

        if (tmp == NULL)
            return -1;

        /* Skip leading slash */
        p = tmp + (tmp[0] == '/');

        for (; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                    ret = -1;
                    break;
                }
                *p = '/';
            }
        }

        if (ret == 0) {
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
                ret = -1;
        }

        free(tmp);
        return ret;
    }
}

int libcmd_rmdir(const char *path)
{
    return rmdir(path);
}

int libcmd_unlink(const char *path)
{
    return unlink(path);
}

int libcmd_chmod(const char *path, unsigned int mode)
{
    return chmod(path, (mode_t)mode);
}

int libcmd_rename(const char *old_path, const char *new_path)
{
    return rename(old_path, new_path);
}

int libcmd_symlink(const char *target, const char *link_path)
{
    return symlink(target, link_path);
}

int libcmd_link(const char *old_path, const char *new_path)
{
    return link(old_path, new_path);
}

int libcmd_stat(const char *path, libcmd_stat_t *st, int follow_links)
{
    struct stat s;
    int ret;

    if (follow_links)
        ret = stat(path, &s);
    else
        ret = lstat(path, &s);

    if (ret < 0)
        return -1;

    stat_to_libcmd(&s, st);
    return 0;
}

int libcmd_access(const char *path, int mode)
{
    int amode = 0;
    if (mode == 0) amode = F_OK;
    if (mode & 4) amode |= R_OK;
    if (mode & 2) amode |= W_OK;
    if (mode & 1) amode |= X_OK;
    return access(path, amode);
}

int libcmd_copy_file(const char *src, const char *dst, int overwrite)
{
    int src_fd, dst_fd, flags;
    char buf[8192];
    long nr;
    int ret = 0;

    src_fd = open(src, O_RDONLY);
    if (src_fd < 0)
        return -1;

    flags = O_WRONLY | O_CREAT | O_TRUNC;
    if (!overwrite)
        flags |= O_EXCL;

    dst_fd = open(dst, flags, 0666);
    if (dst_fd < 0) {
        close(src_fd);
        return -1;
    }

    while ((nr = read(src_fd, buf, sizeof(buf))) > 0) {
        char *p = buf;
        while (nr > 0) {
            long nw = write(dst_fd, p, (size_t)nr);
            if (nw < 0) {
                ret = -1;
                goto done;
            }
            p  += nw;
            nr -= nw;
        }
    }
    if (nr < 0)
        ret = -1;

done:
    close(src_fd);
    close(dst_fd);
    if (ret < 0)
        unlink(dst);
    return ret;
}

/* -------------------------------------------------------------------------
 * Directory iteration
 * ---------------------------------------------------------------------- */

libcmd_dir_t libcmd_opendir(const char *path)
{
    return (libcmd_dir_t)opendir(path);
}

int libcmd_readdir(libcmd_dir_t dir, libcmd_dirent_t *entry)
{
    struct dirent *de;
    struct stat st;
    struct tm *tm_info;
    time_t t;

    if (dir == NULL || entry == NULL)
        return -1;

    de = readdir((DIR *)dir);
    if (de == NULL)
        return -1;

    strncpy(entry->name, de->d_name, LIBCMD_NAME_MAX - 1);
    entry->name[LIBCMD_NAME_MAX - 1] = '\0';

    /* Use d_type when available, fall back to stat */
#ifdef DT_DIR
    if (de->d_type != DT_UNKNOWN) {
        entry->is_dir  = (de->d_type == DT_DIR)  ? LIBCMD_TRUE : LIBCMD_FALSE;
        entry->is_link = (de->d_type == DT_LNK)  ? LIBCMD_TRUE : LIBCMD_FALSE;
        entry->size    = 0;
        entry->mode    = 0;
        /* We still need mtime; do a stat */
    }
#endif

    if (stat(de->d_name, &st) == 0) {
        entry->is_dir  = S_ISDIR(st.st_mode) ? LIBCMD_TRUE : LIBCMD_FALSE;
        entry->is_link = S_ISLNK(st.st_mode) ? LIBCMD_TRUE : LIBCMD_FALSE;
        entry->size    = st.st_size;
        entry->mode    = (unsigned int)st.st_mode;
        entry->uid     = (unsigned int)st.st_uid;

        t = st.st_mtime;
        tm_info = localtime(&t);
        if (tm_info) {
            entry->mtime.year   = tm_info->tm_year + 1900;
            entry->mtime.month  = tm_info->tm_mon  + 1;
            entry->mtime.day    = tm_info->tm_mday;
            entry->mtime.hour   = tm_info->tm_hour;
            entry->mtime.minute = tm_info->tm_min;
            entry->mtime.second = tm_info->tm_sec;
            entry->mtime.ms     = 0;
            entry->mtime.wday   = tm_info->tm_wday;
        }

        t = st.st_atime;
        tm_info = localtime(&t);
        if (tm_info) {
            entry->atime.year   = tm_info->tm_year + 1900;
            entry->atime.month  = tm_info->tm_mon  + 1;
            entry->atime.day    = tm_info->tm_mday;
            entry->atime.hour   = tm_info->tm_hour;
            entry->atime.minute = tm_info->tm_min;
            entry->atime.second = tm_info->tm_sec;
            entry->atime.ms     = 0;
            entry->atime.wday   = tm_info->tm_wday;
        }

        t = st.st_ctime;
        tm_info = localtime(&t);
        if (tm_info) {
            entry->ctime.year   = tm_info->tm_year + 1900;
            entry->ctime.month  = tm_info->tm_mon  + 1;
            entry->ctime.day    = tm_info->tm_mday;
            entry->ctime.hour   = tm_info->tm_hour;
            entry->ctime.minute = tm_info->tm_min;
            entry->ctime.second = tm_info->tm_sec;
            entry->ctime.ms     = 0;
            entry->ctime.wday   = tm_info->tm_wday;
        }
    }

    return 0;
}

void libcmd_closedir(libcmd_dir_t dir)
{
    if (dir)
        closedir((DIR *)dir);
}

/* -------------------------------------------------------------------------
 * Glob
 * ---------------------------------------------------------------------- */

/*
 * Split a pattern into directory and filename parts, then scan the directory
 * and match each entry with fnmatch.
 */
int libcmd_glob(const char *pattern, libcmd_glob_result_t *result)
{
    char dir_part[4096];
    const char *file_part;
    const char *last_slash;
    DIR *d;
    struct dirent *de;
    size_t alloc;

    if (pattern == NULL || result == NULL)
        return -1;

    result->paths = NULL;
    result->count = 0;

    last_slash = strrchr(pattern, '/');
    if (last_slash == NULL) {
        dir_part[0] = '.';
        dir_part[1] = '\0';
        file_part = pattern;
    } else {
        size_t dlen = (size_t)(last_slash - pattern);
        if (dlen == 0) dlen = 1; /* root "/" */
        if (dlen >= sizeof(dir_part))
            return -1;
        memcpy(dir_part, pattern, dlen);
        dir_part[dlen] = '\0';
        file_part = last_slash + 1;
    }

    d = opendir(dir_part);
    if (d == NULL)
        return 0; /* no matches, not an error */

    alloc = 16;
    result->paths = (char **)malloc(alloc * sizeof(char *));
    if (result->paths == NULL) {
        closedir(d);
        return -1;
    }

    while ((de = readdir(d)) != NULL) {
        /* Skip . and .. unless pattern explicitly matches them */
        if (de->d_name[0] == '.') {
            if (file_part[0] != '.')
                continue;
        }

        if (libcmd_fnmatch(file_part, de->d_name) != 0)
            continue;

        /* Build full path */
        {
            char full[4096];
            size_t flen;

            if (dir_part[0] == '.' && dir_part[1] == '\0') {
                flen = (size_t)libcmd_sprintf_s(full, sizeof(full),
                                                "%s", de->d_name);
            } else {
                flen = (size_t)libcmd_sprintf_s(full, sizeof(full),
                                                "%s/%s", dir_part, de->d_name);
            }
            (void)flen;

            if (result->count >= alloc) {
                char **tmp;
                alloc *= 2;
                tmp = (char **)realloc(result->paths,
                                       alloc * sizeof(char *));
                if (tmp == NULL) {
                    closedir(d);
                    libcmd_glob_free(result);
                    return -1;
                }
                result->paths = tmp;
            }

            result->paths[result->count] = libcmd_strdup(full);
            if (result->paths[result->count] == NULL) {
                closedir(d);
                libcmd_glob_free(result);
                return -1;
            }
            result->count++;
        }
    }

    /* NULL-terminate the array */
    {
        char **tmp = (char **)realloc(result->paths,
                                      (result->count + 1) * sizeof(char *));
        if (tmp) {
            result->paths = tmp;
            result->paths[result->count] = NULL;
        }
    }

    closedir(d);
    return 0;
}

void libcmd_glob_free(libcmd_glob_result_t *result)
{
    size_t i;
    if (result == NULL)
        return;
    for (i = 0; i < result->count; i++)
        free(result->paths[i]);
    free(result->paths);
    result->paths = NULL;
    result->count = 0;
}

/* -------------------------------------------------------------------------
 * Volume information (best-effort on Unix)
 * ---------------------------------------------------------------------- */

int libcmd_get_volume_info(const char *path,
                           char *label_buf,
                           size_t label_size,
                           unsigned long *serial)
{
    /* Unix does not have volume labels in the same sense; return empty */
    (void)path;
    if (label_buf && label_size > 0)
        label_buf[0] = '\0';
    if (serial)
        *serial = 0;
    return 0;
}

int libcmd_get_disk_free(const char *path,
                         off_t *free_bytes,
                         off_t *total_bytes)
{
    struct statvfs sv;

    if (statvfs(path, &sv) < 0)
        return -1;

    if (free_bytes)
        *free_bytes = (off_t)sv.f_bavail * (off_t)sv.f_frsize;
    if (total_bytes)
        *total_bytes = (off_t)sv.f_blocks * (off_t)sv.f_frsize;
    return 0;
}
