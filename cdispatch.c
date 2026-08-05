/*
 * cdispatch.c - command dispatch
 *
 * Determines whether a command is a builtin and calls it, or finds and
 * executes an external program via libcmd.
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* sigsetjmp/siglongjmp support for interrupting builtins (defined in cinterp.c) */
extern sigjmp_buf cmd_interrupt_env;
extern volatile sig_atomic_t cmd_in_builtin;

/* -------------------------------------------------------------------------
 * Builtin declarations (defined in builtins\*.c)
 * ---------------------------------------------------------------------- */

extern int builtin_assoc  (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_break  (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_call   (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_cd     (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_cls    (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_color  (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_copy   (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_date   (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_del    (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_dir    (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_echo   (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_endlocal(cmd_context_t *ctx, int argc, char **argv);
extern int builtin_exit   (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_for    (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_ftype  (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_goto   (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_if     (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_md     (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_mklink (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_move   (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_path   (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_pause  (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_popd   (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_prompt (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_pushd  (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_rd     (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_rem    (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_ren    (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_set    (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_setlocal(cmd_context_t *ctx, int argc, char **argv);
extern int builtin_shift  (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_start  (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_time_cmd(cmd_context_t *ctx, int argc, char **argv);
extern int builtin_title  (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_type   (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_ver    (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_verify (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_doskey (cmd_context_t *ctx, int argc, char **argv);
extern int builtin_vol    (cmd_context_t *ctx, int argc, char **argv);

/* -------------------------------------------------------------------------
 * Builtin table
 * ---------------------------------------------------------------------- */

static const cmd_builtin_t builtin_table[] = {
    { "assoc",    builtin_assoc   },
    { "break",    builtin_break   },
    { "call",     builtin_call    },
    { "cd",       builtin_cd      },
    { "chdir",    builtin_cd      },  /* alias */
    { "cls",      builtin_cls     },
    { "color",    builtin_color   },
    { "copy",     builtin_copy    },
    { "date",     builtin_date    },
    { "del",      builtin_del     },
    { "dir",      builtin_dir     },
    { "doskey",   builtin_doskey  },
    { "echo",     builtin_echo    },
    { "endlocal", builtin_endlocal},
    { "erase",    builtin_del     },  /* alias */
    { "exit",     builtin_exit    },
    { "for",      builtin_for     },
    { "ftype",    builtin_ftype   },
    { "goto",     builtin_goto    },
    { "if",       builtin_if      },
    { "md",       builtin_md      },  /* alias for mkdir */
    { "mkdir",    builtin_md      },
    { "mklink",   builtin_mklink  },
    { "move",     builtin_move    },
    { "path",     builtin_path    },
    { "pause",    builtin_pause   },
    { "popd",     builtin_popd    },
    { "prompt",   builtin_prompt  },
    { "pushd",    builtin_pushd   },
    { "rd",       builtin_rd      },  /* alias for rmdir */
    { "rem",      builtin_rem     },
    { "ren",      builtin_ren     },
    { "rename",   builtin_ren     },  /* alias */
    { "rmdir",    builtin_rd      },
    { "set",      builtin_set     },
    { "setlocal", builtin_setlocal},
    { "shift",    builtin_shift   },
    { "start",    builtin_start   },
    { "time",     builtin_time_cmd},
    { "title",    builtin_title   },
    { "type",     builtin_type    },
    { "ver",      builtin_ver     },
    { "verify",   builtin_verify  },
    { "vol",      builtin_vol     },
    { NULL, NULL }
};

const cmd_builtin_t *cmd_find_builtin(const char *name)
{
    const cmd_builtin_t *volatile b;

    if (name == NULL)
        return NULL;

    for (b = builtin_table; b->name != NULL; b++) {
        if (libcmd_strcasecmp(name, b->name) == 0)
            return b;
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * External command execution
 * ---------------------------------------------------------------------- */

/* Expand wildcards in the arguments of an external command (cmd.exe
 * behaviour).  Returns a new NULL-terminated argv array; expanded
 * matches are owned by saved[] (free with libcmd_glob_free).  Returns
 * NULL if no argument needed expansion. */
static char **expand_wildcard_args(int argc, char **argv,
                                   libcmd_glob_result_t *saved,
                                   int *saved_count)
{
    size_t total = (size_t)argc;
    int i, si = 0, o = 0;
    char **out;

    for (i = 1; i < argc; i++) {
        if (strchr(argv[i], '*') != NULL || strchr(argv[i], '?') != NULL) {
            if (libcmd_glob(argv[i], &saved[*saved_count]) == 0 &&
                saved[*saved_count].count > 0) {
                total += saved[*saved_count].count - 1;
                (*saved_count)++;
            }
        }
    }

    if (*saved_count == 0)
        return NULL;

    out = (char **)malloc((total + 1) * sizeof(char *));
    if (out == NULL)
        return NULL;

    for (i = 0; i < argc; i++) {
        int is_wild = strchr(argv[i], '*') != NULL || strchr(argv[i], '?') != NULL;
        if (is_wild && si < *saved_count) {
            size_t j;
            for (j = 0; j < saved[si].count; j++)
                out[o++] = saved[si].paths[j];
            si++;
        } else {
            out[o++] = argv[i];
        }
    }
    out[o] = NULL;
    return out;
}

static int run_external(cmd_context_t *ctx,
                        int argc, char **argv,
                        int stdin_fd, int stdout_fd, int stderr_fd)
{
    char exec_path[CMD_MAX_PATH];
    const char *path_env;
    libcmd_exit_info_t exit_info;
    const char *ext;
    int ret;
    char **exec_argv = argv;
    libcmd_glob_result_t saved[64];
    int nsaved = 0;
    int i;

    memset(saved, 0, sizeof(saved));

    path_env = libcmd_getenv("PATH");

    /* Convert '\' separators in the program name (\usr\bin\grep) */
    libcmd_path_norm_sep(argv[0]);

    ext      = libcmd_path_ext(argv[0]);

    /* Check for batch file (.bat or .cmd extension) */
    if (libcmd_strcasecmp(ext, ".bat") == 0 ||
        libcmd_strcasecmp(ext, ".cmd") == 0) {
        /* Run as batch file */
        char batch_path[CMD_MAX_PATH];

        if (libcmd_path_is_abs(argv[0])) {
            libcmd_sprintf_s(batch_path, sizeof(batch_path), "%s", argv[0]);
        } else if (libcmd_path_abs(argv[0], batch_path, sizeof(batch_path)) < 0) {
            libcmd_sprintf_s(batch_path, sizeof(batch_path), "%s", argv[0]);
        }

        if (libcmd_access(batch_path, 0) == 0) {
            extern int cmd_run_file(cmd_context_t *ctx, const char *path,
                                    int argc, char **argv);
            return cmd_run_file(ctx, batch_path, argc, argv);
        }
    }

    /* Try to find the executable */
    if (libcmd_find_exec(argv[0], path_env, exec_path, sizeof(exec_path)) < 0) {
        /* Try with common batch extensions */
        char with_ext[CMD_MAX_PATH];
        int found = 0;

        if (ext[0] == '\0') {
            /* Try .bat extension */
            libcmd_sprintf_s(with_ext, sizeof(with_ext), "%s.bat", argv[0]);
            if (libcmd_access(with_ext, 0) == 0) {
                extern int cmd_run_file(cmd_context_t *ctx, const char *path,
                                        int argc, char **argv);
                return cmd_run_file(ctx, with_ext, argc, argv);
            }
            /* Try .cmd extension */
            libcmd_sprintf_s(with_ext, sizeof(with_ext), "%s.cmd", argv[0]);
            if (libcmd_access(with_ext, 0) == 0) {
                extern int cmd_run_file(cmd_context_t *ctx, const char *path,
                                        int argc, char **argv);
                return cmd_run_file(ctx, with_ext, argc, argv);
            }
        }

        if (!found) {
            fprintf(stderr, cmd_gettext(MSG_NOT_RECOGNIZED), argv[0]);
            return 1;
        }
    }

    /* Expand wildcards in arguments (cmd.exe semantics) */
    exec_argv = expand_wildcard_args(argc, argv, saved, &nsaved);
    if (exec_argv == NULL)
        exec_argv = argv;

    ret = libcmd_exec_sync(exec_path,
                           exec_argv,
                           libcmd_get_environ(),
                           stdin_fd,
                           stdout_fd,
                           stderr_fd,
                           0,
                           &exit_info);

    if (ret < 0) {
        fprintf(stderr, cmd_gettext(MSG_ERR_EXEC_FAILED),
                argv[0], libcmd_strerror());
        ret = 1;
    } else {
        ret = exit_info.exit_code;
    }

    /* Free expanded wildcard matches */
    for (i = 0; i < nsaved; i++)
        libcmd_glob_free(&saved[i]);
    if (exec_argv != argv)
        free(exec_argv);

    return ret;
}

/* -------------------------------------------------------------------------
 * Main dispatch function
 * ---------------------------------------------------------------------- */

int cmd_dispatch(cmd_context_t *ctx, int argc, char **argv,
                 int stdin_fd, int stdout_fd, int stderr_fd)
{
    const cmd_builtin_t *volatile builtin;
    int ret;

    if (argc == 0 || argv == NULL || argv[0] == NULL)
        return 0;

    /* Echo is handled in cmd_run_line, not here.  Remove the redundant
     * per-dispatch echo that was causing double output. */

    /* Look up builtin */
    builtin = cmd_find_builtin(argv[0]);
    if (builtin != NULL) {
        /* Convert Windows-style '\' separators to '/' in every builtin
         * argument, so paths like \usr\bin work everywhere. */
        {
            int i;
            for (i = 0; i < argc; i++)
                libcmd_path_norm_sep(argv[i]);
        }
        /* Save/restore standard fds around the builtin call if redirection
         * was requested.  For builtins, the fds have already been dup2'd
         * in cmd_exec_node; just call the builtin directly. */
        (void)stdin_fd;
        (void)stdout_fd;
        (void)stderr_fd;

        /* Save cwd before the builtin runs, so we can restore it if the
         * builtin changes it (e.g. dir /s) and gets interrupted by SIGINT.
         * cd / pushd / popd are intentionally excluded. */
        {
            char saved_cwd[CMD_MAX_PATH];
            int have_cwd = (libcmd_getcwd(saved_cwd, sizeof(saved_cwd)) != NULL);
            int skip_restore = (libcmd_strcasecmp(argv[0], "cd")    == 0 ||
                                libcmd_strcasecmp(argv[0], "chdir") == 0 ||
                                libcmd_strcasecmp(argv[0], "pushd") == 0 ||
                                libcmd_strcasecmp(argv[0], "popd")  == 0);

            if (sigsetjmp(cmd_interrupt_env, 1) == 0) {
                cmd_in_builtin = 1;
                ret = builtin->fn(ctx, argc, argv);
            } else {
                ret = 1;  /* interrupted by SIGINT */
            }
            cmd_in_builtin = 0;

            if (have_cwd && !skip_restore)
                libcmd_chdir(saved_cwd);
        }
    } else {
        ret = run_external(ctx, argc, argv, stdin_fd, stdout_fd, stderr_fd);
    }

    ctx->exit_code = ret;
    return ret;
}
