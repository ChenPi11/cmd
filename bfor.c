/*
 * bfor.c - FOR builtin
 *
 * FOR %variable IN (set) DO command
 * FOR /D %variable IN (set) DO command
 * FOR /R [[drive:]path] %variable IN (set) DO command
 * FOR /L %variable IN (start,step,end) DO command
 * FOR /F ["options"] %variable IN (file-set|"string"|'command') DO command
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration */
extern int cmd_run_line(cmd_context_t *ctx, const char *line);
extern char *cmd_expand_vars(cmd_context_t *ctx, const char *line);

/* -------------------------------------------------------------------------
 * Substitute %variable (or %%variable in batch) with values and run cmd.
 * letters[k] holds the k-th variable letter, values[k] its substituted
 * value.  Used by FOR /F "tokens=..." where %i/%j/%k... map to tokens.
 * ---------------------------------------------------------------------- */

static int run_with_vars(cmd_context_t *ctx,
                         const char *cmd_template,
                         const char *letters,
                         const char **values)
{
    char buf[CMD_MAX_LINE];
    const char *p = cmd_template;
    int bpos = 0;

    while (*p && bpos < CMD_MAX_LINE - 1) {
        if (*p == '%') {
            size_t k;
            char next;
            int consumed = 0;

            next = p[1];

            /* Batch form: %%X (X uppercase) */
            if (p[1] == '%' && p[2] != '\0')
                next = (char)toupper((unsigned char)p[2]);

            for (k = 0; letters[k]; k++) {
                if ((char)toupper((unsigned char)next) ==
                    (char)toupper((unsigned char)letters[k])) {
                    const char *v = values[k];
                    while (*v && bpos < CMD_MAX_LINE - 1)
                        buf[bpos++] = *v++;
                    consumed = (p[1] == '%' && p[2] != '\0') ? 3 : 2;
                    break;
                }
            }
            if (consumed) {
                p += consumed;
                continue;
            }
            /* Not a loop variable; keep the '%' for cmd_run_line's own
             * expansion (e.g. %PATH% or a literal %%) */
            buf[bpos++] = '%';
            p++;
            continue;
        }
        buf[bpos++] = *p++;
    }
    buf[bpos] = '\0';

    return cmd_run_line(ctx, buf);
}

static int run_with_var(cmd_context_t *ctx,
                        const char *cmd_template,
                        char var_letter,
                        const char *value)
{
    const char *vals[2];
    char letters[2];

    vals[0] = value;
    vals[1] = NULL;
    letters[0] = (char)toupper((unsigned char)var_letter);
    letters[1] = '\0';
    return run_with_vars(ctx, cmd_template, letters, vals);
}

/* -------------------------------------------------------------------------
 * Parse the (set) content: returns a NULL-terminated array of tokens
 * ---------------------------------------------------------------------- */

static char **parse_set(const char *set_str)
{
    char **items = NULL;
    int count = 0, alloc = 0;
    const char *p = set_str;

    while (*p) {
        char tok[CMD_MAX_PATH];
        int tpos = 0;

        /* Skip spaces */
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (*p == '\0') break;

        /* Read token (may be quoted) */
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && tpos < CMD_MAX_PATH - 1)
                tok[tpos++] = *p++;
            if (*p == '"') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != ',' &&
                   tpos < CMD_MAX_PATH - 1)
                tok[tpos++] = *p++;
        }
        tok[tpos] = '\0';
        if (tpos == 0) continue;

        if (count >= alloc) {
            char **tmp;
            alloc = alloc ? alloc * 2 : 8;
            tmp = (char **)realloc(items, (size_t)(alloc + 1) * sizeof(char *));
            if (!tmp) break;
            items = tmp;
        }
        items[count++] = libcmd_strdup(tok);
    }

    if (items) items[count] = NULL;
    return items;
}

static void free_items(char **items)
{
    int i;
    if (!items) return;
    for (i = 0; items[i]; i++) free(items[i]);
    free(items);
}

/* -------------------------------------------------------------------------
 * FOR variants
 * ---------------------------------------------------------------------- */

/* FOR %X IN (set) DO cmd - iterates over a list of files/patterns */
static int for_basic(cmd_context_t *ctx, char var, const char *set_str,
                     const char *do_cmd, int dirs_only)
{
    char **items = parse_set(set_str);
    int i, ret = 0;

    if (!items) return 1;

    for (i = 0; items[i]; i++) {
        const char *item = items[i];

        if (strchr(item, '*') || strchr(item, '?')) {
            /* Expand glob */
            libcmd_glob_result_t gr;
            if (libcmd_glob(item, &gr) == 0) {
                size_t j;
                for (j = 0; j < gr.count; j++) {
                    if (dirs_only) {
                        libcmd_stat_t st;
                        if (libcmd_stat(gr.paths[j], &st, 1) < 0 || !st.is_dir)
                            continue;
                    }
                    ret = run_with_var(ctx, do_cmd, var, gr.paths[j]);
                    if (ctx->stop_batch || ctx->should_exit) break;
                }
                libcmd_glob_free(&gr);
            }
        } else {
            if (dirs_only) {
                libcmd_stat_t st;
                if (libcmd_stat(item, &st, 1) < 0 || !st.is_dir)
                    continue;
            }
            ret = run_with_var(ctx, do_cmd, var, item);
        }

        if (ctx->stop_batch || ctx->should_exit) break;
    }

    free_items(items);
    return ret;
}

/* FOR /L %X IN (start,step,end) DO cmd */
static int for_loop(cmd_context_t *ctx, char var, const char *set_str,
                    const char *do_cmd)
{
    long start = 0, step = 1, end = 0;
    char buf[256];
    const char *p = set_str;
    char *ep;
    int ret = 0;

    /* Skip leading spaces */
    while (*p == ' ' || *p == '\t') p++;

    start = strtol(p, &ep, 10); p = ep;
    while (*p == ' ' || *p == '\t' || *p == ',') p++;
    step  = strtol(p, &ep, 10); p = ep;
    while (*p == ' ' || *p == '\t' || *p == ',') p++;
    end   = strtol(p, &ep, 10);

    if (step == 0) step = 1;

    if (step > 0) {
        long i;
        for (i = start; i <= end; i += step) {
            libcmd_sprintf_s(buf, sizeof(buf), "%ld", i);
            ret = run_with_var(ctx, do_cmd, var, buf);
            if (ctx->stop_batch || ctx->should_exit) break;
        }
    } else {
        long i;
        for (i = start; i >= end; i += step) {
            libcmd_sprintf_s(buf, sizeof(buf), "%ld", i);
            ret = run_with_var(ctx, do_cmd, var, buf);
            if (ctx->stop_batch || ctx->should_exit) break;
        }
    }

    return ret;
}

/* FOR /R [root] %X IN (set) DO cmd - recursively walk a directory tree.
 * For each directory (starting at root), every file whose name matches one
 * of the patterns in the set is passed to the command as a full path. */
static int for_r_walk(cmd_context_t *ctx, char var, const char *do_cmd,
                      char **patterns, const char *dir)
{
    libcmd_dir_t d;
    libcmd_dirent_t ent;
    int ret = 0;

    if (ctx->stop_batch || ctx->should_exit)
        return 0;

    d = libcmd_opendir(dir);
    if (d == NULL)
        return 0;

    while (libcmd_readdir(d, &ent) == 0) {
        char full[CMD_MAX_PATH * 2];
        int i;

        if (strcmp(ent.name, ".") == 0 || strcmp(ent.name, "..") == 0)
            continue;

        libcmd_sprintf_s(full, sizeof(full), "%s/%s", dir, ent.name);

        if (ent.is_dir) {
            ret = for_r_walk(ctx, var, do_cmd, patterns, full);
            if (ctx->stop_batch || ctx->should_exit)
                break;
        } else {
            for (i = 0; patterns[i]; i++) {
                const char *pat = patterns[i];

                /* A pattern without a path separator matches the bare
                 * file name; one with a separator is matched against
                 * the full path.  Strip a leading "./" on both sides
                 * so a "." root behaves like any other directory
                 * (libcmd_glob() normalizes that prefix away, which is
                 * why comparing against the raw "full" silently matched
                 * nothing). */
                if (strchr(pat, '/') == NULL) {
                    if (libcmd_fnmatch(pat, ent.name) == 0) {
                        ret = run_with_var(ctx, do_cmd, var, full);
                        if (ctx->stop_batch || ctx->should_exit) break;
                    }
                } else {
                    const char *p = pat;
                    const char *f = full;
                    while (p[0] == '.' && p[1] == '/') p += 2;
                    while (f[0] == '.' && f[1] == '/') f += 2;
                    if (libcmd_fnmatch(p, f) == 0) {
                        ret = run_with_var(ctx, do_cmd, var, full);
                        if (ctx->stop_batch || ctx->should_exit) break;
                    }
                }
                if (ctx->stop_batch || ctx->should_exit) break;
            }
            if (ctx->stop_batch || ctx->should_exit) break;
        }
    }

    libcmd_closedir(d);
    return ret;
}

static int for_recursive(cmd_context_t *ctx, char var, const char *root,
                         const char *set_str, const char *do_cmd)
{
    char **patterns = parse_set(set_str);
    int ret;

    if (patterns == NULL)
        return 1;

    if (root == NULL || root[0] == '\0') {
        char cwd[CMD_MAX_PATH];
        if (libcmd_getcwd(cwd, sizeof(cwd)))
            root = cwd;
        else
            root = ".";
    }

    ret = for_r_walk(ctx, var, do_cmd, patterns, root);

    free_items(patterns);
    return ret;
}

/* Substitute the requested tokens of one line into do_cmd and run it.
 * tokens[] are 1-based token numbers; -1 means "rest of the line"
 * (everything after the last explicitly requested token, verbatim apart
 * from leading delimiters). */
static int for_f_process_line(cmd_context_t *ctx, char var,
                              const char *do_cmd, const char *delims,
                              const int *tokens, int ntokens,
                              const char *text)
{
    char  sbuf[CMD_MAX_LINE];
    char *tstart[256];  /* token start pointers into sbuf */
    char *tend[256];    /* token end pointers (one past last char) */
    char *vals[32];
    char *to_free[32];
    char  letters[32];
    int   nt = 0;
    int   k, nreq;

    (void)libcmd_sprintf_s(sbuf, sizeof(sbuf), "%s", text);

    /* Manual span-based tokenization so the "*" (rest) token can take
     * the verbatim remainder of the line */
    {
        char *p = sbuf;
        while (*p) {
            char *s;
            while (*p && strchr(delims, *p)) p++;
            if (!*p) break;
            s = p;
            while (*p && !strchr(delims, *p)) p++;
            if (nt < 255) {
                tstart[nt] = s;
                tend[nt]   = p;
                nt++;
            }
        }
    }

    nreq = (ntokens > 0) ? ntokens : 1;
    if (nreq > 32) nreq = 32;

    {
        int last_explicit = -1; /* token number of last explicit request */

        for (k = 0; k < nreq; k++) {
            int idx = (ntokens > 0) ? tokens[k] : 1;

            letters[k] = (char)(var + (char)k);
            vals[k] = NULL;
            to_free[k] = NULL;

            if (idx > last_explicit)
                last_explicit = idx;

            if (idx < 0) {
                /* rest of the line after the last explicit token */
                const char *start = text;
                int idx2 = last_explicit;
                if (idx2 <= 0)
                    start = text;
                else if (idx2 <= nt)
                    start = tend[idx2 - 1];
                else
                    start = text + strlen(text); /* past end: empty */
                while (*start && strchr(delims, *start)) start++;
                vals[k] = libcmd_strdup(start);
                to_free[k] = (char *)vals[k];
            } else if (idx == 0) {
                vals[k] = (char *)text;
            } else if (idx <= nt) {
                size_t len = (size_t)(tend[idx - 1] - tstart[idx - 1]);
                vals[k] = libcmd_strndup(tstart[idx - 1], len);
                to_free[k] = vals[k];
            } else {
                vals[k] = "";
            }
        }
    }
    vals[nreq] = NULL;
    letters[nreq] = '\0';

    {
        int ret = run_with_vars(ctx, do_cmd, letters, (const char **)vals);
        for (k = 0; k < nreq; k++)
            free(to_free[k]);
        return ret;
    }
}

/* FOR /F ["options"] %X IN (filespec) DO cmd */
static int for_f(cmd_context_t *ctx, char var, const char *options_str,
                 const char *set_str, const char *do_cmd)
{
    char eol_char  = ';';
    int  skip      = 0;
    char delims[64] = " \t";
    int  tokens[32];   /* 1-based token numbers; -1 = rest of line (*) */
    int  ntokens   = 0; /* 0 = default (first token only) */
    int  usebackq  = 0;

    int  ret = 0;
    FILE *src = NULL;
    int   is_popen = 0;  /* 1 if src was opened with libcmd_popen */
    int   is_string = 0;
    char  line[CMD_MAX_LINE];

    /* Parse options string */
    if (options_str && *options_str) {
        const char *p = options_str;
        while (*p) {
            while (*p == ' ' || *p == '\t') p++;
            if (libcmd_strncasecmp(p, "eol=", 4) == 0) {
                p += 4;
                eol_char = *p ? *p++ : ';';
            } else if (libcmd_strncasecmp(p, "skip=", 5) == 0) {
                p += 5;
                skip = (int)strtol(p, (char **)&p, 10);
            } else if (libcmd_strncasecmp(p, "delims=", 7) == 0) {
                int dpos;
                p += 7;
                dpos = 0;
                while (*p && *p != ' ' && dpos < 63)
                    delims[dpos++] = *p++;
                delims[dpos] = '\0';
            } else if (libcmd_strncasecmp(p, "tokens=", 7) == 0) {
                p += 7;
                while (*p && ntokens < 32) {
                    char *ep;
                    long n;
                    while (*p == ',' || *p == ' ' || *p == '\t') p++;
                    if (*p == '\0') break;
                    if (*p == '*') {
                        tokens[ntokens++] = -1;
                        p++;
                        continue;
                    }
                    n = strtol(p, &ep, 10);
                    p = ep;
                    if (*p == '-') {
                        long m;
                        p++;
                        m = strtol(p, &ep, 10);
                        p = ep;
                        for (; n <= m && ntokens < 32; n++)
                            tokens[ntokens++] = (int)n;
                    } else {
                        if (ntokens < 32)
                            tokens[ntokens++] = (int)n;
                    }
                }
            } else if (libcmd_strncasecmp(p, "usebackq", 8) == 0) {
                usebackq = 1;
                p += 8;
            } else {
                /* Skip unknown */
                while (*p && *p != ' ' && *p != '\t') p++;
            }
        }
    }

    /* Determine input source.
     * Default:  "..." = string, '...' = command,  unquoted = file
     * usebackq: '...' = string, "..." = file (spaces allowed), `...` = cmd
     */
    {
        const char *s = set_str;
        while (*s == ' ' || *s == '\t') s++;

        if (!usebackq && *s == '"') {
            /* String literal */
            is_string = 1;
        } else if (usebackq && *s == '\'') {
            /* String literal (single quotes) */
            is_string = 1;
        } else if ((!usebackq && *s == '\'') ||
                   (usebackq && *s == '`')) {
            /* Command output */
            char cmd_buf[CMD_MAX_LINE];
            char close = (!usebackq) ? '\'' : '`';
            size_t clen = strlen(s);
            if (clen > 2 && s[clen-1] == close) {
                memcpy(cmd_buf, s + 1, clen - 2);
                cmd_buf[clen - 2] = '\0';
            } else {
                libcmd_sprintf_s(cmd_buf, sizeof(cmd_buf), "%s", s + 1);
            }
            src = libcmd_popen(cmd_buf, "r");
            if (!src) return 1;
            is_popen = 1;
        } else if (usebackq && *s == '"') {
            /* File name, possibly containing spaces */
            char fbuf[CMD_MAX_LINE];
            size_t flen = strlen(s);
            if (flen >= 2 && s[flen-1] == '"') flen--;
            memcpy(fbuf, s + 1, flen - 1);
            fbuf[flen - 1] = '\0';
            src = fopen(fbuf, "r");
            if (!src) {
                fprintf(stderr, cmd_gettext(MSG_ERR_FOR_OPEN), fbuf);
                return 1;
            }
        } else {
            /* File */
            src = fopen(s, "r");
            if (!src) {
                fprintf(stderr, cmd_gettext(MSG_ERR_FOR_OPEN), s);
                return 1;
            }
        }
    }

    /* Process one line: substitute the requested tokens into the command.
     * tokens[] are 1-based; -1 means "rest of the line" (joined with
     * spaces).  Additional variables continue the letter sequence: with
     * %i, tokens=1,3 maps to %i and %j. */
    if (is_string) {
        const char *s = set_str;
        char text[CMD_MAX_LINE];
        size_t slen;

        while (*s == ' ' || *s == '\t') s++;
        if (*s == '"' || *s == '\'') s++;
        slen = strlen(s);
        while (slen > 0 && (s[slen-1] == '"' || s[slen-1] == '\''))
            slen--;
        memcpy(text, s, slen);
        text[slen] = '\0';

        {
            const char *p = text;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != eol_char)
                ret = for_f_process_line(ctx, var, do_cmd, delims,
                                         tokens, ntokens, text);
        }
    } else {
        int line_num = 0;
        while (fgets(line, sizeof(line), src) != NULL) {
            const char *p;
            size_t len;

            line_num++;
            if (line_num <= skip) continue;

            /* Strip newline */
            len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                line[--len] = '\0';

            /* Check EOL character */
            p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == eol_char) continue;

            ret = for_f_process_line(ctx, var, do_cmd, delims,
                                     tokens, ntokens, line);
            if (ctx->stop_batch || ctx->should_exit) break;
        }
        if (is_popen)
            libcmd_pclose(src);
        else
            fclose(src);
    }

    return ret;
}

/* -------------------------------------------------------------------------
 * Main FOR dispatcher
 * ---------------------------------------------------------------------- */

int builtin_for(cmd_context_t *ctx, int argc, char **argv)
{
    int  argi = 1;
    int  opt_d = 0, opt_r = 0, opt_l = 0, opt_f = 0;
    char options_buf[256];
    char var_letter = 'I';
    const char *set_str = NULL;
    const char *do_cmd  = NULL;
    const char *root    = NULL;
    int i;

    options_buf[0] = '\0';

    if (argc < 2 || libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_FOR), stdout);
        return 0;
    }

    /* Parse optional /D /R /L /F flags */
    while (argi < argc && argv[argi][0] == '/') {
        char opt = (char)toupper((unsigned char)argv[argi][1]);
        if (opt == 'D') { opt_d = 1; argi++; }
        else if (opt == 'R') { opt_r = 1; argi++; }
        else if (opt == 'L') { opt_l = 1; argi++; }
        else if (opt == 'F') {
            opt_f = 1;
            argi++;
            /* Optional options string.  The tokenizer strips the
             * surrounding quotes, so a multi-word option list arrives
             * as several arguments; join them until the variable. */
            while (argi < argc && argv[argi][0] != '%' &&
                   libcmd_strcasecmp(argv[argi], "IN") != 0) {
                if (options_buf[0] != '\0')
                    strncat(options_buf, " ",
                            sizeof(options_buf) - strlen(options_buf) - 1);
                strncat(options_buf, argv[argi],
                        sizeof(options_buf) - strlen(options_buf) - 1);
                argi++;
            }
        }
        else break;
    }

    /* Command extensions disabled (/e:off): /D /R /L /F are extensions */
    if (!ctx->extensions && (opt_d || opt_r || opt_l || opt_f)) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_FOR_EXTENSIONS));
        return 1;
    }

    /* /R: optional root path before the variable */
    if (opt_r && argi < argc && argv[argi][0] != '%') {
        root = argv[argi++];
    }

    /* %variable */
    if (argi >= argc) goto syntax_err;
    {
        const char *varg = argv[argi++];
        if (varg[0] == '%' && varg[1] != '\0') {
            var_letter = (char)toupper((unsigned char)varg[1]);
        } else {
            var_letter = (char)toupper((unsigned char)varg[0]);
        }
    }

    /* IN keyword */
    if (argi >= argc || libcmd_strcasecmp(argv[argi], "IN") != 0) goto syntax_err;
    argi++;

    /* (set) - find the opening ( and closing ) */
    if (argi >= argc || argv[argi][0] != '(') goto syntax_err;
    {
        char set_buf[CMD_MAX_LINE];
        int spos = 0;
        int depth = 0;
        int in_set = 0;
        const char *p;

        set_buf[0] = '\0';
        for (i = argi; i < argc && !in_set; i++) {
            p = argv[i];
            while (*p) {
                if (*p == '(') {
                    if (depth++ == 0) { p++; continue; }
                } else if (*p == ')') {
                    if (--depth == 0) { in_set = 1; argi = i + 1; break; }
                }
                if (spos < CMD_MAX_LINE - 1)
                    set_buf[spos++] = *p;
                p++;
            }
            if (!in_set && spos > 0 && spos < CMD_MAX_LINE - 1)
                set_buf[spos++] = ' ';
        }
        set_buf[spos] = '\0';
        set_str = libcmd_strdup(set_buf);
    }

    /* DO keyword */
    if (argi >= argc || libcmd_strcasecmp(argv[argi], "DO") != 0) goto syntax_err;
    argi++;

    /* Build do_cmd from remaining args */
    {
        char buf[CMD_MAX_LINE];
        buf[0] = '\0';
        for (i = argi; i < argc; i++) {
            if (i > argi) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
        }
        do_cmd = libcmd_strdup(buf);
    }

    if (set_str == NULL || do_cmd == NULL) {
        free((char *)set_str);
        free((char *)do_cmd);
        return 1;
    }

    {
        int ret;
        if (opt_l)
            ret = for_loop(ctx, var_letter, set_str, do_cmd);
        else if (opt_f)
            ret = for_f(ctx, var_letter, options_buf, set_str, do_cmd);
        else if (opt_r)
            ret = for_recursive(ctx, var_letter, root, set_str, do_cmd);
        else
            ret = for_basic(ctx, var_letter, set_str, do_cmd, opt_d);

        free((char *)set_str);
        free((char *)do_cmd);
        return ret;
    }

syntax_err:
    fprintf(stderr, "%s", cmd_gettext(MSG_ERR_FOR_SYNTAX));
    free((char *)set_str);
    free((char *)do_cmd);
    return 1;
}
