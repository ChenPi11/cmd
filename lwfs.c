#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#define _CRT_SECURE_NO_WARNINGS
/*
 * lwfs.c - filesystem operations (Windows implementation)
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <windows.h>
#include <aclapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <direct.h>
#include <io.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void filetime_to_libcmd(const FILETIME *ft, libcmd_time_t *out)
{
    SYSTEMTIME st;
    FILETIME local_ft;

    FileTimeToLocalFileTime(ft, &local_ft);
    FileTimeToSystemTime(&local_ft, &st);

    out->year   = st.wYear;
    out->month  = st.wMonth;
    out->day    = st.wDay;
    out->hour   = st.wHour;
    out->minute = st.wMinute;
    out->second = st.wSecond;
    out->ms     = st.wMilliseconds;
    out->wday   = st.wDayOfWeek;
}

static void finddata_to_libcmd(const WIN32_FIND_DATAA *fd,
                               const char *full_path,
                               libcmd_dirent_t *out)
{
    ULONGLONG size;

    libcmd_sprintf_s(out->name, sizeof(out->name), "%s", fd->cFileName);

    out->is_dir = (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
                  LIBCMD_TRUE : LIBCMD_FALSE;
    out->is_link = (fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ?
                   LIBCMD_TRUE : LIBCMD_FALSE;

    size = ((ULONGLONG)fd->nFileSizeHigh << 32) | fd->nFileSizeLow;
    out->size = (libcmd_filesize_t)size;

    filetime_to_libcmd(&fd->ftLastWriteTime, &out->mtime);
    filetime_to_libcmd(&fd->ftLastAccessTime, &out->atime);
    filetime_to_libcmd(&fd->ftCreationTime, &out->ctime);

    out->uid = 0;
    out->mode = 0;
}

/* -------------------------------------------------------------------------
 * Basic filesystem operations
 * ---------------------------------------------------------------------- */

int libcmd_chdir(const char *path)
{
    return _chdir(path);
}

char *libcmd_getcwd(char *buf, size_t size)
{
    if (buf == NULL) {
        size_t alloc_size = 4096;
        char *tmp = (char *)malloc(alloc_size);
        if (tmp == NULL)
            return NULL;
        if (_getcwd(tmp, alloc_size) == NULL) {
            free(tmp);
            return NULL;
        }
        return tmp;
    }
    return _getcwd(buf, (int)size);
}

int libcmd_mkdir(const char *path, int make_parents)
{
    if (make_parents) {
        /* Create parent directories if needed */
        char *parent;
        const char *last_sep;

        last_sep = strrchr(path, '\\');
        if (last_sep == NULL)
            last_sep = strrchr(path, '/');

        if (last_sep != NULL && last_sep > path) {
            size_t len = (size_t)(last_sep - path);
            parent = (char *)malloc(len + 1);
            if (parent != NULL) {
                memcpy(parent, path, len);
                parent[len] = '\0';
                libcmd_mkdir(parent, 1);
                free(parent);
            }
        }
    }
    return _mkdir(path);
}

int libcmd_rmdir(const char *path)
{
    return _rmdir(path);
}

int libcmd_unlink(const char *path)
{
    return _unlink(path);
}

int libcmd_rename(const char *old_path, const char *new_path)
{
    return rename(old_path, new_path);
}

int libcmd_access(const char *path, int mode)
{
    return _access(path, mode);
}

/* -------------------------------------------------------------------------
 * Copies, links, attribute changes, disk space
 * ---------------------------------------------------------------------- */

int libcmd_chmod(const char *path, unsigned int mode)
{
    DWORD attrs;

    if (path == NULL)
        return -1;
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return -1;
    /* Windows has no permission bits; POSIX write permission maps to
     * the read-only attribute (0644 clears it, bdel's "del /f" path). */
    if (mode & 0x92)                    /* S_IWUSR | S_IWGRP | S_IWOTH */
        attrs &= ~FILE_ATTRIBUTE_READONLY;
    else
        attrs |= FILE_ATTRIBUTE_READONLY;
    return SetFileAttributesA(path, attrs) ? 0 : -1;
}

int libcmd_symlink(const char *target, const char *link_path)
{
    libcmd_stat_t st;
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;

    if (target == NULL || link_path == NULL)
        return -1;
    /* Links to directories must carry the directory flag. */
    if (libcmd_stat(target, &st, 1) == 0 && st.is_dir)
        flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    return CreateSymbolicLinkA(link_path, target, flags) ? 0 : -1;
}

int libcmd_link(const char *old_path, const char *new_path)
{
    if (old_path == NULL || new_path == NULL)
        return -1;
    return CreateHardLinkA(new_path, old_path, NULL) ? 0 : -1;
}

int libcmd_copy_file(const char *src, const char *dst, int overwrite)
{
    if (src == NULL || dst == NULL)
        return -1;
    /* bFailIfExists mirrors the O_EXCL side of the Unix fallback. */
    return CopyFileA(src, dst, overwrite ? FALSE : TRUE) ? 0 : -1;
}

int libcmd_get_disk_free(const char *path,
                         libcmd_filesize_t *free_bytes,
                         libcmd_filesize_t *total_bytes)
{
    ULARGE_INTEGER free_q;
    ULARGE_INTEGER total_q;

    if (path == NULL)
        return -1;
    if (!GetDiskFreeSpaceExA(path, &free_q, &total_q, NULL))
        return -1;
    if (free_bytes)
        *free_bytes = (libcmd_filesize_t)free_q.QuadPart;
    if (total_bytes)
        *total_bytes = (libcmd_filesize_t)total_q.QuadPart;
    return 0;
}

/* -------------------------------------------------------------------------
 * Entry owner lookup ("dir /q")
 * ---------------------------------------------------------------------- */

int libcmd_get_entry_owner(const char *path, char *name_buf, size_t name_size)
{
    PSID owner_sid = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    char name[LIBCMD_NAME_MAX];
    char domain[LIBCMD_NAME_MAX];
    DWORD name_len  = (DWORD)sizeof(name);
    DWORD domain_len = (DWORD)sizeof(domain);
    SID_NAME_USE sid_use;
    int ret = -1;

    if (path == NULL || name_buf == NULL || name_size == 0)
        return -1;
    name_buf[0] = '\0';

    if (GetNamedSecurityInfoA(path, SE_FILE_OBJECT,
                              OWNER_SECURITY_INFORMATION,
                              &owner_sid, NULL, NULL, NULL,
                              &sd) != ERROR_SUCCESS)
        return -1;

    if (owner_sid != NULL &&
        LookupAccountSidA(NULL, owner_sid,
                          name, &name_len,
                          domain, &domain_len,
                          &sid_use))
    {
        /* "DOMAIN\account", like cmd.exe shows for e.g.
         * "NT AUTHORITY\SYSTEM" */
        if (domain_len > 0 && domain[0] != '\0')
            libcmd_sprintf_s(name_buf, name_size, "%s\\%s", domain, name);
        else
            libcmd_sprintf_s(name_buf, name_size, "%s", name);
        ret = 0;
    }

    if (sd != NULL)
        LocalFree(sd);
    return ret;
}

/* -------------------------------------------------------------------------
 * Executable detection ("start <file>")
 * ---------------------------------------------------------------------- */

int libcmd_is_executable_file(const char *path)
{
    libcmd_stat_t st;
    const char *ext;

    if (path == NULL)
        return 0;
    if (libcmd_stat(path, &st, 1) != 0 || !st.is_regular)
        return 0;

    /* Windows decides purely by extension: .com, .exe, .bat, .cmd */
    ext = libcmd_path_ext(path);
    if (ext[0] != '.')
        return 0;
    if (libcmd_strcasecmp(ext, ".exe") == 0 ||
        libcmd_strcasecmp(ext, ".com") == 0 ||
        libcmd_strcasecmp(ext, ".bat") == 0 ||
        libcmd_strcasecmp(ext, ".cmd") == 0)
        return 1;
    return 0;
}

/* -------------------------------------------------------------------------
 * Default-application open ("start <path>" on Windows)
 * ---------------------------------------------------------------------- */

int libcmd_shell_open(const char *path, int wait,
                      libcmd_exit_info_t *exit_info)
{
    SHELLEXECUTEINFOA sei;

    if (path == NULL)
        return -1;

    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize   = sizeof(sei);
    sei.fMask    = wait ? (SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_DDEWAIT)
                        : SEE_MASK_FLAG_DDEWAIT;
    sei.nShow    = SW_SHOWNORMAL;
    sei.lpFile   = path;

    if (!ShellExecuteExA(&sei))
        return -1;
    if ((INT_PTR)sei.hInstApp <= 32)
        return -1;

    if (wait && sei.hProcess != NULL)
    {
        DWORD exit_code = 0;

        if (WaitForSingleObject(sei.hProcess, INFINITE) == WAIT_FAILED)
        {
            CloseHandle(sei.hProcess);
            return -1;
        }
        GetExitCodeProcess(sei.hProcess, &exit_code);
        CloseHandle(sei.hProcess);
        if (exit_info != NULL)
        {
            exit_info->exited    = 1;
            exit_info->exit_code = (int)exit_code;
            exit_info->signaled  = 0;
            exit_info->signal    = 0;
        }
    }
    return 0;
}

int libcmd_stat(const char *path, libcmd_stat_t *out, int follow_links)
{
    struct __stat64 st;
    int ret;

    if (path == NULL || out == NULL)
        return -1;

    ret = _stat64(path, &st);
    if (ret < 0)
        return -1;

    out->is_regular = (st.st_mode & _S_IFREG) ? LIBCMD_TRUE : LIBCMD_FALSE;
    out->is_dir     = (st.st_mode & _S_IFDIR) ? LIBCMD_TRUE : LIBCMD_FALSE;
    out->is_link    = LIBCMD_FALSE;
    out->size       = st.st_size;
    out->mode       = (unsigned int)st.st_mode;

    {
        struct tm *tm_info;

        tm_info = localtime(&st.st_mtime);
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

        tm_info = localtime(&st.st_atime);
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

        tm_info = localtime(&st.st_ctime);
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

    return 0;
}

/* -------------------------------------------------------------------------
 * Directory operations
 * ---------------------------------------------------------------------- */

struct libcmd_dir {
    HANDLE hFind;
    WIN32_FIND_DATAA find_data;
    int first;
    char path[4096];
};

libcmd_dir_t libcmd_opendir(const char *path)
{
    struct libcmd_dir *dir;
    char search_path[4096];

    if (path == NULL)
        return NULL;

    dir = (struct libcmd_dir *)malloc(sizeof(struct libcmd_dir));
    if (dir == NULL)
        return NULL;

    libcmd_sprintf_s(search_path, sizeof(search_path), "%s\\*", path);
    libcmd_sprintf_s(dir->path, sizeof(dir->path), "%s", path);

    dir->hFind = FindFirstFileA(search_path, &dir->find_data);
    dir->first = 1;

    if (dir->hFind == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }

    return dir;
}

int libcmd_readdir(libcmd_dir_t dir_handle, libcmd_dirent_t *entry)
{
    struct libcmd_dir *dir = (struct libcmd_dir *)dir_handle;
    char full_path[4096];

    if (dir == NULL || entry == NULL)
        return -1;

    if (dir->first) {
        dir->first = 0;
    } else {
        if (!FindNextFileA(dir->hFind, &dir->find_data))
            return -1;
    }

    libcmd_sprintf_s(full_path, sizeof(full_path), "%s\\%s",
                     dir->path, dir->find_data.cFileName);
    finddata_to_libcmd(&dir->find_data, full_path, entry);

    return 0;
}

void libcmd_closedir(libcmd_dir_t dir_handle)
{
    struct libcmd_dir *dir = (struct libcmd_dir *)dir_handle;

    if (dir == NULL)
        return;

    if (dir->hFind != INVALID_HANDLE_VALUE)
        FindClose(dir->hFind);

    free(dir);
}

/* -------------------------------------------------------------------------
 * Glob
 * ---------------------------------------------------------------------- */

int libcmd_glob(const char *pattern, libcmd_glob_result_t *result)
{
    WIN32_FIND_DATAA find_data;
    HANDLE hFind;
    char **paths = NULL;
    size_t count = 0, alloc = 0;

    if (pattern == NULL || result == NULL)
        return -1;

    result->paths = NULL;
    result->count = 0;

    hFind = FindFirstFileA(pattern, &find_data);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;

    do {
        if (count >= alloc) {
            char **tmp;
            alloc = alloc ? alloc * 2 : 8;
            tmp = (char **)realloc(paths, alloc * sizeof(char *));
            if (tmp == NULL) {
                /* Cleanup */
                size_t i;
                for (i = 0; i < count; i++)
                    free(paths[i]);
                free(paths);
                FindClose(hFind);
                return -1;
            }
            paths = tmp;
        }

        paths[count] = libcmd_strdup(find_data.cFileName);
        if (paths[count] == NULL) {
            size_t i;
            for (i = 0; i < count; i++)
                free(paths[i]);
            free(paths);
            FindClose(hFind);
            return -1;
        }
        count++;
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);

    result->paths = paths;
    result->count = count;

    return 0;
}

void libcmd_glob_free(libcmd_glob_result_t *result)
{
    size_t i;

    if (result == NULL)
        return;

    if (result->paths != NULL) {
        for (i = 0; i < result->count; i++)
            free(result->paths[i]);
        free(result->paths);
    }

    result->paths = NULL;
    result->count = 0;
}

/* -------------------------------------------------------------------------
 * Disk space
 * ---------------------------------------------------------------------- */

int libcmd_get_disk_space(const char *path,
                          libcmd_filesize_t *free_bytes,
                          libcmd_filesize_t *total_bytes)
{
    ULARGE_INTEGER free_avail, total_bytes_li, total_free;
    const char *root;

    if (path == NULL)
        return -1;

    /* Extract root path (e.g., "C:\") */
    root = path;
    if (root[0] != '\0' && root[1] == ':') {
        char root_path[4];
        root_path[0] = root[0];
        root_path[1] = ':';
        root_path[2] = '\\';
        root_path[3] = '\0';

        if (!GetDiskFreeSpaceExA(root_path, &free_avail, &total_bytes_li, &total_free))
            return -1;

        if (free_bytes)
            *free_bytes = (libcmd_filesize_t)free_avail.QuadPart;
        if (total_bytes)
            *total_bytes = (libcmd_filesize_t)total_bytes_li.QuadPart;
        return 0;
    }

    return -1;
}

#endif
