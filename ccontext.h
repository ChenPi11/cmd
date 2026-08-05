/*
 * ccontext.h - interpreter state definitions
 *
 * The cmd_context structure holds all state for one instance of the
 * cmd interpreter.  This header is internal to the cmd executable.
 *
 * License: GNU GPLv3
 */

#include <stdio.h>
#include <stddef.h>

#include "imessages.h"

#ifndef CMD_CONTEXT_H
#define CMD_CONTEXT_H

/* Maximum argument count for a single command */
#define CMD_MAX_ARGS 1024

/* Maximum line length */
#define CMD_MAX_LINE 8192

/* Maximum path length */
#define CMD_MAX_PATH 4096

/* Maximum number of batch script arguments (%0 - %9) */
#define CMD_BATCH_ARGS 10

/* Maximum nesting depth for SETLOCAL */
#define CMD_SETLOCAL_DEPTH 32

/* Maximum depth for PUSHD/POPD */
#define CMD_DIRSTACK_DEPTH 128

/* -------------------------------------------------------------------------
 * Saved local environment (SETLOCAL / ENDLOCAL)
 * ---------------------------------------------------------------------- */

typedef struct cmd_local_env {
    char  **env_vars;    /* copy of environment variable array, NULL-terminated */
    int     env_count;
    char    cwd[CMD_MAX_PATH]; /* saved working directory */
    int     echo;
    int     delayed_expand;
    int     extensions;
} cmd_local_env_t;

/* DOSKEY macro definition */
typedef struct cmd_macro {
    char name[64];
    char value[CMD_MAX_LINE];
} cmd_macro_t;

/* -------------------------------------------------------------------------
 * Batch file call stack frame
 * ---------------------------------------------------------------------- */

typedef struct cmd_call_frame {
    FILE  *file;                      /* batch file handle         */
    char   path[CMD_MAX_PATH];        /* path of batch file        */
    char  *args[CMD_BATCH_ARGS];      /* %0 .. %9 (heap-allocated) */
    int    shift_count;               /* offset applied by SHIFT   */
    long   resume_pos;                /* for GOTO: position after label seek */
} cmd_call_frame_t;

#define CMD_CALL_STACK_DEPTH 64

/* -------------------------------------------------------------------------
 * Main interpreter context
 * ---------------------------------------------------------------------- */

typedef struct cmd_context {
    /* --- interpreter flags --- */
    int echo;            /* 1 = echo commands (default on)     */
    int extensions;      /* 1 = command extensions enabled     */
    int delayed_expand;  /* 1 = delayed var expansion with !   */
    int file_completion; /* 1 = file/dir tab completion        */
    int unicode_output;  /* 1 = unicode output mode            */
    int ansi_output;     /* 1 = ANSI output mode               */
    int quiet;           /* /q flag                            */
    int disable_autorun; /* /d flag                            */
    int verify_on;       /* VERIFY ON/OFF state (default off)  */

    /* --- DOSKEY macros --- */
#define CMD_DOSKEY_MACROS 64
    cmd_macro_t macros[CMD_DOSKEY_MACROS];
    int macro_count;

    /* --- exit state --- */
    int should_exit;     /* set to 1 to exit the interactive loop */
    int stop_batch;      /* set to 1 to stop the current batch file only */
    int abort_batch;     /* set to 1 to abort the whole batch chain
                          * ("Terminate batch job (Y/N)?" answered Y) */
    int exit_code;       /* current ERRORLEVEL                 */
    int exit_value;      /* value to pass to exit()            */

    /* Set to 1 to suppress echo for the current command only (@ prefix) */
    int echo_suppress;

    /* --- batch call stack --- */
    cmd_call_frame_t *call_stack[CMD_CALL_STACK_DEPTH];
    int               call_depth;    /* 0 = interactive/top level */

    /* --- SETLOCAL / ENDLOCAL stack --- */
    cmd_local_env_t   local_stack[CMD_SETLOCAL_DEPTH];
    int               local_depth;

    /* --- PUSHD / POPD stack --- */
    char *dir_stack[CMD_DIRSTACK_DEPTH];
    int   dir_stack_top;

    /* --- I/O redirection (saved fds) --- */
    int saved_stdin;    /* saved original STDIN_FILENO  */
    int saved_stdout;   /* saved original STDOUT_FILENO */
    int saved_stderr;   /* saved original STDERR_FILENO */

    /* --- miscellaneous --- */
    char prompt_string[256];  /* current prompt format string  */
    char cmdline[CMD_MAX_LINE * 2]; /* original command line (%CMDCMDLINE%) */

    /* --- install prefix (for autorun, etc.) --- */
    char prefix[CMD_MAX_PATH];
} cmd_context_t;

/* -------------------------------------------------------------------------
 * Builtin function signature
 * ---------------------------------------------------------------------- */

/*
 * A builtin command receives:
 *   ctx  - the interpreter state
 *   argc - number of arguments (argv[0] is the command name)
 *   argv - argument array, NULL-terminated
 *
 * Returns an exit code (0 = success, non-zero = failure).
 * The exit code is stored in ctx->exit_code by the caller.
 */
typedef int (*cmd_builtin_fn)(cmd_context_t *ctx, int argc, char **argv);

/* -------------------------------------------------------------------------
 * Builtin table entry
 * ---------------------------------------------------------------------- */

typedef struct cmd_builtin {
    const char    *name;
    cmd_builtin_fn fn;
} cmd_builtin_t;

/* -------------------------------------------------------------------------
 * Forward declarations for functions used across cmd files
 * ---------------------------------------------------------------------- */

/* cinterp.c */
void cmd_context_init(cmd_context_t *ctx);
void cmd_context_free(cmd_context_t *ctx);
void cmd_install_signal_handlers(void);
const char *cmd_get_banner(void);
int  cmd_run_line(cmd_context_t *ctx, const char *line);
int  cmd_run_file(cmd_context_t *ctx, const char *path,
                  int argc, char **argv);
/*
 * cmd_run_file_sub - run a sub-call within an already-open batch file.
 * Used by CALL :label.  Does NOT close fp when done.
 * fp must already be positioned at the start of the sub-routine.
 * path is the original batch file path (for %0).
 */
int  cmd_run_file_sub(cmd_context_t *ctx, FILE *fp,
                      const char *path, int argc, char **argv);
int  cmd_run_interactive(cmd_context_t *ctx);
int  cmd_run_string(cmd_context_t *ctx, const char *cmd_str);

/* autorun callback (libcmd_run_fn-compatible) */
int  cmd_autorun_callback(const char *line, void *user_data);

/* restore one SETLOCAL level (implicit ENDLOCAL at end of batch) */
void cmd_endlocal(cmd_context_t *ctx);

/* cvars.c */
char *cmd_expand_vars(cmd_context_t *ctx, const char *line);
int   cmd_print_dynamic_vars(cmd_context_t *ctx, const char *prefix);

/* cdispatch.c */
int cmd_dispatch(cmd_context_t *ctx, int argc, char **argv,
                 int stdin_fd, int stdout_fd, int stderr_fd);
const cmd_builtin_t *cmd_find_builtin(const char *name);

/* cparser.c */
typedef struct cmd_node cmd_node_t;
cmd_node_t *cmd_parse(const char *line);
void        cmd_node_free(cmd_node_t *node);
int         cmd_exec_node(cmd_context_t *ctx, cmd_node_t *node);

/* credir.c */
int  cmd_apply_redir(cmd_context_t *ctx, cmd_node_t *node);
void cmd_restore_redir(cmd_context_t *ctx,
                       int old_stdin, int old_stdout, int old_stderr);

#endif
