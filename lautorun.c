/*
 * lautorun.c - execute AutoRun scripts
 *
 * On Unix, executes all executable files under $PREFIX/etc/cmd/AutoRun/
 * (sorted by name) by reading them as batch scripts and passing each line
 * to the provided run_fn callback.
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Compare function for qsort to sort dirent names */
static int dirent_name_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int libcmd_exec_autorun(const char *prefix,
                        libcmd_run_fn run_fn,
                        void *user_data)
{
    char dir_path[4096];
    DIR *d;
    struct dirent *de;
    char **names = NULL;
    size_t count = 0, alloc = 0;
    size_t i;
    int ret = 0;

    if (prefix == NULL)
        prefix = "/usr/local";

    libcmd_sprintf_s(dir_path, sizeof(dir_path),
                     "%s/etc/cmd/AutoRun", prefix);

    d = opendir(dir_path);
    if (d == NULL)
        return 0; /* No autorun directory is OK */

    /* Collect executable filenames */
    while ((de = readdir(d)) != NULL) {
        char full[4096];
        struct stat st;

        if (de->d_name[0] == '.')
            continue;

        libcmd_sprintf_s(full, sizeof(full), "%s/%s", dir_path, de->d_name);

        if (stat(full, &st) < 0)
            continue;
        if (!S_ISREG(st.st_mode))
            continue;
        if (!(st.st_mode & S_IXUSR))
            continue;

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

        names[count] = libcmd_strdup(de->d_name);
        if (names[count] == NULL) {
            ret = -1;
            goto cleanup;
        }
        count++;
    }

    /* Sort alphabetically */
    if (count > 0)
        qsort(names, count, sizeof(char *), dirent_name_cmp);

    /* Execute each script */
    for (i = 0; i < count; i++) {
        char full[4096];
        FILE *f;
        char line[4096];

        libcmd_sprintf_s(full, sizeof(full), "%s/%s", dir_path, names[i]);

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
    closedir(d);
    return ret;
}
