#define _CRT_SECURE_NO_WARNINGS
/*
 * lwautorun.c - execute AutoRun scripts (Windows implementation)
 *
 * On Windows, executes all batch files under %PREFIX%\etc\cmd\AutoRun\
 * (sorted by name) by reading them as batch scripts and passing each line
 * to the provided run_fn callback.
 *
 * License: GNU GPLv3
 */

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)

#include "glibcmd.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compare function for qsort to sort filenames */
static int filename_cmp(const void *a, const void *b)
{
    return _stricmp(*(const char *const *)a, *(const char *const *)b);
}

int libcmd_exec_autorun(const char *prefix,
                        libcmd_run_fn run_fn,
                        void *user_data)
{
    char dir_path[4096];
    WIN32_FIND_DATAA find_data;
    HANDLE hFind;
    char **names = NULL;
    size_t count = 0, alloc = 0;
    size_t i;
    int ret = 0;
    char search_path[4096];

    if (prefix == NULL)
        prefix = "C:\\Program Files\\cmd";

    libcmd_sprintf_s(dir_path, sizeof(dir_path),
                     "%s\\etc\\cmd\\AutoRun", prefix);

    libcmd_sprintf_s(search_path, sizeof(search_path), "%s\\*", dir_path);

    hFind = FindFirstFileA(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0; /* No autorun directory is OK */

    /* Collect batch filenames */
    do {
        char full[4096];
        DWORD attrs;

        if (find_data.cFileName[0] == '.')
            continue;

        libcmd_sprintf_s(full, sizeof(full), "%s\\%s", dir_path, find_data.cFileName);

        attrs = GetFileAttributesA(full);
        if (attrs == INVALID_FILE_ATTRIBUTES)
            continue;
        if (attrs & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        /* Check if it's a .bat or .cmd file */
        {
            const char *ext = strrchr(find_data.cFileName, '.');
            if (ext == NULL)
                continue;
            if (_stricmp(ext, ".bat") != 0 && _stricmp(ext, ".cmd") != 0)
                continue;
        }

        if (count >= alloc) {
            char **tmp;
            alloc = alloc ? alloc * 2 : 8;
            tmp = (char **)realloc(names, alloc * sizeof(char *));
            if (tmp == NULL) {
                ret = -1;
                goto cleanup;
            }
            names = tmp;
        }

        names[count] = libcmd_strdup(find_data.cFileName);
        if (names[count] == NULL) {
            ret = -1;
            goto cleanup;
        }
        count++;
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);

    /* Sort alphabetically */
    if (count > 0)
        qsort(names, count, sizeof(char *), filename_cmp);

    /* Execute each script */
    for (i = 0; i < count; i++) {
        char full[4096];
        FILE *f;
        char line[4096];

        libcmd_sprintf_s(full, sizeof(full), "%s\\%s", dir_path, names[i]);

        f = fopen(full, "r");
        if (f == NULL)
            continue;

        while (fgets(line, sizeof(line), f) != NULL) {
            /* Strip trailing newline */
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                line[--len] = '\0';

            if (run_fn && len > 0) {
                if (run_fn(line, user_data) < 0)
                    break;
            }
        }
        fclose(f);
    }

cleanup:
    for (i = 0; i < count; i++)
        free(names[i]);
    free(names);
    return ret;
}

#endif
