/*
 * cmain.c - entry point
 *
 * Parses command-line arguments and starts the interpreter.
 *
 * Usage:
 *   cmd [/c|/k] [/s] [/q] [/d] [/a|/u] [/t:{bf|f}]
 *       [/e:{on|off}] [/f:{on|off}] [/v:{on|off}] [string]
 *
 * License: GNU GPLv3
 */

#define _CRT_SECURE_NO_WARNINGS

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * Argument parsing
 * ---------------------------------------------------------------------- */

typedef struct {
    int  run_then_exit;   /* /c */
    int  run_then_keep;   /* /k */
    int  special_parse;   /* /s */
    int  quiet;           /* /q */
    int  no_autorun;      /* /d */
    int  ansi_output;     /* /a */
    int  unicode_output;  /* /u */
    int  fg_color;        /* /t foreground nibble or -1 */
    int  bg_color;        /* /t background nibble or -1 */
    int  extensions;      /* 1=on 0=off from /e:on|off  */
    int  file_complete;   /* 1=on 0=off from /f:on|off  */
    int  delayed_expand;  /* 1=on 0=off from /v:on|off  */
    char *cmd_string;     /* string to execute (if /c or /k) */
    char *batch_file;     /* batch file path (if present) */
    int   batch_argc;     /* number of batch file arguments */
    char **batch_argv;    /* batch file argument array      */
    int  show_help;       /* /? */
} cmd_opts_t;

static void print_usage(void)
{
    fputs(cmd_gettext(MSG_HELP_CMD), stdout);
}

static int parse_on_off(const char *s)
{
    if (libcmd_strcasecmp(s, "on") == 0)  return 1;
    if (libcmd_strcasecmp(s, "off") == 0) return 0;
    return -1;
}

static int parse_color_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Return a malloc'ed copy of the raw command line starting right after
 * the flag token matched by ARGV[IDXP] (e.g. /C), or NULL when the raw
 * line is unavailable or does not line up with the CRT argv.  Real
 * cmd.exe takes its /C string from this text: launchers such as
 * PowerShell pass arguments containing quotes unescaped
 * ("echo Q=find "stdio" tail"), and the CRT argv rules fold those
 * quotes away, losing them before the parser ever sees them. */
#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)
static char *raw_tail_after_flag(int argc, char **argv, int idxp)
{
    const char *raw = libcmd_get_raw_cmdline();
    const char *p;
    int i;

    if (raw == NULL || idxp <= 0 || idxp >= argc)
        return NULL;

    /* Skip the argv[0] token, which may be quoted. */
    p = raw;
    if (*p == '"') {
        p++;
        while (*p != '\0' && *p != '"')
            p++;
        if (*p == '"')
            p++;
    }
    while (*p != '\0' && *p != ' ' && *p != '\t')
        p++;

    /* Pair each flag token, argv[1 .. IDXP-1], with its raw-text
     * counterpart; a mismatch means the two tokenizations diverge
     * (quotes around flags or trailing junk), so fall back to the
     * argv join.  The string itself (argv[IDXP..]) is never compared:
     * raw quoting there is exactly what must be preserved. */
    for (i = 1; i < idxp; i++) {
        const char *tok;
        size_t tlen;
        size_t alen = strlen(argv[i]);

        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            return NULL;

        tok = p;
        if (*p == '"') {
            tok++;
            p++;
            while (*p != '\0' && *p != '"')
                p++;
            tlen = (size_t)(p - tok);
            if (*p == '"')
                p++;
            /* A quoted flag with text glued after it re-merges into
             * one argument only under rules argv may follow later;
             * be conservative. */
            if (*p != '\0' && *p != ' ' && *p != '\t')
                return NULL;
        } else {
            while (*p != '\0' && *p != ' ' && *p != '\t')
                p++;
            tlen = (size_t)(p - tok);
        }

        if (tlen != alen || memcmp(tok, argv[i], alen) != 0)
            return NULL;
    }

    while (*p == ' ' || *p == '\t')
        p++;
    return libcmd_strdup(p);
}
#endif

/* Real cmd.exe quote handling for the /C (and /K) string, per its
 * documented "modification to the /S and /C semantics": when the string
 * begins with a quote, the first and the last quote character are
 * removed (preserving any text after the last one) unless ALL of these
 * hold at once: no /S switch, exactly two quote characters, no special
 * characters between them (& < > ( ) @ ^ |), one or more whitespace
 * characters between them, and the text between them names an existing
 * executable file.  FORCE_STRIP is the /S switch. */
static void apply_c_quote_rule(char *buf, int force_strip)
{
    const char *s;
    char *lastq;
    size_t len;
    int n_quotes = 0;

    if (buf == NULL || buf[0] != '"')
        return;
    len = strlen(buf);
    if (len < 2)
        return;

    for (s = buf; *s != '\0'; s++)
        if (*s == '"')
            n_quotes++;

    if (!force_strip && n_quotes == 2) {
        const char *c;
        const char *q2 = strchr(buf + 1, '"');
        int ok_ws   = 0;
        int ok_spec = 1;

        if (q2 != NULL && q2 > buf + 1) {
            size_t mlen = (size_t)(q2 - (buf + 1));

            for (c = buf + 1; c != q2; c++) {
                if (*c == ' ' || *c == '\t')
                    ok_ws = 1;
                else if (strchr("&<>()@^|", *c) != NULL)
                    ok_spec = 0;
            }
            if (ok_ws && ok_spec) {
                char exe[CMD_MAX_PATH];
                char where[CMD_MAX_PATH];

                if (mlen < sizeof(exe)) {
                    memcpy(exe, buf + 1, mlen);
                    exe[mlen] = '\0';
                    if (libcmd_find_exec(exe, NULL, where,
                                         sizeof(where)) == 0)
                        return;   /* preserve the quote pair */
                }
            }
        }
    }

    /* Strip the leading quote and the last quote on the line,
     * keeping everything that sits between and after them. */
    lastq = strrchr(buf, '"');
    if (lastq != NULL && lastq != buf) {
        /* Bytes between the leading quote and the last quote, then
         * whatever followed the last quote. */
        size_t nlead = (size_t)(lastq - buf) - 1;

        memmove(buf, buf + 1, nlead);
        memmove(buf + nlead, lastq + 1, strlen(lastq + 1) + 1);
    } else {
        memmove(buf, buf + 1, len);
    }
}

static void parse_args(int argc, char **argv, cmd_opts_t *opts)
{
    int i;

    memset(opts, 0, sizeof(*opts));
    opts->fg_color      = -1;
    opts->bg_color      = -1;
    opts->extensions    = 1;   /* default: on */
    opts->file_complete = 1;   /* default: on (TAB completes files/dirs) */
    opts->delayed_expand = 0;  /* default: off */

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if ((arg[0] != '/' && arg[0] != '-') || arg[1] == '\0') {
            /* Not a flag; treat as batch file or command string */
            break;
        }

        switch (toupper((unsigned char)arg[1])) {
        case 'C':
            opts->run_then_exit = 1;
            i++;
            goto collect_string;

        case 'K':
            opts->run_then_keep = 1;
            i++;
            goto collect_string;

        case 'S':
            opts->special_parse = 1;
            break;

        case 'Q':
            opts->quiet = 1;
            break;

        case 'D':
            opts->no_autorun = 1;
            break;

        case 'A':
            opts->ansi_output = 1;
            break;

        case 'U':
            opts->unicode_output = 1;
            break;

        case 'T': {
            /* /T:bf or /T:f - only valid with a ':' value, otherwise
             * treat as a batch file path (e.g. /tmp/x.bat) */
            const char *val = NULL;
            if (arg[2] == ':')
                val = arg + 3;
            else if (i + 1 < argc && argv[i+1][0] != '/')
                val = argv[++i];
            else
                goto have_rest;
            if (val) {
                size_t vlen = strlen(val);
                if (vlen >= 2) {
                    opts->bg_color = parse_color_nibble(val[0]);
                    opts->fg_color = parse_color_nibble(val[1]);
                } else if (vlen == 1) {
                    opts->fg_color = parse_color_nibble(val[0]);
                }
            }
            break;
        }

        case 'E': {
            const char *val = (arg[2] == ':') ? arg + 3 : NULL;
            if (val) {
                int r = parse_on_off(val);
                if (r >= 0) opts->extensions = r;
            } else {
                goto have_rest;  /* e.g. /etc/x.bat */
            }
            break;
        }

        case 'F': {
            const char *val = (arg[2] == ':') ? arg + 3 : NULL;
            if (val) {
                int r = parse_on_off(val);
                if (r >= 0) opts->file_complete = r;
            } else {
                goto have_rest;  /* e.g. /foo/x.bat */
            }
            break;
        }

        case 'V': {
            const char *val = (arg[2] == ':') ? arg + 3 : NULL;
            if (val) {
                int r = parse_on_off(val);
                if (r >= 0) opts->delayed_expand = r;
            } else {
                goto have_rest;  /* e.g. /var/x.bat */
            }
            break;
        }

        case '?':
            opts->show_help = 1;
            return;

        default:
            /* Unknown flag: treat it and everything after it as a
             * batch file / command string (e.g. cmd /tmp/script.bat) */
            goto have_rest;
        }
        continue;

collect_string:
        /* Join remaining arguments as the command string */
        if (i < argc) {
            char *buf = NULL;

#if defined(_WIN32) || defined(_WIN64) || defined(_MSC_VER)
            /* Preferred source: the raw OS command line.  Real cmd.exe
             * takes the /C string from this text, and launchers such as
             * PowerShell pass inner quotes unescaped ("echo x "y" z"),
             * which the CRT argv rules fold away before the join.  The
             * flag-token walk verifies the raw text lines up with the
             * CRT argv before it is trusted. */
            buf = raw_tail_after_flag(argc, argv, i);
#endif

            if (buf == NULL) {
                /* Fall back to joining the CRT arguments with spaces. */
                size_t total = 0;
                int j;

                for (j = i; j < argc; j++)
                    total += strlen(argv[j]) + 1;

                buf = (char *)malloc(total + 1);
                if (buf) {
                    buf[0] = '\0';
                    for (j = i; j < argc; j++) {
                        if (j > i) strcat(buf, " ");
                        strcat(buf, argv[j]);
                    }
                }
            }

            if (buf) {
                /* cmd.exe /C quote handling (/S forces the strip). */
                apply_c_quote_rule(buf, opts->special_parse);
                opts->cmd_string = buf;
            }
            i = argc; /* consume remaining args */
        }
        return;
    }

    /* Any remaining argument is a batch file */
have_rest:
    if (i < argc) {
        opts->batch_file = argv[i];
        opts->batch_argc = argc - i;
        opts->batch_argv = argv + i;
    }
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    cmd_context_t *ctx;
    cmd_opts_t    opts;
    int           ret = 0;

    libcmd_init_self(argc > 0 ? argv[0] : NULL);

    parse_args(argc, argv, &opts);

    /* Select the message language early: print_usage() needs it */
    cmd_i18n_init();

    if (opts.show_help) {
        print_usage();
        return 0;
    }

    /* Ctrl+C must not kill cmd: catch SIGINT and just continue */
    cmd_install_signal_handlers();

    ctx = (cmd_context_t *)calloc(1, sizeof(cmd_context_t));
    if (ctx == NULL) {
        fputs("cmd: out of memory\n", stderr);
        return 1;
    }

    cmd_context_init(ctx);

    /* Save the original command line for %CMDCMDLINE% */
    {
        int i;
        ctx->cmdline[0] = '\0';
        for (i = 0; i < argc; i++) {
            if (i > 0)
                strncat(ctx->cmdline, " ",
                        sizeof(ctx->cmdline) - strlen(ctx->cmdline) - 1);
            strncat(ctx->cmdline, argv[i],
                    sizeof(ctx->cmdline) - strlen(ctx->cmdline) - 1);
        }
    }

    /* Apply options */
    ctx->echo           = opts.quiet ? 0 : 1;
    ctx->extensions     = opts.extensions;
    ctx->delayed_expand = opts.delayed_expand;
    ctx->file_completion = opts.file_complete;
    ctx->ansi_output    = opts.ansi_output;
    ctx->unicode_output = opts.unicode_output;
    ctx->quiet          = opts.quiet;
    ctx->disable_autorun = opts.no_autorun;

    /* /f:on - enable TAB file/directory completion key */
    libcmd_readline_set_file_completion(opts.file_complete);

    /* Apply initial color if specified */
    if (opts.fg_color >= 0 || opts.bg_color >= 0)
        libcmd_set_color(opts.fg_color, opts.bg_color);

    /* Print banner in interactive mode */
    if (!opts.run_then_exit && !opts.run_then_keep &&
        !opts.batch_file && !opts.cmd_string) {
        if (libcmd_isatty(LIBCMD_STDIN_FILENO)) {
            fputs(cmd_get_banner(), stdout);
        }
    }

    /* Execute AutoRun scripts unless /D was given */
    if (!ctx->disable_autorun) {
        libcmd_exec_autorun(ctx->prefix, cmd_autorun_callback, ctx);
    }

#ifdef ENABLE_AUTOEXEC
    /* Search for autoexec.bat (case-insensitive) in the root directory
     * and execute the first match found. */
    {
        libcmd_dir_t root = libcmd_opendir("/");
        if (root) {
            libcmd_dirent_t entry;
            while (libcmd_readdir(root, &entry) == 0) {
                if (!entry.is_dir &&
                    libcmd_strcasecmp(entry.name, "autoexec.bat") == 0) {
                    char autoexec_path[CMD_MAX_PATH];
                    libcmd_sprintf_s(autoexec_path, sizeof(autoexec_path),
                                     "/%s", entry.name);
                    {
                        char *bat_argv[2];
                        bat_argv[0] = autoexec_path;
                        bat_argv[1] = NULL;
                        cmd_run_file(ctx, autoexec_path, 1, bat_argv);
                    }
                    break;  /* execute only the first match */
                }
            }
            libcmd_closedir(root);
        }
    }
#endif

    /* Dispatch */
    if (opts.cmd_string) {
        /*
         * If the command string looks like a batch file path, run it directly.
         * Otherwise, run it as a command string.
         *
         * The string is used verbatim like cmd.exe's /C: normalising it
         * as a path would turn every switch into "\switch".  A batch
         * file given with '\' separators still resolves (the crt
         * accepts either separator), and dispatch normalises the
         * command name itself.
         */
        {
            const char *ext = libcmd_path_ext(opts.cmd_string);
            if ((libcmd_strcasecmp(ext, ".bat") == 0 ||
                 libcmd_strcasecmp(ext, ".cmd") == 0) &&
                libcmd_access(opts.cmd_string, 0) == 0) {
                char *bat_argv[2];
                bat_argv[0] = opts.cmd_string;
                bat_argv[1] = NULL;
                ret = cmd_run_file(ctx, opts.cmd_string, 1, bat_argv);
            } else {
                ret = cmd_run_string(ctx, opts.cmd_string);
            }
        }
        free(opts.cmd_string);
        if (opts.run_then_keep) {
            ret = cmd_run_interactive(ctx);
        }
    } else if (opts.batch_file) {
        libcmd_path_norm_sep(opts.batch_file);
        ret = cmd_run_file(ctx, opts.batch_file,
                           opts.batch_argc, opts.batch_argv);
    } else {
        ret = cmd_run_interactive(ctx);
    }

    cmd_context_free(ctx);
    free(ctx);
    libcmd_reset_color();
    return ret;
}
