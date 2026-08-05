/*
 * bftype.c - FTYPE builtin
 *
 * FTYPE [fileType[=[openCommandString]]]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FTYPE_FILE "/tmp/.cmd_ftype"

static FILE *open_ftype(const char *mode)
{
    const char *home = libcmd_getenv("HOME");
    char path[CMD_MAX_PATH];

    if (home)
        libcmd_sprintf_s(path, sizeof(path), "%s/.cmd_ftype", home);
    else
        libcmd_sprintf_s(path, sizeof(path), "%s", FTYPE_FILE);

    return fopen(path, mode);
}

static char *lookup_ftype(const char *ft)
{
    FILE *f = open_ftype("r");
    char line[512];
    char *result = NULL;

    if (!f) return NULL;

    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (libcmd_strcasecmp(line, ft) == 0) {
            char *val = eq + 1;
            size_t len = strlen(val);
            while (len > 0 && (val[len-1] == '\n' || val[len-1] == '\r'))
                val[--len] = '\0';
            result = libcmd_strdup(val);
            break;
        }
    }
    fclose(f);
    return result;
}

static int set_ftype(const char *ft, const char *cmd)
{
    FILE *f_in  = open_ftype("r");
    FILE *f_out;
    char tmp_path[CMD_MAX_PATH];
    char ftype_path[CMD_MAX_PATH];
    const char *home = libcmd_getenv("HOME");
    char line[512];
    int found = 0;

    if (home)
        libcmd_sprintf_s(ftype_path, sizeof(ftype_path), "%s/.cmd_ftype", home);
    else
        libcmd_sprintf_s(ftype_path, sizeof(ftype_path), "%s", FTYPE_FILE);

    libcmd_sprintf_s(tmp_path, sizeof(tmp_path), "%s.tmp", ftype_path);

    f_out = fopen(tmp_path, "w");
    if (!f_out) { if (f_in) fclose(f_in); return -1; }

    if (f_in) {
        while (fgets(line, sizeof(line), f_in)) {
            char *eq = strchr(line, '=');
            if (eq) {
                char saved = *eq;
                *eq = '\0';
                if (libcmd_strcasecmp(line, ft) == 0) {
                    found = 1;
                    if (cmd && *cmd)
                        fprintf(f_out, "%s=%s\n", ft, cmd);
                    continue;
                }
                *eq = saved;
            }
            fputs(line, f_out);
        }
        fclose(f_in);
    }

    if (!found && cmd && *cmd)
        fprintf(f_out, "%s=%s\n", ft, cmd);

    fclose(f_out);
    libcmd_rename(tmp_path, ftype_path);
    return 0;
}

static void list_ftype(void)
{
    FILE *f = open_ftype("r");
    char line[512];

    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        puts(line);
    }
    fclose(f);
}

int builtin_ftype(cmd_context_t *ctx, int argc, char **argv)
{
    (void)ctx;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_FTYPE), stdout);
        return 0;
    }

    if (argc <= 1) {
        list_ftype();
        return 0;
    }

    {
        const char *arg = argv[1];
        const char *eq  = strchr(arg, '=');

        if (eq == NULL) {
            char *val = lookup_ftype(arg);
            if (val) {
                printf("%s=%s\n", arg, val);
                free(val);
            } else {
                fprintf(stderr, cmd_gettext(MSG_ERR_FTYPE_NOT_FOUND), arg);
                return 1;
            }
        } else {
            char ft[128];
            size_t flen = (size_t)(eq - arg);
            if (flen >= sizeof(ft)) flen = sizeof(ft) - 1;
            memcpy(ft, arg, flen);
            ft[flen] = '\0';
            if (set_ftype(ft, eq + 1) < 0) return 1;
            if (*(eq + 1))
                printf("%s=%s\n", ft, eq + 1);
        }
    }

    return 0;
}
