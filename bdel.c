/*
 * bdel.c - DEL / ERASE builtin
 *
 * DEL  [/P] [/F] [/S] [/Q] [/A[[:attributes]]] names
 * ERASE [/P] [/F] [/S] [/Q] [/A[[:attributes]]] names
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int prompt;       /* /P */
    int force;        /* /F */
    int quiet;        /* /Q */
    int recur;        /* /S */
    int have_attrib;  /* /A given */
    char attrib[64];  /* /A spec */
} del_opts_t;

static int attr_matches(const libcmd_dirent_t *e, char attr)
{
    switch (attr) {
    case 'D': return e->is_dir;
    case 'H': return e->name[0] == '.';
    case 'R': return !(e->mode & 0200);
    case 'L': return e->is_link;
    case 'S':
    case 'A':
    default:  return 1;
    }
}

static int attrib_filter(const libcmd_dirent_t *e, const char *spec)
{
    const char *p;
    if (spec == NULL || spec[0] == '\0')
        return 1;
    for (p = spec; *p; p++) {
        int neg = 0;
        if (*p == '-') {
            neg = 1;
            p++;
        }
        if (!*p)
            break;
        if (neg ? attr_matches(e, (char)toupper((unsigned char)*p))
                : !attr_matches(e, (char)toupper((unsigned char)*p)))
            return 0;
    }
    return 1;
}

static int name_matches(const char *name, const char *pattern)
{
    if (strchr(pattern, '*') != NULL || strchr(pattern, '?') != NULL) {
        libcmd_glob_result_t gr;
        size_t i;
        int found = 0;
        if (libcmd_glob(pattern, &gr) != 0)
            return 0;
        for (i = 0; i < gr.count; i++) {
            if (libcmd_strcasecmp(gr.paths[i], name) == 0) {
                found = 1;
                break;
            }
        }
        libcmd_glob_free(&gr);
        return found;
    }
    return libcmd_strcasecmp(name, pattern) == 0;
}

static int do_delete(cmd_context_t *ctx, const char *name,
                     const del_opts_t *o, int *ret)
{
    int do_del = 1;

    (void)ctx;

    if (o->prompt) {
        printf(cmd_gettext(MSG_ASK_DELETE), name);
        fflush(stdout);
        {
            int c = getchar();
            while (c != '\n' && c != EOF) {
                int last = c;
                c = getchar();
                if (c == '\n' || c == EOF)
                    do_del = (last == 'y' || last == 'Y');
            }
        }
    }

    if (!do_del)
        return 0;

    if (libcmd_unlink(name) < 0) {
        /* /F: force-delete read-only files */
        if (o->force && libcmd_chmod(name, 0644) == 0) {
            if (libcmd_unlink(name) == 0)
                return 0;
        }
        if (!o->quiet)
            fprintf(stderr, cmd_gettext(MSG_ERR_COULD_NOT_DELETE), name);
        *ret = 1;
        return -1;
    }
    return 0;
}

/* Recursive deletion for /S: walk dir_path, delete files matching
 * pattern (and attribute filter), recurse into subdirectories */
static void del_tree(const char *dir_path, const char *pattern,
                     const del_opts_t *o, int *ret)
{
    libcmd_dir_t d;
    libcmd_dirent_t entry;
    char **subdirs = NULL;
    size_t sub_count = 0, sub_alloc = 0;
    char old_cwd[CMD_MAX_PATH];
    size_t i;

    d = libcmd_opendir(dir_path);
    if (d == NULL)
        return;

    if (libcmd_getcwd(old_cwd, sizeof(old_cwd)) == NULL)
        old_cwd[0] = '\0';
    libcmd_chdir(dir_path);

    while (libcmd_readdir(d, &entry) == 0) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
            continue;

        if (entry.is_dir && !entry.is_link) {
            if (sub_count >= sub_alloc) {
                char **tmp;
                sub_alloc = sub_alloc ? sub_alloc * 2 : 16;
                tmp = (char **)realloc(subdirs, sub_alloc * sizeof(char *));
                if (tmp == NULL)
                    break;
                subdirs = tmp;
            }
            subdirs[sub_count++] = libcmd_strdup(entry.name);
            continue;
        }

        if (!name_matches(entry.name, pattern))
            continue;
        if (o->have_attrib && !attrib_filter(&entry, o->attrib))
            continue;

        do_delete(NULL, entry.name, o, ret);
    }

    libcmd_closedir(d);
    if (old_cwd[0])
        libcmd_chdir(old_cwd);

    for (i = 0; i < sub_count; i++) {
        size_t dl = strlen(dir_path);
        char child[CMD_MAX_PATH];
        if (dl > 0 && dir_path[dl - 1] == '/')
            libcmd_sprintf_s(child, sizeof(child), "%s%s", dir_path, subdirs[i]);
        else
            libcmd_sprintf_s(child, sizeof(child), "%s/%s", dir_path, subdirs[i]);
        del_tree(child, pattern, o, ret);
        free(subdirs[i]);
    }
    free(subdirs);
}

int builtin_del(cmd_context_t *ctx, int argc, char **argv)
{
    del_opts_t o;
    int i;
    int ret = 0;

    (void)ctx;

    if (argc <= 1) {
        fputs(cmd_gettext(MSG_HELP_DEL), stdout);
        return 0;
    }

    memset(&o, 0, sizeof(o));

    for (i = 1; i < argc; i++) {
        if (libcmd_is_switch(argv[i], "PFSQA")) {
            char opt = (char)toupper((unsigned char)argv[i][1]);
            const char *arg = argv[i] + 2;
            if (*arg == ':')
                arg++;
            if (opt == 'P') o.prompt = 1;
            else if (opt == 'F') o.force = 1;
            else if (opt == 'S') o.recur = 1;
            else if (opt == 'Q') o.quiet = 1;
            else if (opt == 'A') {
                o.have_attrib = 1;
                if (arg[0])
                    libcmd_sprintf_s(o.attrib, sizeof(o.attrib), "%s", arg);
            }
            else if (argv[i][1] == '?') {
                fputs(cmd_gettext(MSG_INFO_DEL_QUIET), stdout);
                return 0;
            }
        } else {
            /* It's a file/pattern */
            const char *arg = argv[i];

            if (o.recur) {
                /* Split into directory + pattern */
                const char *last_slash = strrchr(arg, '/');
                const char *fname = last_slash ? last_slash + 1 : arg;
                char dir_path[CMD_MAX_PATH];

                if (last_slash) {
                    size_t dlen = (size_t)(last_slash - arg);
                    if (dlen == 0) dlen = 1;
                    memcpy(dir_path, arg, dlen);
                    dir_path[dlen] = '\0';
                } else {
                    libcmd_sprintf_s(dir_path, sizeof(dir_path), ".");
                }

                if (strchr(fname, '*') == NULL && strchr(fname, '?') == NULL) {
                    /* Plain name: cmd /S deletes it in the given dir only
                     * when it appears in subdirs too; delete if it exists
                     * in dir, then search deeper only if wildcard. */
                    char full[CMD_MAX_PATH];
                    if (strcmp(dir_path, ".") != 0)
                        libcmd_sprintf_s(full, sizeof(full), "%s/%s",
                                         dir_path, fname);
                    else
                        libcmd_sprintf_s(full, sizeof(full), "%s", fname);
                    do_delete(ctx, full, &o, &ret);
                } else {
                    del_tree(dir_path, fname, &o, &ret);
                }
            } else if (strchr(arg, '*') != NULL || strchr(arg, '?') != NULL) {
                /* Wildcard: expand */
                libcmd_glob_result_t gr;
                if (libcmd_glob(arg, &gr) == 0) {
                    size_t j;
                    for (j = 0; j < gr.count; j++)
                        do_delete(ctx, gr.paths[j], &o, &ret);
                    libcmd_glob_free(&gr);
                } else {
                    fprintf(stderr, cmd_gettext(MSG_ERR_COULD_NOT_FIND), arg);
                    ret = 1;
                }
            } else {
                do_delete(ctx, arg, &o, &ret);
            }
        }
    }

    return ret;
}
