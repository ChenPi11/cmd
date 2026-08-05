/*
 * cvars.c - variable expansion
 *
 * Handles:
 *   %VAR%           - environment variable expansion
 *   %0 .. %9        - batch file argument expansion
 *   %~0 .. %~9      - batch argument modifiers (f, d, p, n, x, s, a, t, z)
 *   %DATE%, %TIME%  - dynamic variables
 *   %ERRORLEVEL%    - last exit code
 *   %RANDOM%        - random number 0-32767
 *   %CD%            - current directory
 *   %CMDEXTVERSION% - command extensions version
 *   %CMDCMDLINE%    - original command line
 *   !VAR!           - delayed expansion (when ctx->delayed_expand)
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Dynamic variable helpers
 * ---------------------------------------------------------------------- */

static void get_date_string(char *buf, size_t size)
{
    libcmd_time_t t;
    libcmd_get_local_time(&t);
    libcmd_sprintf_s(buf, size, "%02d/%02d/%04d",
                     t.month, t.day, t.year);
}

static void get_time_string(char *buf, size_t size)
{
    libcmd_time_t t;
    libcmd_get_local_time(&t);
    libcmd_sprintf_s(buf, size, "%02d:%02d:%02d.%02d",
                     t.hour, t.minute, t.second, t.ms / 10);
}

/* -------------------------------------------------------------------------
 * Lookup a single variable name; returns heap-allocated value or NULL
 * ---------------------------------------------------------------------- */

char *lookup_var(cmd_context_t *ctx, const char *name, size_t name_len)
{
    char var_name[256];
    const char *val;

    if (name_len == 0 || name_len >= sizeof(var_name))
        return NULL;

    memcpy(var_name, name, name_len);
    var_name[name_len] = '\0';

    /* --- Dynamic variables (case-insensitive) --- */
    if (libcmd_strcasecmp(var_name, "CD") == 0) {
        return libcmd_getcwd(NULL, 0);
    }
    if (libcmd_strcasecmp(var_name, "DATE") == 0) {
        char buf[64];
        get_date_string(buf, sizeof(buf));
        return libcmd_strdup(buf);
    }
    if (libcmd_strcasecmp(var_name, "TIME") == 0) {
        char buf[64];
        get_time_string(buf, sizeof(buf));
        return libcmd_strdup(buf);
    }
    if (libcmd_strcasecmp(var_name, "RANDOM") == 0) {
        char buf[16];
        libcmd_sprintf_s(buf, sizeof(buf), "%d", libcmd_random());
        return libcmd_strdup(buf);
    }
    if (libcmd_strcasecmp(var_name, "ERRORLEVEL") == 0) {
        char buf[16];
        libcmd_sprintf_s(buf, sizeof(buf), "%d", ctx->exit_code);
        return libcmd_strdup(buf);
    }
    if (libcmd_strcasecmp(var_name, "CMDEXTVERSION") == 0) {
        return libcmd_strdup("2");
    }
    if (libcmd_strcasecmp(var_name, "CMDCMDLINE") == 0) {
        /* The original command line this cmd was started with */
        if (ctx->cmdline[0])
            return libcmd_strdup(ctx->cmdline);
        return libcmd_strdup("cmd");
    }
    if (libcmd_strcasecmp(var_name, "HIGHESTNUMANODENUMBER") == 0) {
        return libcmd_strdup("0");
    }

    /* --- Substring / replacement modifiers --- */
    /* %VAR:str1=str2% and %VAR:~n,m% handled after basic lookup */

    val = libcmd_getenv(var_name);
    if (val)
        return libcmd_strdup(val);

    return NULL; /* undefined */
}

/* -------------------------------------------------------------------------
 * Print all dynamic (built-in) variables matching a prefix.
 * If prefix is NULL or empty, prints all dynamic variables.
 * Returns the number of variables printed.
 * ---------------------------------------------------------------------- */

int cmd_print_dynamic_vars(cmd_context_t *ctx, const char *prefix)
{
    static const char *dyn_names[] = {
        "CD", "DATE", "TIME", "RANDOM", "ERRORLEVEL",
        "CMDEXTVERSION", "CMDCMDLINE", "HIGHESTNUMANODENUMBER",
        NULL
    };
    size_t plen = prefix ? strlen(prefix) : 0;
    int i;
    int printed = 0;

    for (i = 0; dyn_names[i] != NULL; i++) {
        char *val;

        if (plen > 0 &&
            libcmd_strncasecmp(dyn_names[i], prefix, plen) != 0)
            continue;

        val = lookup_var(ctx, dyn_names[i], strlen(dyn_names[i]));
        if (val) {
            printf("%s=%s\n", dyn_names[i], val);
            free(val);
            printed++;
        }
    }

    return printed;
}

/* Apply :~n[,m] substring modifier */
static char *apply_substr(const char *val, const char *modifier)
{
    /* modifier is the part after ':~', e.g. "10,5" or "-3" or "0,-2" */
    long start, length;
    long vlen;
    char *end_ptr;

    if (val == NULL)
        return libcmd_strdup("");

    vlen  = (long)strlen(val);
    start = strtol(modifier, &end_ptr, 10);
    if (*end_ptr == ',') {
        length = strtol(end_ptr + 1, NULL, 10);
    } else {
        length = -99999; /* means "to end" */
    }

    /* Negative start: count from end */
    if (start < 0) {
        start = vlen + start;
        if (start < 0) start = 0;
    }
    if (start >= vlen)
        return libcmd_strdup("");

    /* Negative length: trim from end */
    if (length == -99999) {
        /* No length specified: to end */
        return libcmd_strdup(val + start);
    } else if (length < 0) {
        length = vlen - start + length;
        if (length <= 0)
            return libcmd_strdup("");
    }
    if (start + length > vlen)
        length = vlen - start;
    if (length <= 0)
        return libcmd_strdup("");

    return libcmd_strndup(val + start, (size_t)length);
}

/* Apply :str1=str2 replacement modifier */
static char *apply_replace(const char *val, const char *str1, const char *str2)
{
    const char *p;
    char *result;
    size_t len1, len2, result_size;
    char *out;
    size_t out_pos;

    if (val == NULL || str1 == NULL)
        return libcmd_strdup(val ? val : "");

    len1 = strlen(str1);
    len2 = str2 ? strlen(str2) : 0;

    if (len1 == 0)
        return libcmd_strdup(val);

    /* Allocate conservatively */
    result_size = strlen(val) * (len2 > len1 ? len2 / len1 + 2 : 1) + 256;
    result = (char *)malloc(result_size);
    if (result == NULL)
        return NULL;

    p       = val;
    out     = result;
    out_pos = 0;

    while (*p) {
        if (libcmd_strncasecmp(p, str1, len1) == 0) {
            if (str2) {
                if (out_pos + len2 >= result_size) {
                    char *tmp;
                    result_size = (out_pos + len2 + 1) * 2;
                    tmp = (char *)realloc(result, result_size);
                    if (!tmp) { free(result); return NULL; }
                    result = tmp;
                    out = result;
                }
                memcpy(result + out_pos, str2, len2);
                out_pos += len2;
            }
            p += len1;
        } else {
            if (out_pos + 1 >= result_size) {
                char *tmp;
                result_size = (out_pos + 2) * 2;
                tmp = (char *)realloc(result, result_size);
                if (!tmp) { free(result); return NULL; }
                result = tmp;
                out = result;
            }
            result[out_pos++] = *p++;
        }
    }
    result[out_pos] = '\0';
    (void)out;
    return result;
}

/* -------------------------------------------------------------------------
 * Batch argument modifier expansion: %~[modifiers][digit]
 * ---------------------------------------------------------------------- */

static char *expand_batch_arg_mod(cmd_context_t *ctx, const char *mods, int argnum)
{
    const char *arg;
    char path[CMD_MAX_PATH];
    int has_f = 0, has_d = 0, has_p = 0, has_n = 0, has_x = 0;
    int has_s = 0, has_a = 0, has_t = 0, has_z = 0;
    const char *m;

    /* Get the raw argument */
    if (ctx->call_depth == 0)
        return libcmd_strdup("");

    {
        cmd_call_frame_t *frame = ctx->call_stack[ctx->call_depth - 1];
        int idx = argnum + frame->shift_count;
        if (idx < 0 || idx >= CMD_BATCH_ARGS || frame->args[idx] == NULL)
            return libcmd_strdup("");
        arg = frame->args[idx];
    }

    if (mods == NULL || mods[0] == '\0')
        return libcmd_strdup(arg);

    /* Parse modifier letters */
    for (m = mods; *m && *m != '$'; m++) {
        switch (tolower((unsigned char)*m)) {
        case 'f': has_f = 1; break;
        case 'd': has_d = 1; break;
        case 'p': has_p = 1; break;
        case 'n': has_n = 1; break;
        case 'x': has_x = 1; break;
        case 's': has_s = 1; break;
        case 'a': has_a = 1; break;
        case 't': has_t = 1; break;
        case 'z': has_z = 1; break;
        default:  break;
        }
    }

    /* File metadata modifiers (a/t/z) need a stat */
    if (has_a || has_t || has_z) {
        libcmd_stat_t st;
        char out[CMD_MAX_PATH];
        int opos = 0;

        if (libcmd_stat(arg, &st, 1) == 0) {
            if (has_a) {
                /* Attribute string: d r h a s l (dir/ro/hidden/archive/
                 * system/link); archive & system have no Unix equivalent */
                out[opos++] = st.is_dir      ? 'd' : '-';
                out[opos++] = (st.mode & 0200) ? '-' : 'r';
                out[opos++] = (arg[0] == '.') ? 'h' : '-';
                out[opos++] = '-'; /* archive */
                out[opos++] = '-'; /* system */
                out[opos++] = st.is_link     ? 'l' : '-';
            }
            if (has_t) {
                /* Timestamp, cmd style: MM/DD/YYYY hh:mm AM/PM */
                int h12 = st.mtime.hour % 12;
                if (h12 == 0) h12 = 12;
                opos += libcmd_sprintf_s(out + opos, sizeof(out) - (size_t)opos,
                                         "%02d/%02d/%04d %02d:%02d %s",
                                         st.mtime.month, st.mtime.day,
                                         st.mtime.year, h12, st.mtime.minute,
                                         st.mtime.hour < 12 ? "AM" : "PM");
            }
            if (has_z) {
                if (opos > 0 && opos < (int)sizeof(out) - 1)
                    out[opos++] = ' ';
                opos += libcmd_sprintf_s(out + opos, sizeof(out) - (size_t)opos,
                                         "%ld", (long)st.size);
            }
            out[opos] = '\0';
            return libcmd_strdup(out);
        }
        return libcmd_strdup("");
    }

    /* %~s: short (8.3) names do not exist on Unix; the path is
     * returned unchanged */
    if (has_s && !has_f && !has_d && !has_p && !has_n && !has_x)
        return libcmd_strdup(arg);

    /* Get full absolute path if any modifier requires it */
    if (has_f || has_d || has_p || has_n || has_x) {
        if (libcmd_path_abs(arg, path, sizeof(path)) < 0)
            libcmd_sprintf_s(path, sizeof(path), "%s", arg);
    } else {
        libcmd_sprintf_s(path, sizeof(path), "%s", arg);
    }

    /* Apply modifiers */
    if (has_d && has_p) {
        /* Directory with trailing slash */
        char dir[CMD_MAX_PATH];
        libcmd_path_dirname(path, dir, sizeof(dir));
        libcmd_sprintf_s(path, sizeof(path), "%s/", dir);
        return libcmd_strdup(path);
    }
    if (has_d) {
        /* Drive / root only (Unix: no drive, return "/") */
        return libcmd_strdup("/");
    }
    if (has_p) {
        char dir[CMD_MAX_PATH];
        libcmd_path_dirname(path, dir, sizeof(dir));
        return libcmd_strdup(dir);
    }
    if (has_n && has_x) {
        return libcmd_strdup(libcmd_path_basename(path));
    }
    if (has_n) {
        const char *base = libcmd_path_basename(path);
        const char *ext  = libcmd_path_ext(base);
        return libcmd_strndup(base, (size_t)(ext - base));
    }
    if (has_x) {
        return libcmd_strdup(libcmd_path_ext(path));
    }
    if (has_f) {
        return libcmd_strdup(path);
    }

    return libcmd_strdup(arg);
}

/* Join all batch arguments (%1..%9) with single spaces into a new
 * heap string.  The original list is used, ignoring shift_count,
 * matching cmd.exe where %* is not affected by SHIFT. */
static char *batch_args_all(cmd_context_t *ctx)
{
    cmd_call_frame_t *frame = ctx->call_stack[ctx->call_depth - 1];
    size_t len = 0;
    size_t n = 0;
    size_t i;
    char *buf, *p;

    for (i = 1; i < CMD_BATCH_ARGS; i++) {
        if (frame->args[i] == NULL)
            break;
        if (n > 0)
            len++;
        len += strlen(frame->args[i]);
        n++;
    }

    buf = (char *)malloc(len + 1);
    if (buf == NULL)
        return NULL;

    p = buf;
    n = 0;
    for (i = 1; i < CMD_BATCH_ARGS; i++) {
        size_t alen;
        if (frame->args[i] == NULL)
            break;
        if (n > 0)
            *p++ = ' ';
        alen = strlen(frame->args[i]);
        memcpy(p, frame->args[i], alen);
        p += alen;
        n++;
    }
    *p = '\0';
    return buf;
}

/* -------------------------------------------------------------------------
 * Main expansion function
 * ---------------------------------------------------------------------- */

char *cmd_expand_vars(cmd_context_t *ctx, const char *line)
{
    char  out[CMD_MAX_LINE * 4];
    int   opos = 0;
    int   olen = (int)(sizeof(out) - 1);
    const char *p = line;

    if (line == NULL)
        return libcmd_strdup("");

#define EMIT(c) do { if (opos < olen) out[opos++] = (c); } while(0)
#define EMITS(s) do { \
        const char *_s = (s); \
        while (*_s && opos < olen) out[opos++] = *_s++; \
    } while(0)

    while (*p) {
        if (*p == '%') {
            p++;

            /* %% -> literal % */
            if (*p == '%') {
                EMIT('%');
                p++;
                continue;
            }

            /* %~[mods]digit - batch argument with modifiers */
            if (*p == '~') {
                char mods[32];
                int  mpos = 0;
                int  digit = -1;
                const char *q = p + 1;

                while (*q && !isdigit((unsigned char)*q) && mpos < 31)
                    mods[mpos++] = *q++;
                mods[mpos] = '\0';

                if (isdigit((unsigned char)*q)) {
                    digit = *q - '0';
                    q++;
                    p = q;
                    {
                        char *val = expand_batch_arg_mod(ctx, mods, digit);
                        if (val) { EMITS(val); free(val); }
                    }
                    continue;
                }
                /* Not a valid modifier sequence; emit literally */
                EMIT('%');
                continue;
            }

            /* %* - all batch arguments (original list, unaffected by
             * SHIFT, matching cmd.exe semantics) */
            if (*p == '*') {
                p++;
                if (ctx->call_depth > 0) {
                    char *all = batch_args_all(ctx);
                    if (all) { EMITS(all); free(all); }
                }
                continue;
            }

            /* %digit - batch argument */
            if (isdigit((unsigned char)*p)) {
                int digit = *p - '0';
                p++;
                if (ctx->call_depth > 0) {
                    cmd_call_frame_t *frame = ctx->call_stack[ctx->call_depth-1];
                    int idx = digit + frame->shift_count;
                    if (idx >= 0 && idx < CMD_BATCH_ARGS && frame->args[idx])
                        EMITS(frame->args[idx]);
                }
                continue;
            }

            /* %NAME% or %NAME:modifier% */
            {
                const char *name_start = p;
                char        var_name[256];
                int         vpos = 0;
                int         bad_name = 0;
                char       *val  = NULL;

                while (*p && *p != '%' && vpos < 255) {
                    /* Names with spaces are not valid variables (cmd.exe
                     * leaves them literal, which keeps FOR loops like
                     * "%i in (1 2 3) do echo %i" intact) */
                    if (*p == ' ' || *p == '\t')
                        bad_name = 1;
                    var_name[vpos++] = *p++;
                }

                var_name[vpos] = '\0';

                if (*p == '%') {
                    p++; /* consume closing % */

                    if (bad_name) {
                        /* Invalid name: emit '%' literally and rescan the
                         * rest (the closing % may start a real variable) */
                        EMIT('%');
                        p = name_start;
                        continue;
                    }

                    /* Check for modifiers: :~n,m or :str1=str2 */
                    if (strchr(var_name, ':') != NULL) {
                        char *colon;
                        char *modifier;

                        colon = strchr(var_name, ':');
                        *colon = '\0';
                        modifier = colon + 1;

                        val = lookup_var(ctx, var_name, strlen(var_name));
                        if (val == NULL) val = libcmd_strdup("");

                        if (*modifier == '~') {
                            char *sub = apply_substr(val, modifier + 1);
                            free(val);
                            val = sub;
                        } else {
                            /* str1=str2 replacement */
                            char *eq = strchr(modifier, '=');
                            char *tmp;

                            if (eq) {
                                *eq = '\0';
                                tmp = apply_replace(val, modifier, eq + 1);
                                free(val);
                                val = tmp;
                            }
                        }
                    } else {
                        val = lookup_var(ctx, var_name, strlen(var_name));
                    }

                    if (val) { EMITS(val); free(val); }
                } else {
                    /* No closing %, emit as-is */
                    EMIT('%');
                    p = name_start;
                }
            }
            continue;
        }

        /* Delayed expansion: !VAR! */
        if (*p == '!' && ctx->delayed_expand) {
            p++;
            if (*p == '!') {
                EMIT('!');
                p++;
                continue;
            }
            {
                char var_name[256];
                int  vpos = 0;
                char *val;

                while (*p && *p != '!' && vpos < 255)
                    var_name[vpos++] = *p++;

                var_name[vpos] = '\0';

                if (*p == '!') p++;

                val = lookup_var(ctx, var_name, strlen(var_name));
                if (val) { EMITS(val); free(val); }
            }
            continue;
        }

        EMIT(*p);
        p++;
    }

#undef EMIT
#undef EMITS

    out[opos] = '\0';
    return libcmd_strdup(out);
}
