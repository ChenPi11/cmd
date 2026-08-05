/*
 * cinterp.c - main interpreter loop
 *
 * Handles:
 *   - Context initialization and teardown
 *   - Interactive REPL (prompt, line reading, echo)
 *   - Batch file execution
 *   - Running a single command string
 *   - Prompt rendering
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Version banner (uses uname to generate dynamically)
 * ---------------------------------------------------------------------- */

const char *cmd_get_banner(void)
{
    static char buf[256];
    struct utsname u;

    memset(buf, 0, sizeof(buf));
    /* Some unix may return 1 on success. */
    uname(&u);

    if (u.sysname[0] == '\0')
    {
        u.sysname[0] = 'U';
        u.sysname[1] = 'n';
        u.sysname[2] = 'i';
        u.sysname[3] = 'x';
        u.sysname[4] = '\0';
    }
    if (u.release[0] == '\0')
    {
        u.release[0] = 'U';
        u.release[1] = 'n';
        u.release[2] = 'k';
        u.release[3] = 'n';
        u.release[4] = 'o';
        u.release[5] = 'w';
        u.release[6] = 'n';
        u.release[7] = '\0';
    }
    libcmd_sprintf_s(buf, sizeof(buf), cmd_gettext(MSG_BANNER_COPYRIGHT), u.sysname, u.release);

    return buf;
}

/* -------------------------------------------------------------------------
 * Signal handling (Ctrl+C)
 * ---------------------------------------------------------------------- */

static volatile sig_atomic_t cmd_sigint_flag = 0;
static volatile sig_atomic_t cmd_sigtstp_flag = 0;

/* sigsetjmp/siglongjmp support for interrupting builtin commands.
 * cmd_in_builtin is set before a builtin runs and cleared after;
 * the SIGINT handler checks it to decide whether to siglongjmp. */
sigjmp_buf cmd_interrupt_env;
volatile sig_atomic_t cmd_in_builtin = 0;

static void cmd_sigint_handler(int sig)
{
    (void)sig;
    cmd_sigint_flag = 1;
    if (cmd_in_builtin)
    {
        cmd_in_builtin = 0;
        siglongjmp(cmd_interrupt_env, 1);
    }
}

static void cmd_sigtstp_handler(int sig)
{
    (void)sig;
    cmd_sigtstp_flag = 1;
}

/*
 * Called by libcmd when a signal interrupted the terminal read.
 * Returns the signal number that was caught (0 if none); libcmd then
 * echoes '^C' / '^Z' and repaints the prompt on a fresh line.
 */
static int cmd_readline_signal_hook(void)
{
    if (cmd_sigint_flag)
    {
        cmd_sigint_flag = 0;
        return SIGINT;
    }
    if (cmd_sigtstp_flag)
    {
        cmd_sigtstp_flag = 0;
        return SIGTSTP;
    }
    return 0;
}

void cmd_install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = cmd_sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART: interrupted syscalls return EINTR */
    sigaction(SIGINT, &sa, NULL);

    /* Builtins writing to a closed pipe (e.g. echo x | head) must get
     * EPIPE instead of dying; pipeline children reset this to SIG_DFL. */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);
}

/* -------------------------------------------------------------------------
 * "Terminate batch job (Y/N)?" prompt (cmd.exe Ctrl+C behaviour)
 *
 * Returns 1 if the batch job should be terminated, 0 to continue with
 * the next line.  The prompt is written to /dev/tty and the answer read
 * from there too, so it works regardless of stdin/stdout redirection.
 * When no controlling terminal exists (non-interactive run), the batch
 * job is terminated without asking, matching cmd.exe batch mode.
 * ---------------------------------------------------------------------- */

static int prompt_terminate_batch(void)
{
    const char *msg = cmd_gettext(MSG_TERMINATE_BATCH);
    struct termios old, raw;
    int fd;
    int result = 1; /* default: terminate */
    int have_raw = 0;
    size_t msg_len = strlen(msg);

    fd = open("/dev/tty", O_RDWR);
    if (fd < 0)
        return 1;

    /* The terminal is in canonical mode: a single-key answer would
     * sit in the line buffer until Enter.  Switch to cbreak (no
     * ICANON) so one keystroke answers immediately; echo stays on. */
    if (tcgetattr(fd, &old) == 0)
    {
        raw = old;
        raw.c_lflag &= ~(tcflag_t)ICANON;
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(fd, TCSANOW, &raw) == 0)
            have_raw = 1;
    }

    for (;;)
    {
        char c = 0;
        long n;

        if (write(fd, msg, msg_len) < 0)
            break;

        do
        {
            n = read(fd, &c, 1);
        } while (n < 0 && errno == EINTR);

        if (n <= 0)
        {
            /* EOF / error on the terminal: terminate */
            break;
        }
        if (c == 'Y' || c == 'y')
        {
            result = 1;
            break;
        }
        if (c == 'N' || c == 'n')
        {
            result = 0;
            break;
        }
        /* Any other key: ask again */
    }

    if (have_raw)
        tcsetattr(fd, TCSANOW, &old);
    (void)write(fd, "\n", 1);
    close(fd);
    return result;
}

/* -------------------------------------------------------------------------
 * Context lifecycle
 * ---------------------------------------------------------------------- */

void cmd_context_init(cmd_context_t *ctx)
{
    int i;

    memset(ctx, 0, sizeof(*ctx));

    ctx->echo = 1;
    ctx->extensions = 1;
    ctx->delayed_expand = 0;
    ctx->file_completion = 0;
    ctx->unicode_output = 0;
    ctx->ansi_output = 0;
    ctx->quiet = 0;
    ctx->disable_autorun = 0;

    ctx->should_exit = 0;
    ctx->exit_code = 0;
    ctx->exit_value = 0;

    ctx->call_depth = 0;
    ctx->local_depth = 0;
    ctx->dir_stack_top = 0;

    ctx->saved_stdin = -1;
    ctx->saved_stdout = -1;
    ctx->saved_stderr = -1;

    /* Default prompt: $P$G */
    libcmd_sprintf_s(ctx->prompt_string, sizeof(ctx->prompt_string), "$P$G");

    /* Default install prefix */
    {
        const char *prefix_env = libcmd_getenv("CMD_PREFIX");
        if (prefix_env)
            libcmd_sprintf_s(ctx->prefix, sizeof(ctx->prefix), "%s", prefix_env);
        else
            libcmd_sprintf_s(ctx->prefix, sizeof(ctx->prefix), "/usr/local");
    }

    for (i = 0; i < CMD_DIRSTACK_DEPTH; i++)
        ctx->dir_stack[i] = NULL;

    /* Initialize line editing for interactive mode */
    libcmd_readline_init();
    libcmd_readline_set_signal_hook(cmd_readline_signal_hook);
}

void cmd_context_free(cmd_context_t *ctx)
{
    int i, j;

    /* Free call stack */
    for (i = 0; i < ctx->call_depth; i++)
    {
        cmd_call_frame_t *frame = ctx->call_stack[i];
        if (frame)
        {
            for (j = 0; j < CMD_BATCH_ARGS; j++)
                free(frame->args[j]);
            if (frame->file)
                fclose(frame->file);
            free(frame);
        }
    }

    /* Free dir stack */
    for (i = 0; i < ctx->dir_stack_top; i++)
        free(ctx->dir_stack[i]);

    /* Free local env stacks */
    for (i = 0; i < ctx->local_depth; i++)
    {
        cmd_local_env_t *loc = &ctx->local_stack[i];
        if (loc->env_vars)
        {
            for (j = 0; loc->env_vars[j]; j++)
                free(loc->env_vars[j]);
            free(loc->env_vars);
        }
    }

    /* Shutdown line editing and save history */
    libcmd_readline_shutdown();
}

/* -------------------------------------------------------------------------
 * Prompt rendering
 * ---------------------------------------------------------------------- */

static void render_prompt(cmd_context_t *ctx, FILE *out)
{
    const char *p = ctx->prompt_string;
    char cwd[CMD_MAX_PATH];

    while (*p)
    {
        if (*p == '$')
        {
            p++;
            if (*p == '\0')
                break;
            switch (toupper((unsigned char)*p))
            {
            case 'A':
                fputc('&', out);
                break;
            case 'B':
                fputc('|', out);
                break;
            case 'C':
                fputc('(', out);
                break;
            case 'D': {
                libcmd_time_t t;
                libcmd_get_local_time(&t);
                fprintf(out, "%02d/%02d/%04d", t.month, t.day, t.year);
                break;
            }
            case 'E':
                fputc('\033', out);
                break;
            case 'F':
                fputc(')', out);
                break;
            case 'G':
                fputc('>', out);
                break;
            case 'H':
                fputc('\b', out);
                break;
            case 'L':
                fputc('<', out);
                break;
            case 'N':
                fputc('C', out);
                break; /* No drive on Unix; just "C" */
            case 'P':
                /* Display the current directory with Windows-style '\' separators */
                if (libcmd_getcwd(cwd, sizeof(cwd)))
                {
                    char *q;
                    for (q = cwd; *q; q++)
                        if (*q == '/')
                            *q = '\\';
                    fputs(cwd, out);
                }
                break;
            case 'Q':
                fputc('=', out);
                break;
            case 'S':
                fputc(' ', out);
                break;
            case 'T': {
                libcmd_time_t t;
                libcmd_get_local_time(&t);
                fprintf(out, "%02d:%02d:%02d.%02d", t.hour, t.minute, t.second, t.ms / 10);
                break;
            }
            case 'V':
                fputs("cmd 0.1.0", out);
                break;
            case '_':
                fputc('\n', out);
                break;
            case '$':
                fputc('$', out);
                break;
            case '+': {
                int k;
                for (k = 0; k < ctx->dir_stack_top; k++)
                    fputc('+', out);
                break;
            }
            case 'M':
                /* Remote drive info not applicable on Unix */
                break;
            default:
                fputc('$', out);
                fputc(*p, out);
                break;
            }
        }
        else
        {
            fputc(*p, out);
        }
        p++;
    }
    fflush(out);
}

/* -------------------------------------------------------------------------
 * Line reading (with continuation handling)
 * ---------------------------------------------------------------------- */

/*
 * Read a logical line from f, handling line continuations (trailing ^).
 * buf must be CMD_MAX_LINE bytes.
 * Returns NULL on EOF, buf on success.
 */
static char *read_line(FILE *f, char *buf, size_t size)
{
    char tmp[CMD_MAX_LINE];
    size_t bpos = 0;

    for (;;)
    {
        int has_cont = 0;

        bpos = 0;

        /* Accumulate potentially continuation lines */
        while (fgets(tmp, sizeof(tmp), f) != NULL)
        {
            size_t len = strlen(tmp);

            /* Strip trailing newline(s) */
            while (len > 0 && (tmp[len - 1] == '\n' || tmp[len - 1] == '\r'))
                tmp[--len] = '\0';

            /* Check for line continuation: trailing ^ */
            if (len > 0 && tmp[len - 1] == '^')
            {
                tmp[--len] = '\0';
                has_cont = 1;
            }

            /* Append to buf */
            if (bpos + len < size - 1)
            {
                memcpy(buf + bpos, tmp, len);
                bpos += len;
            }

            if (!has_cont)
                break;
        }

        /* Empty result but not EOF: skip blank lines and keep reading */
        if (bpos == 0)
        {
            if (feof(f))
                return NULL; /* true EOF */
            continue;        /* blank line - skip and read next */
        }

        buf[bpos] = '\0';
        return buf;
    }
}

/* -------------------------------------------------------------------------
 * Run a single line
 * ---------------------------------------------------------------------- */

int cmd_run_line(cmd_context_t *ctx, const char *line)
{
    char *expanded;
    cmd_node_t *node;
    int ret = 0;
    const char *p = line;
    char macro_buf[CMD_MAX_LINE * 2];

    if (line == NULL)
        return 0;

    /* Skip whitespace */
    while (*p == ' ' || *p == '\t')
        p++;

    /* Empty line */
    if (*p == '\0')
        return 0;

    /* Comment line starting with :: (batch comment) */
    if (p[0] == ':' && p[1] == ':')
        return 0;

    /* Label line: :label (handled by goto in batch mode) */
    if (p[0] == ':')
        return 0;

    /* @ prefix: suppress echo for this line only */
    if (*p == '@')
    {
        p++;
        while (*p == ' ' || *p == '\t')
            p++;
        ctx->echo_suppress = 1;
        expanded = cmd_expand_vars(ctx, p);
        if (expanded)
        {
            node = cmd_parse(expanded);
            if (node)
            {
                ret = cmd_exec_node(ctx, node);
                cmd_node_free(node);
            }
            free(expanded);
        }
        ctx->echo_suppress = 0;
        return ret;
    }

    /* DOSKEY macro expansion (before variable expansion) */
    {
        extern void cmd_doskey_expand(cmd_context_t *, char *, size_t);
        libcmd_sprintf_s(macro_buf, sizeof(macro_buf), "%s", p);
        cmd_doskey_expand(ctx, macro_buf, sizeof(macro_buf));
        if (strcmp(macro_buf, p) != 0)
            p = macro_buf;
    }

    /* Expand variables */
    expanded = cmd_expand_vars(ctx, p);
    if (expanded == NULL)
        return 0;

    /* Parse and execute */
    node = cmd_parse(expanded);
    if (node)
    {
        /* Echo if in batch and echo is on and not suppressed */
        if (ctx->echo && !ctx->echo_suppress && ctx->call_depth > 0)
        {
            render_prompt(ctx, stdout);
            fputs(expanded, stdout);
            fputc('\n', stdout);
        }
        ret = cmd_exec_node(ctx, node);
        cmd_node_free(node);
    }

    free(expanded);
    return ret;
}

/* -------------------------------------------------------------------------
 * Autorun callback
 * ---------------------------------------------------------------------- */

int cmd_autorun_callback(const char *line, void *user_data)
{
    cmd_context_t *ctx = (cmd_context_t *)user_data;
    return cmd_run_line(ctx, line);
}

/* Restore one SETLOCAL level (used by ENDLOCAL and by the implicit
 * end-of-batch ENDLOCAL) */
void cmd_endlocal(cmd_context_t *ctx)
{
    extern int builtin_endlocal(cmd_context_t *, int, char **);
    char *dummy[1] = {NULL};
    builtin_endlocal(ctx, 0, dummy);
}

/* -------------------------------------------------------------------------
 * Run a batch file
 * ---------------------------------------------------------------------- */

int cmd_run_file(cmd_context_t *ctx, const char *path, int argc, char **argv)
{
    FILE *f;
    cmd_call_frame_t *frame;
    char line[CMD_MAX_LINE];
    int i;
    int start_depth;

    if (ctx->call_depth >= CMD_CALL_STACK_DEPTH)
    {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_CALL_DEPTH));
        return 1;
    }

    f = fopen(path, "r");
    if (f == NULL)
    {
        fprintf(stderr, cmd_gettext(MSG_ERR_OPEN_BATCH), path, libcmd_strerror());
        return 1;
    }

    /* Reject directories: fopen on a directory succeeds but reads fail
     * with EISDIR forever */
    {
        libcmd_stat_t st;
        if (libcmd_stat(path, &st, 1) == 0 && !st.is_regular)
        {
            fprintf(stderr, cmd_gettext(MSG_ERR_OPEN_BATCH), path, "Is a directory");
            fclose(f);
            return 1;
        }
    }

    frame = (cmd_call_frame_t *)calloc(1, sizeof(cmd_call_frame_t));
    if (frame == NULL)
    {
        fclose(f);
        return 1;
    }

    frame->file = f;
    libcmd_sprintf_s(frame->path, sizeof(frame->path), "%s", path);
    frame->shift_count = 0;

    /* %0 = script name, %1..%9 = arguments */
    frame->args[0] = libcmd_strdup(path);
    for (i = 1; i < CMD_BATCH_ARGS; i++)
    {
        if (i < argc)
            frame->args[i] = libcmd_strdup(argv[i]);
        else
            frame->args[i] = libcmd_strdup("");
    }

    ctx->call_stack[ctx->call_depth++] = frame;
    start_depth = ctx->local_depth;

    /* Execute lines */
    while (!ctx->should_exit && !ctx->stop_batch && !ctx->abort_batch)
    {
        if (read_line(f, line, sizeof(line)) == NULL)
            break;
        cmd_run_line(ctx, line);
        if (cmd_sigint_flag)
        {
            cmd_sigint_flag = 0;
            if (prompt_terminate_batch())
            {
                ctx->abort_batch = 1;
                break;
            }
            /* N: skip the interrupted line, continue with the next one */
        }
    }

    /* Clear stop_batch so callers can continue; abort_batch stays set so
     * every batch frame in the chain unwinds (outer loops also check it).
     * Only the root batch frame (no parent frame) clears it again. */
    ctx->stop_batch = 0;
    if (ctx->call_depth == 0 && ctx->abort_batch)
        ctx->abort_batch = 0;

    /* A batch file implicitly ends every SETLOCAL it started (cmd.exe
     * semantics: matching ENDLOCALs are executed at end of batch) */
    while (ctx->local_depth > start_depth)
        cmd_endlocal(ctx);

    /* Pop frame */
    ctx->call_depth--;
    for (i = 0; i < CMD_BATCH_ARGS; i++)
        free(frame->args[i]);
    fclose(frame->file);
    free(frame);

    return ctx->exit_code;
}

/* -------------------------------------------------------------------------
 * Run a sub-routine within the current batch file (CALL :label)
 * ---------------------------------------------------------------------- */

int cmd_run_file_sub(cmd_context_t *ctx, FILE *fp, const char *path, int argc, char **argv)
{
    cmd_call_frame_t *frame;
    char line[CMD_MAX_LINE];
    int i;
    int start_depth;

    if (ctx->call_depth >= CMD_CALL_STACK_DEPTH)
    {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_CALL_DEPTH));
        return 1;
    }

    frame = (cmd_call_frame_t *)calloc(1, sizeof(cmd_call_frame_t));
    if (frame == NULL)
        return 1;

    frame->file = fp; /* shared; do NOT fclose in this frame */
    libcmd_sprintf_s(frame->path, sizeof(frame->path), "%s", path);
    frame->shift_count = 0;

    /* %0 = script name, %1..%9 = subroutine arguments */
    frame->args[0] = libcmd_strdup(path);
    for (i = 1; i < CMD_BATCH_ARGS; i++)
    {
        if (i < argc)
            frame->args[i] = libcmd_strdup(argv[i]);
        else
            frame->args[i] = libcmd_strdup("");
    }

    ctx->call_stack[ctx->call_depth] = frame;
    ctx->call_depth++;
    start_depth = ctx->local_depth;

    /* Execute until stop_batch (GOTO :EOF / EXIT /B) or real EOF */
    while (!ctx->should_exit && !ctx->stop_batch && !ctx->abort_batch)
    {
        if (read_line(fp, line, sizeof(line)) == NULL)
            break;
        cmd_run_line(ctx, line);
        if (cmd_sigint_flag)
        {
            cmd_sigint_flag = 0;
            if (prompt_terminate_batch())
            {
                ctx->abort_batch = 1;
                break;
            }
            /* N: skip the interrupted line, continue with the next one */
        }
    }

    /* Clear stop_batch - the parent frame continues.  abort_batch stays
     * set so the whole chain unwinds (the root cmd_run_file clears it). */
    ctx->stop_batch = 0;

    /* Implicit ENDLOCAL at end of subroutine */
    while (ctx->local_depth > start_depth)
        cmd_endlocal(ctx);

    /* Pop frame but do NOT close fp (owned by parent frame) */
    ctx->call_depth--;
    for (i = 0; i < CMD_BATCH_ARGS; i++)
        free(frame->args[i]);
    free(frame);

    return ctx->exit_code;
}

/* -------------------------------------------------------------------------
 * Run a command string
 * ---------------------------------------------------------------------- */

int cmd_run_string(cmd_context_t *ctx, const char *cmd_str)
{
    /* A command string may contain multiple commands separated by newlines */
    char *copy, *p, *line_start;

    copy = libcmd_strdup(cmd_str);
    if (copy == NULL)
        return 1;

    p = copy;
    while (*p && !ctx->should_exit && !cmd_sigint_flag)
    {
        line_start = p;
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            *p++ = '\0';
        cmd_run_line(ctx, line_start);
    }

    free(copy);
    return ctx->exit_code;
}

/* -------------------------------------------------------------------------
 * Interactive REPL
 * ---------------------------------------------------------------------- */

int cmd_run_interactive(cmd_context_t *ctx)
{
    char line[CMD_MAX_LINE];
    int is_tty = libcmd_isatty(LIBCMD_STDIN_FILENO);

    while (!ctx->should_exit)
    {
        char *result;
        char *prompt = NULL;
        size_t prompt_len = 0;

        /* Ctrl+C during command execution: the terminal already echoed
         * '^C'; move to a fresh line before showing the next prompt */
        if (cmd_sigint_flag || cmd_sigtstp_flag)
        {
            cmd_sigint_flag = 0;
            cmd_sigtstp_flag = 0;
            if (is_tty)
            {
                fputc('\n', stdout);
                fflush(stdout);
            }
        }

        /* Render the prompt and hand it to the line editor, so it knows where
         * the input area starts (fixes backspace eating the prompt) */
        if (is_tty)
        {
            FILE *m = libcmd_open_memstream(&prompt, &prompt_len);
            if (m)
            {
                render_prompt(ctx, m);
                libcmd_memstream_close(m);
            }
        }

        /* Catch Ctrl+Z only while readline is editing: it is echoed as
         * '^Z' and the prompt repainted, like Ctrl+C. Outside editing,
         * SIGTSTP keeps its default behaviour (suspends the foreground
         * group; a plain waitpid would block forever on a stopped
         * child, since there is no job control) */
        if (is_tty)
        {
            struct sigaction sa_tstp, sa_prev;

            memset(&sa_tstp, 0, sizeof(sa_tstp));
            sa_tstp.sa_handler = cmd_sigtstp_handler;
            sigemptyset(&sa_tstp.sa_mask);
            sa_tstp.sa_flags = 0;
            sigaction(SIGTSTP, &sa_tstp, &sa_prev);
            result = libcmd_readline(is_tty && prompt ? prompt : "", line, sizeof(line));
            sigaction(SIGTSTP, &sa_prev, NULL);
        }
        else
        {
            result = libcmd_readline("", line, sizeof(line));
        }

        free(prompt);

        if (result == NULL)
        {
            /* EOF, or interrupted by Ctrl+C */
            if (cmd_sigint_flag || cmd_sigtstp_flag || errno == EINTR)
            {
                cmd_sigint_flag = 0;
                cmd_sigtstp_flag = 0;
                continue;
            }
            break;
        }

        /* Strip trailing newline (shouldn't be present from the line editor, but be safe) */
        {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
                line[--len] = '\0';
        }

        cmd_run_line(ctx, line);
    }

    return ctx->exit_value;
}
