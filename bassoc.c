/*
 * bassoc.c - ASSOC builtin
 *
 * ASSOC [.ext[=[fileType]]]
 *
 * On Unix, file associations are stored in $HOME/.cmd_assoc or
 * $PREFIX/etc/cmd/assoc.
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSOC_FILE "/tmp/.cmd_assoc"

/* Simple key=value store: read the assoc file line by line */
static FILE *open_assoc(const char *mode)
{
    const char *home = libcmd_getenv("HOME");
    char path[CMD_MAX_PATH];

    if (home)
        libcmd_sprintf_s(path, sizeof(path), "%s/.cmd_assoc", home);
    else
        libcmd_sprintf_s(path, sizeof(path), "%s", ASSOC_FILE);

    return fopen(path, mode);
}

static char *lookup_assoc(const char *ext)
{
    FILE *f = open_assoc("r");
    char line[512];
    char *result = NULL;

    if (!f) return NULL;

    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (libcmd_strcasecmp(line, ext) == 0) {
            /* Strip newline */
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

static int set_assoc(const char *ext, const char *ftype)
{
    FILE *f_in  = open_assoc("r");
    FILE *f_out;
    char tmp_path[CMD_MAX_PATH];
    char assoc_path[CMD_MAX_PATH];
    const char *home = libcmd_getenv("HOME");
    char line[512];
    int found = 0;

    if (home)
        libcmd_sprintf_s(assoc_path, sizeof(assoc_path), "%s/.cmd_assoc", home);
    else
        libcmd_sprintf_s(assoc_path, sizeof(assoc_path), "%s", ASSOC_FILE);

    libcmd_sprintf_s(tmp_path, sizeof(tmp_path), "%s.tmp", assoc_path);

    f_out = fopen(tmp_path, "w");
    if (!f_out) {
        if (f_in) fclose(f_in);
        return -1;
    }

    if (f_in) {
        while (fgets(line, sizeof(line), f_in)) {
            char *eq = strchr(line, '=');
            if (eq) {
                char saved = *eq;
                *eq = '\0';
                if (libcmd_strcasecmp(line, ext) == 0) {
                    found = 1;
                    if (ftype && *ftype)
                        fprintf(f_out, "%s=%s\n", ext, ftype);
                    continue;
                }
                *eq = saved;
            }
            fputs(line, f_out);
        }
        fclose(f_in);
    }

    if (!found && ftype && *ftype)
        fprintf(f_out, "%s=%s\n", ext, ftype);

    fclose(f_out);
    libcmd_rename(tmp_path, assoc_path);
    return 0;
}

static void list_assoc(void)
{
    FILE *f = open_assoc("r");
    char line[512];

    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        puts(line);
    }
    fclose(f);
}

int builtin_assoc(cmd_context_t *ctx, int argc, char **argv)
{
    (void)ctx;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_ASSOC), stdout);
        return 0;
    }

    if (argc <= 1) {
        list_assoc();
        return 0;
    }

    {
        const char *arg = argv[1];
        const char *eq  = strchr(arg, '=');

        if (eq == NULL) {
            /* Query */
            char *val = lookup_assoc(arg);
            if (val) {
                printf("%s=%s\n", arg, val);
                free(val);
            } else {
                fprintf(stderr, cmd_gettext(MSG_ERR_ASSOC_NOT_FOUND), arg);
                return 1;
            }
        } else {
            char ext[64];
            size_t elen = (size_t)(eq - arg);
            if (elen >= sizeof(ext)) elen = sizeof(ext) - 1;
            memcpy(ext, arg, elen);
            ext[elen] = '\0';

            if (set_assoc(ext, eq + 1) < 0) {
                fprintf(stderr, "%s", cmd_gettext(MSG_ERR_ASSOC_UPDATE));
                return 1;
            }
            if (*(eq + 1))
                printf("%s=%s\n", ext, eq + 1);
        }
    }

    return 0;
}
