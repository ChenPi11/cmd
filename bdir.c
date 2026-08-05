/*
 * bdir.c - DIR builtin
 *
 * DIR [drive:][path][filename] [/A[[:]attributes]] [/B] [/D] [/L] [/N]
 *     [/O[[:]sortorder]] [/P] [/Q] [/S] [/T[[:]timefield]] [/W] [/X] [/4]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <ctype.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Directory listing options (shared between main listing and /S tree) */
typedef struct {
    int bare;          /* /B */
    int wide;          /* /W or /D */
    int lower;         /* /L */
    int recur;         /* /S */
    int pause;         /* /P */
    int show_owner;    /* /Q */
    int sort_key;      /* /O: 'N' 'S' 'E' 'D' */
    int sort_reverse;  /* /O- */
    int dir_first;     /* /O:G */
    int dir_last;      /* /O:-G */
    int time_field;    /* /T: 'W' 'C' 'A' */
    char attrib[64];   /* /A spec, empty = show all incl. hidden */
    int have_attrib;   /* /A given (with or without spec) */
} dir_opts_t;

/* Active options for the qsort comparator */
static const dir_opts_t *g_sort_opts;

/* Approximate seconds for time comparisons (fine for sorting) */
static off_t t_secs(const libcmd_time_t *t)
{
    static const int mdays[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    off_t d = (off_t)(t->year - 1970) * 365;
    int m;
    for (m = 0; m < t->month - 1 && m < 12; m++)
        d += mdays[m];
    d += t->day - 1;
    return d * (off_t)86400 + (off_t)t->hour * 3600 + t->minute * 60 + t->second;
}

static int entry_cmp(const void *a, const void *b)
{
    const libcmd_dirent_t *ea = *(const libcmd_dirent_t *const *)a;
    const libcmd_dirent_t *eb = *(const libcmd_dirent_t *const *)b;
    const dir_opts_t *o = g_sort_opts;
    int cmp = 0;

    if (o->dir_first || o->dir_last) {
        if (ea->is_dir != eb->is_dir) {
            if (o->dir_first)
                return ea->is_dir ? -1 : 1;
            return ea->is_dir ? 1 : -1;
        }
    }

    switch (o->sort_key) {
    case 'S': /* size */
        cmp = (ea->size > eb->size) - (ea->size < eb->size);
        if (cmp == 0)
            cmp = libcmd_strcasecmp(ea->name, eb->name);
        break;
    case 'E': { /* extension */
        const char *ex1 = strrchr(ea->name, '.');
        const char *ex2 = strrchr(eb->name, '.');
        cmp = libcmd_strcasecmp(ex1 ? ex1 : "", ex2 ? ex2 : "");
        if (cmp == 0)
            cmp = libcmd_strcasecmp(ea->name, eb->name);
        break;
    }
    case 'D': { /* date (time field) */
        const libcmd_time_t *t1, *t2;
        t1 = (o->time_field == 'A') ? &ea->atime
           : (o->time_field == 'C') ? &ea->ctime : &ea->mtime;
        t2 = (o->time_field == 'A') ? &eb->atime
           : (o->time_field == 'C') ? &eb->ctime : &eb->mtime;
        cmp = (t_secs(t1) > t_secs(t2)) - (t_secs(t1) < t_secs(t2));
        if (cmp == 0)
            cmp = libcmd_strcasecmp(ea->name, eb->name);
        break;
    }
    default: /* 'N' name */
        cmp = libcmd_strcasecmp(ea->name, eb->name);
        break;
    }

    if (o->sort_reverse)
        cmp = -cmp;
    return cmp;
}

static int subdir_cmp(const void *a, const void *b)
{
    return libcmd_strcasecmp(*(char *const *)a, *(char *const *)b);
}

static int attr_matches(const libcmd_dirent_t *e, char attr)
{
    switch (attr) {
    case 'D': return e->is_dir;
    case 'H': return e->name[0] == '.';
    case 'R': return !(e->mode & 0200); /* no write bits = read-only */
    case 'L': return e->is_link;
    case 'S': /* system files: no Unix equivalent, match all */
    case 'A': /* archive: no Unix equivalent, match all */
    default:  return 1;
    }
}

static int attrib_filter(const libcmd_dirent_t *e, const char *spec)
{
    const char *p;
    /* "." and ".." are never listed (cmd.exe behaviour) */
    if (strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0)
        return 0;
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

/* Default filter (no /A): skip "." ".." and hidden files */
static int hidden_filter(const libcmd_dirent_t *e)
{
    if (strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0)
        return 0;
    if (e->name[0] == '.')
        return 0;
    return 1;
}

/* /P: pause every full screen when stdin is a terminal */
static void maybe_pause(int *lines, const dir_opts_t *o)
{
    if (!o->pause || !isatty(0))
        return;
    if (++(*lines) >= 24) {
        *lines = 0;
        fputs(cmd_gettext(MSG_ASK_PRESS_KEY_NL), stdout);
        fflush(stdout);
        getchar();
        fputc('\n', stdout);
    }
}

static void print_time(const libcmd_time_t *t, const dir_opts_t *o)
{
    printf("%02d/%02d/%04d  %02d:%02d",
           t->month, t->day, t->year, t->hour, t->minute);
    (void)o;
}

static const libcmd_time_t *pick_time(const libcmd_dirent_t *e,
                                      const dir_opts_t *o)
{
    if (o->time_field == 'A') return &e->atime;
    if (o->time_field == 'C') return &e->ctime;
    return &e->mtime;
}

/* -------------------------------------------------------------------------
 * Recursive listing for /S
 * ---------------------------------------------------------------------- */

static void dir_list_tree(const char *dir_path, const char *pattern,
                          const dir_opts_t *o, int *lines,
                          off_t *tot_files, off_t *tot_size,
                          off_t *tot_dirs)
{
    libcmd_dir_t d;
    libcmd_dirent_t entry;
    libcmd_dirent_t **entries = NULL;
    size_t entry_count = 0, entry_alloc = 0;
    char **subdirs = NULL;
    size_t sub_count = 0, sub_alloc = 0;
    char old_cwd[CMD_MAX_PATH];
    size_t i;

    d = libcmd_opendir(dir_path);
    if (d == NULL) {
        fprintf(stderr, "%s", cmd_gettext(MSG_INFO_FILE_NOT_FOUND));
        return;
    }

    if (libcmd_getcwd(old_cwd, sizeof(old_cwd)) == NULL)
        old_cwd[0] = '\0';
    libcmd_chdir(dir_path);

    while (libcmd_readdir(d, &entry) == 0) {
        libcmd_dirent_t *e;

        /* Apply pattern filter */
        if (strcmp(pattern, "*") != 0) {
            libcmd_glob_result_t gr;
            char test[CMD_MAX_PATH];
            size_t gi;
            int found = 0;
            libcmd_sprintf_s(test, sizeof(test), "%s", entry.name);
            if (libcmd_glob(pattern, &gr) == 0) {
                for (gi = 0; gi < gr.count; gi++) {
                    if (libcmd_strcasecmp(gr.paths[gi], test) == 0) {
                        found = 1;
                        break;
                    }
                }
                libcmd_glob_free(&gr);
                if (!found) continue;
            }
        }

        if (!o->have_attrib && !hidden_filter(&entry))
            continue;
        if (o->have_attrib && !attrib_filter(&entry, o->attrib))
            continue;

        e = (libcmd_dirent_t *)malloc(sizeof(libcmd_dirent_t));
        if (e == NULL) break;
        *e = entry;

        if (o->lower) {
            char *p;
            for (p = e->name; *p; p++)
                *p = (char)tolower((unsigned char)*p);
        }

        if (entry_count >= entry_alloc) {
            libcmd_dirent_t **tmp;
            entry_alloc = entry_alloc ? entry_alloc * 2 : 32;
            tmp = (libcmd_dirent_t **)realloc(entries,
                                               entry_alloc * sizeof(void *));
            if (tmp == NULL) { free(e); break; }
            entries = tmp;
        }
        entries[entry_count++] = e;

        if (entry.is_dir && !entry.is_link &&
            strcmp(entry.name, ".") != 0 &&
            strcmp(entry.name, "..") != 0) {
            if (sub_count >= sub_alloc) {
                char **tmp;
                sub_alloc = sub_alloc ? sub_alloc * 2 : 16;
                tmp = (char **)realloc(subdirs, sub_alloc * sizeof(char *));
                if (tmp == NULL) break;
                subdirs = tmp;
            }
            subdirs[sub_count++] = libcmd_strdup(entry.name);
        }

        if (entry.is_dir)
            (*tot_dirs)++;
        else {
            (*tot_files)++;
            *tot_size += entry.size;
        }
    }

    libcmd_closedir(d);
    if (old_cwd[0])
        libcmd_chdir(old_cwd);

    g_sort_opts = o;
    if (entries && entry_count > 0)
        qsort(entries, entry_count, sizeof(void *), entry_cmp);
    if (subdirs && sub_count > 0)
        qsort(subdirs, sub_count, sizeof(char *), subdir_cmp);

    /* Header */
    if (!o->bare) {
        char abs_dir[CMD_MAX_PATH];
        libcmd_path_abs(dir_path, abs_dir, sizeof(abs_dir));
        printf(cmd_gettext(MSG_INFO_DIRECTORY_OF), abs_dir);
    }

    /* Entries */
    if (o->wide) {
        int col = 0;
        for (i = 0; i < entry_count; i++) {
            libcmd_dirent_t *e = entries[i];
            if (e->is_dir)
                printf("[%-14s]", e->name);
            else
                printf("%-16s", e->name);
            if (++col % 5 == 0) {
                fputc('\n', stdout);
                maybe_pause(lines, o);
            }
        }
        if (col % 5 != 0) fputc('\n', stdout);
    } else if (o->bare) {
        for (i = 0; i < entry_count; i++) {
            size_t dl = strlen(dir_path);
            if (dl > 0 && dir_path[dl - 1] == '/')
                printf("%s%s\n", dir_path, entries[i]->name);
            else
                printf("%s/%s\n", dir_path, entries[i]->name);
            maybe_pause(lines, o);
        }
    } else {
        for (i = 0; i < entry_count; i++) {
            libcmd_dirent_t *e = entries[i];
            const libcmd_time_t *tm = pick_time(e, o);

            print_time(tm, o);

            if (o->show_owner) {
                struct passwd *pw = getpwuid((uid_t)e->uid);
                printf("  %-10s", pw ? pw->pw_name : "?");
            }

            if (e->is_dir)
                printf("    <DIR>          %-s\n", e->name);
            else
                printf("  %14ld %-s\n", (long)e->size, e->name);
            maybe_pause(lines, o);
        }
    }

    for (i = 0; i < entry_count; i++)
        free(entries[i]);
    free(entries);

    /* Recurse into subdirectories */
    for (i = 0; i < sub_count; i++) {
        char child[CMD_MAX_PATH];
        size_t dl = strlen(dir_path);
        if (dl > 0 && dir_path[dl - 1] == '/')
            libcmd_sprintf_s(child, sizeof(child), "%s%s", dir_path, subdirs[i]);
        else
            libcmd_sprintf_s(child, sizeof(child), "%s/%s", dir_path, subdirs[i]);
        dir_list_tree(child, pattern, o, lines,
                      tot_files, tot_size, tot_dirs);
        free(subdirs[i]);
    }
    free(subdirs);
}

int builtin_dir(cmd_context_t *ctx, int argc, char **argv)
{
    const char *path_arg = NULL;
    dir_opts_t o;
    int i;
    char dir_path[CMD_MAX_PATH];
    char pattern[CMD_MAX_PATH];
    libcmd_dir_t d;
    libcmd_dirent_t entry;
    libcmd_dirent_t **entries = NULL;
    size_t entry_count = 0, entry_alloc = 0;
    off_t total_size = 0;
    off_t free_bytes = 0, total_bytes = 0;
    int lines = 0;

    (void)ctx;

    memset(&o, 0, sizeof(o));
    o.sort_key   = 'N';
    o.dir_first  = 1;
    o.time_field = 'W';

    for (i = 1; i < argc; i++) {
        if (libcmd_is_switch(argv[i], "BWLDNSX4QPATO")) {
            char opt = (char)toupper((unsigned char)argv[i][1]);
            const char *arg = argv[i] + 2;
            if (*arg == ':')
                arg++;
            switch (opt) {
            case 'B': o.bare  = 1; break;
            case 'W': case 'D': o.wide  = 1; break;
            case 'L': o.lower = 1; break;
            case 'N': break; /* default on Unix */
            case 'S': o.recur = 1; break;
            case 'P': o.pause = 1; break;
            case 'X': case '4': break; /* no 8.3 names on Unix; 4-digit year is default */
            case 'Q': o.show_owner = 1; break;
            case 'A':
                o.have_attrib = 1;
                if (arg[0])
                    libcmd_sprintf_s(o.attrib, sizeof(o.attrib), "%s", arg);
                break;
            case 'O':
                if (arg[0]) {
                    const char *p;
                    int any = 0;
                    for (p = arg; *p; p++) {
                        if (*p == '-') { o.sort_reverse = 1; continue; }
                        any = 1;
                        switch ((char)toupper((unsigned char)*p)) {
                        case 'N': o.sort_key = 'N'; o.sort_reverse = 0; break;
                        case 'S': o.sort_key = 'S'; break;
                        case 'E': o.sort_key = 'E'; break;
                        case 'D': o.sort_key = 'D'; break;
                        case 'G': o.dir_first = 1; o.dir_last = 0;
                                  if (p > arg && p[-1] == '-') {
                                      o.dir_first = 0; o.dir_last = 1;
                                      o.sort_reverse = 0;
                                  }
                                  break;
                        default: break;
                        }
                    }
                    if (!any) { o.sort_key = 'N'; }
                }
                break;
            case 'T':
                if (arg[0]) {
                    switch ((char)toupper((unsigned char)arg[0])) {
                    case 'C': o.time_field = 'C'; break;
                    case 'A': o.time_field = 'A'; break;
                    default:  o.time_field = 'W'; break;
                    }
                }
                break;
            case '?':
                fputs(cmd_gettext(MSG_HELP_DIR), stdout);
                return 0;
            default: break;
            }
        } else {
            path_arg = argv[i];
        }
    }

    /* Parse path_arg into dir and pattern */
    if (path_arg == NULL) {
        libcmd_sprintf_s(dir_path, sizeof(dir_path), ".");
        libcmd_sprintf_s(pattern,  sizeof(pattern),  "*");
    } else {
        /* Check if path_arg ends with a wildcard or plain name */
        const char *last_slash = strrchr(path_arg, '/');
        const char *fname = last_slash ? last_slash + 1 : path_arg;
        int has_wild = strchr(fname, '*') != NULL || strchr(fname, '?') != NULL;

        if (has_wild) {
            if (last_slash) {
                size_t dlen = (size_t)(last_slash - path_arg);
                if (dlen == 0) dlen = 1;
                memcpy(dir_path, path_arg, dlen);
                dir_path[dlen] = '\0';
            } else {
                libcmd_sprintf_s(dir_path, sizeof(dir_path), ".");
            }
            libcmd_sprintf_s(pattern, sizeof(pattern), "%s", fname);
        } else {
            /* Check if it's a directory */
            libcmd_stat_t st;
            if (libcmd_stat(path_arg, &st, 1) == 0 && st.is_dir) {
                libcmd_sprintf_s(dir_path, sizeof(dir_path), "%s", path_arg);
                libcmd_sprintf_s(pattern,  sizeof(pattern),  "*");
            } else {
                if (last_slash) {
                    size_t dlen = (size_t)(last_slash - path_arg);
                    if (dlen == 0) dlen = 1;
                    memcpy(dir_path, path_arg, dlen);
                    dir_path[dlen] = '\0';
                } else {
                    libcmd_sprintf_s(dir_path, sizeof(dir_path), ".");
                }
                libcmd_sprintf_s(pattern, sizeof(pattern), "%s", fname);
            }
        }
    }

    /* /S: recursive listing */
    if (o.recur) {
        off_t tot_files = 0, tot_size = 0, tot_dirs = 0;

        dir_list_tree(dir_path, pattern, &o, &lines,
                      &tot_files, &tot_size, &tot_dirs);

        if (!o.bare) {
            printf("%s", cmd_gettext(MSG_INFO_TOTAL_FILES));
            printf(cmd_gettext(MSG_INFO_FILES_TOTAL), (long)tot_files, (long)tot_size);
            libcmd_get_disk_free(dir_path, &free_bytes, &total_bytes);
            printf(cmd_gettext(MSG_INFO_DIRS_TOTAL), (long)tot_dirs, (long)free_bytes);
        }
        return 0;
    }

    /* Open directory */
    d = libcmd_opendir(dir_path);
    if (d == NULL) {
        fprintf(stderr, "%s", cmd_gettext(MSG_INFO_FILE_NOT_FOUND));
        return 1;
    }

    /* If we need to chdir to see the stat results properly */
    {
        char old_cwd[CMD_MAX_PATH];
        libcmd_getcwd(old_cwd, sizeof(old_cwd));
        libcmd_chdir(dir_path);

        /* Read all entries */
        while (libcmd_readdir(d, &entry) == 0) {
            libcmd_dirent_t *e;

            /* Apply pattern filter */
            if (strcmp(pattern, "*") != 0) {
                /* Simple glob match */
                libcmd_glob_result_t gr;
                char test[CMD_MAX_PATH];
                int found = 0;
                size_t gi;
                libcmd_sprintf_s(test, sizeof(test), "%s", entry.name);
                if (libcmd_glob(pattern, &gr) == 0) {
                    for (gi = 0; gi < gr.count; gi++) {
                        if (libcmd_strcasecmp(gr.paths[gi], test) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    libcmd_glob_free(&gr);
                    if (!found) continue;
                }
            }

            if (!o.have_attrib && !hidden_filter(&entry))
                continue;
            if (o.have_attrib && !attrib_filter(&entry, o.attrib))
                continue;

            e = (libcmd_dirent_t *)malloc(sizeof(libcmd_dirent_t));
            if (!e) break;
            *e = entry;

            if (o.lower) {
                /* Convert name to lowercase */
                char *p;
                for (p = e->name; *p; p++)
                    *p = (char)tolower((unsigned char)*p);
            }

            if (entry_count >= entry_alloc) {
                libcmd_dirent_t **tmp;
                entry_alloc = entry_alloc ? entry_alloc * 2 : 32;
                tmp = (libcmd_dirent_t **)realloc(entries,
                                                   entry_alloc * sizeof(void *));
                if (!tmp) { free(e); break; }
                entries = tmp;
            }
            entries[entry_count++] = e;

            if (!entry.is_dir)
                total_size += entry.size;
        }

        libcmd_chdir(old_cwd);
    }
    libcmd_closedir(d);

    /* Sort entries */
    g_sort_opts = &o;
    if (entries && entry_count > 0)
        qsort(entries, entry_count, sizeof(void *), entry_cmp);

    /* Print header */
    if (!o.bare) {
        char abs_dir[CMD_MAX_PATH];
        libcmd_path_abs(dir_path, abs_dir, sizeof(abs_dir));
        printf(cmd_gettext(MSG_INFO_DIRECTORY_OF), abs_dir);
    }

    /* Print entries */
    if (o.wide) {
        /* Wide format: 5 columns */
        int col = 0;
        size_t j;
        for (j = 0; j < entry_count; j++) {
            libcmd_dirent_t *e = entries[j];
            if (e->is_dir)
                printf("[%-14s]", e->name);
            else
                printf("%-16s", e->name);
            if (++col % 5 == 0) {
                fputc('\n', stdout);
                maybe_pause(&lines, &o);
            }
        }
        if (col % 5 != 0) fputc('\n', stdout);
    } else if (o.bare) {
        size_t j;
        for (j = 0; j < entry_count; j++) {
            printf("%s\n", entries[j]->name);
            maybe_pause(&lines, &o);
        }
    } else {
        /* Long format */
        size_t j;
        long dir_count = 0, file_count = 0;
        for (j = 0; j < entry_count; j++) {
            libcmd_dirent_t *e = entries[j];
            const libcmd_time_t *tm = pick_time(e, &o);

            print_time(tm, &o);

            if (o.show_owner) {
                struct passwd *pw = getpwuid((uid_t)e->uid);
                printf("  %-10s", pw ? pw->pw_name : "?");
            }

            if (e->is_dir) {
                printf("    <DIR>          %-s\n", e->name);
                dir_count++;
            } else {
                printf("  %14ld %-s\n", (long)e->size, e->name);
                file_count++;
            }
            maybe_pause(&lines, &o);
        }
        /* Summary */
        printf(cmd_gettext(MSG_INFO_FILES_TOTAL),
               (long)file_count, (long)total_size);

        libcmd_get_disk_free(dir_path, &free_bytes, &total_bytes);
        printf(cmd_gettext(MSG_INFO_DIRS_TOTAL),
               (long)dir_count, (long)free_bytes);
        (void)total_bytes;
    }

    /* Free entries */
    {
        size_t j;
        for (j = 0; j < entry_count; j++)
            free(entries[j]);
        free(entries);
    }

    return 0;
}
