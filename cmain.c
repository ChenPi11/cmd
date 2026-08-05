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
            /* Join all remaining args with spaces */
            size_t total = 0;
            int j;
            char *buf;

            for (j = i; j < argc; j++)
                total += strlen(argv[j]) + 1;

            buf = (char *)malloc(total + 1);
            if (buf) {
                buf[0] = '\0';
                for (j = i; j < argc; j++) {
                    if (j > i) strcat(buf, " ");
                    strcat(buf, argv[j]);
                }
                /* /s: strip surrounding quotes from the command string
                 * (cmd.exe strips the outer quote pair of a /c string
                 * when the whole string is quoted) */
                if (opts->special_parse && buf[0] == '"') {
                    size_t blen = strlen(buf);
                    if (blen >= 2 && buf[blen - 1] == '"' && buf[1] != '"') {
                        memmove(buf, buf + 1, blen - 2);
                        buf[blen - 2] = '\0';
                    }
                }
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
         */
        libcmd_path_norm_sep(opts.cmd_string);
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
