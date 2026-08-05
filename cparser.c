/*
 * cparser.c - command line parser
 *
 * Parses a cmd command line into a tree of cmd_node_t nodes.
 * The tree reflects the operator precedence:
 *
 *   & (sequential) < && / || (conditional) < | (pipe) < simple command
 *
 * Grammar (simplified):
 *
 *   stmt_list  := stmt (& stmt)*
 *   stmt       := pipeline (&& pipeline | || pipeline)*
 *   pipeline   := cmd (| cmd)*
 *   cmd        := simple_cmd | ( stmt_list )
 *   simple_cmd := [redir]* word [word]* [redir]*
 *   redir      := (> | >> | < | 2> | n> | n>> | n>&m) filename_or_fd
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <assert.h>
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

/* -------------------------------------------------------------------------
 * Node types
 * ---------------------------------------------------------------------- */

typedef enum {
    NODE_SIMPLE,   /* a simple command (words + redirections)  */
    NODE_PIPE,     /* cmd1 | cmd2                              */
    NODE_AND,      /* cmd1 && cmd2                             */
    NODE_OR,       /* cmd1 || cmd2                             */
    NODE_SEQ,      /* cmd1 & cmd2                              */
    NODE_GROUP     /* ( stmt_list )                            */
} cmd_node_type_t;

/* Redirection operators */
typedef enum {
    REDIR_IN,       /* <  fd 0 from file           */
    REDIR_OUT,      /* >  fd 1 to file (truncate)  */
    REDIR_APPEND,   /* >> fd 1 to file (append)    */
    REDIR_ERR,      /* 2> fd 2 to file             */
    REDIR_ERR_APP,  /* 2>> fd 2 to file (append)   */
    REDIR_FD,       /* n>&m duplicate fd           */
    REDIR_OUT_ERR,  /* &> or &>> stdout+stderr to file */
    REDIR_OUT_ERR_APP /* &>> stdout+stderr to file (append) */
} redir_type_t;

typedef struct cmd_redir {
    redir_type_t type;
    int          src_fd;   /* source fd (e.g. 2 for 2>)       */
    int          dst_fd;   /* for REDIR_FD: destination fd    */
    char        *file;     /* for file redirections            */
    struct cmd_redir *next;
} cmd_redir_t;

struct cmd_node {
    cmd_node_type_t type;

    /* NODE_SIMPLE */
    int    argc;
    char **argv; /* NULL-terminated, argv[0] = command name */
    cmd_redir_t *redirs;

    /* NODE_PIPE, NODE_AND, NODE_OR, NODE_SEQ, NODE_GROUP */
    cmd_node_t *left;
    cmd_node_t *right;
};

/* -------------------------------------------------------------------------
 * Tokenizer state
 * ---------------------------------------------------------------------- */

typedef struct {
    const char *src;
    int         pos;
    int         len;
} lexer_t;

static void lexer_init(lexer_t *l, const char *src)
{
    l->src = src;
    l->pos = 0;
    l->len = (int)strlen(src);
}

static int lexer_peek(const lexer_t *l)
{
    if (l->pos >= l->len) return -1;
    return (unsigned char)l->src[l->pos];
}

static int lexer_peek2(const lexer_t *l)
{
    if (l->pos + 1 >= l->len) return -1;
    return (unsigned char)l->src[l->pos + 1];
}

static int lexer_get(lexer_t *l)
{
    if (l->pos >= l->len) return -1;
    return (unsigned char)l->src[l->pos++];
}

static void lexer_skip_spaces(lexer_t *l)
{
    while (l->pos < l->len &&
           (l->src[l->pos] == ' ' || l->src[l->pos] == '\t'))
        l->pos++;
}

/* -------------------------------------------------------------------------
 * Word reading (handles quotes and caret escapes)
 * ---------------------------------------------------------------------- */

/*
 * Read one word token from the lexer.
 * A word is a sequence of characters separated from others by spaces,
 * operators, or redirections.
 * Handles:
 *   "..." - quoted string (special chars literal inside, except %var%)
 *   ^X    - escaped character (^ removes special meaning of X)
 *
 * Returns a heap-allocated string, or NULL at end/on error.
 */
static char *read_word(lexer_t *l)
{
    char buf[CMD_MAX_LINE];
    int  bpos  = 0;
    int  in_quote = 0;
    int  c;

    lexer_skip_spaces(l);

    if (l->pos >= l->len)
        return NULL;

    c = lexer_peek(l);
    /* Check for operator / special chars that are NOT part of a word */
    if (!in_quote && (c == '|' || c == '&' || c == '<' || c == '>' ||
                      c == '(' || c == ')' || c == '\n' || c == '\r')) {
        return NULL;
    }

    while (l->pos < l->len) {
        c = lexer_peek(l);

        if (!in_quote) {
            if (c == '"') {
                in_quote = 1;
                lexer_get(l);
                continue;
            }
            if (c == '^') {
                lexer_get(l);
                c = lexer_get(l);
                if (c < 0) break;
                if (bpos < CMD_MAX_LINE - 1)
                    buf[bpos++] = (char)c;
                continue;
            }
            /* Operators end the word */
            if (c == ' '  || c == '\t' || c == '|'  ||
                c == '&'  || c == '<'  || c == '>'  ||
                c == '('  || c == ')'  || c == '\n' || c == '\r') {
                break;
            }
        } else {
            /* Inside quotes */
            if (c == '"') {
                in_quote = 0;
                lexer_get(l);
                continue;
            }
        }

        lexer_get(l);
        if (bpos < CMD_MAX_LINE - 1)
            buf[bpos++] = (char)c;
    }

    if (bpos == 0)
        return NULL;

    buf[bpos] = '\0';
    return libcmd_strdup(buf);
}

/* -------------------------------------------------------------------------
 * Redirection parsing
 * ---------------------------------------------------------------------- */

static cmd_redir_t *parse_redir(lexer_t *l)
{
    cmd_redir_t *r;
    int c, c2;
    int src_fd = -1;
    redir_type_t type;
    char *file = NULL;

    lexer_skip_spaces(l);
    c = lexer_peek(l);

    /* Check for numeric fd prefix: n> n>> n>&m */
    if (isdigit(c) && l->pos + 1 < l->len) {
        int nc = (unsigned char)l->src[l->pos + 1];
        if (nc == '>' || nc == '<') {
            src_fd = c - '0';
            lexer_get(l); /* consume digit */
            c = lexer_peek(l);
        }
    }

    if (c == '<') {
        lexer_get(l);
        type   = REDIR_IN;
        src_fd = (src_fd < 0) ? 0 : src_fd;
    } else if (c == '>') {
        lexer_get(l);
        c2 = lexer_peek(l);
        if (c2 == '>') {
            lexer_get(l);
            type   = (src_fd == 2) ? REDIR_ERR_APP : REDIR_APPEND;
            src_fd = (src_fd < 0) ? 1 : src_fd;
        } else if (c2 == '&') {
            /* n>&m redirection */
            int dst;
            lexer_get(l);
            c2 = lexer_get(l);
            if (c2 < 0 || !isdigit(c2))
                return NULL;
            dst    = c2 - '0';
            r      = (cmd_redir_t *)calloc(1, sizeof(cmd_redir_t));
            if (r) {
                r->type   = REDIR_FD;
                r->src_fd = (src_fd < 0) ? 1 : src_fd;
                r->dst_fd = dst;
            }
            return r;
        } else {
            type   = (src_fd == 2) ? REDIR_ERR : REDIR_OUT;
            src_fd = (src_fd < 0) ? 1 : src_fd;
        }
    } else {
        return NULL;
    }

    lexer_skip_spaces(l);
    file = read_word(l);
    if (file == NULL)
        return NULL;
    libcmd_path_norm_sep(file);

    r = (cmd_redir_t *)calloc(1, sizeof(cmd_redir_t));
    if (r == NULL) {
        free(file);
        return NULL;
    }
    r->type   = type;
    r->src_fd = src_fd;
    r->file   = file;
    return r;
}

static void redir_free(cmd_redir_t *r)
{
    while (r) {
        cmd_redir_t *next = r->next;
        free(r->file);
        free(r);
        r = next;
    }
}

/* -------------------------------------------------------------------------
 * Node allocation / free
 * ---------------------------------------------------------------------- */

static cmd_node_t *node_alloc(cmd_node_type_t type)
{
    cmd_node_t *n = (cmd_node_t *)calloc(1, sizeof(cmd_node_t));
    if (n) n->type = type;
    return n;
}

void cmd_node_free(cmd_node_t *node)
{
    int i;
    if (node == NULL) return;

    cmd_node_free(node->left);
    cmd_node_free(node->right);
    redir_free(node->redirs);

    if (node->argv) {
        for (i = 0; i < node->argc; i++)
            free(node->argv[i]);
        free(node->argv);
    }
    free(node);
}

/* -------------------------------------------------------------------------
 * Recursive descent parser
 * ---------------------------------------------------------------------- */

/* Forward declarations */
static cmd_node_t *parse_stmt_list(lexer_t *l);
static cmd_node_t *parse_stmt(lexer_t *l);
static cmd_node_t *parse_pipeline(lexer_t *l);
static cmd_node_t *parse_cmd(lexer_t *l);

static cmd_node_t *parse_stmt_list(lexer_t *l)
{
    cmd_node_t *left = parse_stmt(l);

    for (;;) {
        int c;
        cmd_node_t *right, *seq;

        lexer_skip_spaces(l);
        c = lexer_peek(l);

        if (c != '&')
            break;
        /* Make sure it's not && */
        if (lexer_peek2(l) == '&')
            break;

        lexer_get(l); /* consume & */

        right = parse_stmt(l);
        seq   = node_alloc(NODE_SEQ);
        if (seq == NULL) {
            cmd_node_free(right);
            return left;
        }
        seq->left  = left;
        seq->right = right;
        left = seq;
    }
    return left;
}

static cmd_node_t *parse_stmt(lexer_t *l)
{
    cmd_node_t *left = parse_pipeline(l);

    for (;;) {
        int c, c2;
        cmd_node_t *right, *op;
        cmd_node_type_t type;

        lexer_skip_spaces(l);
        c  = lexer_peek(l);
        c2 = lexer_peek2(l);

        if (c == '&' && c2 == '&') {
            type = NODE_AND;
        } else if (c == '|' && c2 == '|') {
            type = NODE_OR;
        } else {
            break;
        }

        lexer_get(l);
        lexer_get(l);

        right = parse_pipeline(l);
        op    = node_alloc(type);
        if (op == NULL) {
            cmd_node_free(right);
            return left;
        }
        op->left  = left;
        op->right = right;
        left = op;
    }
    return left;
}

static cmd_node_t *parse_pipeline(lexer_t *l)
{
    cmd_node_t *left = parse_cmd(l);

    for (;;) {
        int c, c2;
        cmd_node_t *right, *pipe;

        lexer_skip_spaces(l);
        c  = lexer_peek(l);
        c2 = lexer_peek2(l);

        /* | but not || */
        if (c != '|' || c2 == '|')
            break;

        lexer_get(l); /* consume | */

        right = parse_cmd(l);
        pipe  = node_alloc(NODE_PIPE);
        if (pipe == NULL) {
            cmd_node_free(right);
            return left;
        }
        pipe->left  = left;
        pipe->right = right;
        left = pipe;
    }
    return left;
}

static cmd_node_t *parse_cmd(lexer_t *l)
{
    cmd_node_t *node;
    char **args = NULL;
    int    argc = 0, alloc = 0;
    cmd_redir_t *redirs = NULL, **redir_tail = &redirs;
    int c;

    lexer_skip_spaces(l);
    c = lexer_peek(l);

    /* Grouped command: ( stmt_list ) */
    if (c == '(') {
        cmd_node_t *inner;
        lexer_get(l);
        inner = parse_stmt_list(l);
        lexer_skip_spaces(l);
        if (lexer_peek(l) == ')')
            lexer_get(l);
        node = node_alloc(NODE_GROUP);
        if (node) node->left = inner;
        return node;
    }

    node = node_alloc(NODE_SIMPLE);
    if (node == NULL)
        return NULL;

    for (;;) {
        cmd_redir_t *r;
        char *w;
        char *file = NULL;

        lexer_skip_spaces(l);
        c = lexer_peek(l);

        /* End of input or operator */
        if (c < 0 || c == '\n' || c == '\r')
            break;

        /* &> file and &>> file: redirect stdout AND stderr to a file */
        if (c == '&' && l->pos + 1 < l->len &&
            l->src[l->pos + 1] == '>') {
            int append = 0;
            lexer_get(l);            /* consume '&' */
            lexer_get(l);            /* consume '>' */
            if (l->pos < l->len && l->src[l->pos] == '>') {
                append = 1;
                lexer_get(l);
            }
            lexer_skip_spaces(l);
            file = read_word(l);
            if (file == NULL)
                break;
            r = (cmd_redir_t *)calloc(1, sizeof(cmd_redir_t));
            if (r) {
                r->type  = append ? REDIR_OUT_ERR_APP : REDIR_OUT_ERR;
                r->file  = file;
                *redir_tail = r;
                redir_tail  = &r->next;
            } else {
                free(file);
                break;
            }
            continue;
        }

        if (c == '|' || c == '&' || c == ')')
            break;

        /* Redirection? */
        if (c == '<' || c == '>') {
            r = parse_redir(l);
            if (r) {
                *redir_tail = r;
                redir_tail  = &r->next;
            }
            continue;
        }
        /* Digit followed by > or < : potential redirection */
        if (isdigit(c) && l->pos + 1 < l->len) {
            int nc = (unsigned char)l->src[l->pos + 1];
            if (nc == '>' || nc == '<') {
                r = parse_redir(l);
                if (r) {
                    *redir_tail = r;
                    redir_tail  = &r->next;
                }
                continue;
            }
        }

        /*
         * Parenthesized set: (items...) - used by FOR and IF.
         * We collect the entire balanced (...) as a single token so that
         * builtins can parse their own set syntax.  We only do this when
         * there are already words accumulated (i.e. this is NOT the very
         * first token of the command, which would be a real group).
         */
        if (c == '(' && argc > 0) {
            char paren_buf[CMD_MAX_LINE];
            int ppos = 0, depth = 0;
            lexer_get(l);           /* consume '(' */
            paren_buf[ppos++] = '(';
            depth = 1;
            while (l->pos < l->len && depth > 0) {
                int pc = lexer_get(l);
                if (pc == '(') depth++;
                else if (pc == ')') { depth--; }
                if (ppos < CMD_MAX_LINE - 1)
                    paren_buf[ppos++] = (char)pc;
            }
            paren_buf[ppos] = '\0';
            w = libcmd_strdup(paren_buf);
            if (w == NULL) break;
            if (argc >= alloc) {
                char **tmp;
                alloc = alloc ? alloc * 2 : 8;
                tmp   = (char **)realloc(args, (size_t)(alloc + 1) * sizeof(char *));
                if (tmp == NULL) { free(w); break; }
                args = tmp;
            }
            args[argc++] = w;
            continue;
        }

        /* Regular word */
        w = read_word(l);
        if (w == NULL)
            break;

        if (argc >= alloc) {
            char **tmp;
            alloc = alloc ? alloc * 2 : 8;
            tmp   = (char **)realloc(args, (size_t)(alloc + 1) * sizeof(char *));
            if (tmp == NULL) {
                free(w);
                break;
            }
            args = tmp;
        }
        args[argc++] = w;
    }

    if (args)
        args[argc] = NULL;

    node->argc  = argc;
    node->argv  = args;
    node->redirs = redirs;

    if (argc == 0 && redirs == NULL) {
        cmd_node_free(node);
        return NULL;
    }

    return node;
}

/* -------------------------------------------------------------------------
 * Public entry point
 * ---------------------------------------------------------------------- */

cmd_node_t *cmd_parse(const char *line)
{
    lexer_t l;

    if (line == NULL)
        return NULL;

    /* Skip leading whitespace and REM comments */
    while (*line == ' ' || *line == '\t') line++;

    lexer_init(&l, line);
    return parse_stmt_list(&l);
}

/* -------------------------------------------------------------------------
 * Node execution (called by cinterp.c)
 * ---------------------------------------------------------------------- */

/*
 * Forward-declare functions from other files.
 * The actual implementations are in cinterp.c and cdispatch.c.
 */
extern int cmd_dispatch(cmd_context_t *ctx, int argc, char **argv,
                        int stdin_fd, int stdout_fd, int stderr_fd);

static int exec_simple(cmd_context_t *ctx, cmd_node_t *node,
                       int stdin_fd, int stdout_fd, int stderr_fd);
static int exec_node_fds(cmd_context_t *ctx, cmd_node_t *node,
                         int stdin_fd, int stdout_fd, int stderr_fd);

static int exec_simple(cmd_context_t *ctx, cmd_node_t *node,
                       int stdin_fd, int stdout_fd, int stderr_fd)
{
    if (node == NULL || node->argc == 0)
        return 0;
    return cmd_dispatch(ctx, node->argc, node->argv,
                        stdin_fd, stdout_fd, stderr_fd);
}

/* Apply redirections of one node to the real standard fds (0/1/2).
 * Returns 0 on success, -1 on open failure (an error message is
 * printed).  Used for top-level redirections and inside pipeline
 * children, where the fds are dup2'd directly. */
static int apply_redirs(cmd_redir_t *redirs)
{
    cmd_redir_t *r;

    for (r = redirs; r; r = r->next) {
        int fd = -1;

        switch (r->type) {
        case REDIR_IN:
            fd = libcmd_open(r->file, LIBCMD_O_RDONLY, 0);
            if (fd < 0) {
                fprintf(stderr, "%s",
                        cmd_gettext(MSG_ERR_FILE_NOT_FOUND));
                return -1;
            }
            libcmd_dup2(fd, LIBCMD_STDIN_FILENO);
            libcmd_close(fd);
            break;
        case REDIR_OUT:
            fd = libcmd_open(r->file,
                             LIBCMD_O_WRONLY | LIBCMD_O_CREAT | LIBCMD_O_TRUNC,
                             0644);
            if (fd < 0) {
                fprintf(stderr, cmd_gettext(MSG_ERR_CANNOT_CREATE_FILE),
                        r->file);
                return -1;
            }
            libcmd_dup2(fd, LIBCMD_STDOUT_FILENO);
            libcmd_close(fd);
            break;
        case REDIR_APPEND:
            fd = libcmd_open(r->file,
                             LIBCMD_O_WRONLY | LIBCMD_O_CREAT | LIBCMD_O_APPEND,
                             0644);
            if (fd < 0) {
                fprintf(stderr, cmd_gettext(MSG_ERR_CANNOT_CREATE_FILE),
                        r->file);
                return -1;
            }
            libcmd_dup2(fd, LIBCMD_STDOUT_FILENO);
            libcmd_close(fd);
            break;
        case REDIR_ERR:
            fd = libcmd_open(r->file,
                             LIBCMD_O_WRONLY | LIBCMD_O_CREAT | LIBCMD_O_TRUNC,
                             0644);
            if (fd < 0) {
                fprintf(stderr, cmd_gettext(MSG_ERR_CANNOT_CREATE_FILE),
                        r->file);
                return -1;
            }
            libcmd_dup2(fd, LIBCMD_STDERR_FILENO);
            libcmd_close(fd);
            break;
        case REDIR_ERR_APP:
            fd = libcmd_open(r->file,
                             LIBCMD_O_WRONLY | LIBCMD_O_CREAT | LIBCMD_O_APPEND,
                             0644);
            if (fd < 0) {
                fprintf(stderr, cmd_gettext(MSG_ERR_CANNOT_CREATE_FILE),
                        r->file);
                return -1;
            }
            libcmd_dup2(fd, LIBCMD_STDERR_FILENO);
            libcmd_close(fd);
            break;
        case REDIR_FD:
            libcmd_dup2(r->dst_fd, r->src_fd);
            break;
        case REDIR_OUT_ERR:
        case REDIR_OUT_ERR_APP:
            fd = libcmd_open(r->file,
                             (r->type == REDIR_OUT_ERR)
                                 ? LIBCMD_O_WRONLY | LIBCMD_O_CREAT | LIBCMD_O_TRUNC
                                 : LIBCMD_O_WRONLY | LIBCMD_O_CREAT | LIBCMD_O_APPEND,
                             0644);
            if (fd < 0) {
                fprintf(stderr, cmd_gettext(MSG_ERR_CANNOT_CREATE_FILE),
                        r->file);
                return -1;
            }
            libcmd_dup2(fd, LIBCMD_STDOUT_FILENO);
            libcmd_dup2(fd, LIBCMD_STDERR_FILENO);
            libcmd_close(fd);
            break;
        }
    }

    return 0;
}

/* Flatten a NODE_PIPE chain into its stages, in pipeline order */
static int collect_pipe_stages(cmd_node_t *node, cmd_node_t **stages, int max)
{
    int n;

    if (node->type == NODE_PIPE) {
        n = collect_pipe_stages(node->left, stages, max);
        if (n < 0 || n >= max)
            return -1;
        stages[n++] = node->right;
    } else {
        if (max <= 0)
            return -1;
        n = 1;
        stages[0] = node;
    }
    return n;
}

static int exec_node_fds(cmd_context_t *ctx, cmd_node_t *node,
                         int stdin_fd, int stdout_fd, int stderr_fd)
{
    int ret = 0;

    if (node == NULL)
        return 0;

    switch (node->type) {
    case NODE_SIMPLE:
        ret = exec_simple(ctx, node, stdin_fd, stdout_fd, stderr_fd);
        break;

    case NODE_GROUP:
        ret = exec_node_fds(ctx, node->left, stdin_fd, stdout_fd, stderr_fd);
        break;

    case NODE_PIPE: {
        /* Run all pipeline stages concurrently: every stage executes in
         * its own forked child (matching cmd.exe, where variable changes
         * inside a pipeline do not persist), with pipes between them. */
        cmd_node_t *stages[64];
        int n = collect_pipe_stages(node, stages, 64);
        int prev_read = stdin_fd;
        int pids[64];
        int i;

        if (n <= 0)
            return 1;

        for (i = 0; i < n; i++) {
            int fds[2] = { -1, -1 };
            pid_t pid;

            if (i + 1 < n) {
                if (libcmd_pipe(fds) < 0)
                    return 1;
            }

            /* Flush stdio before forking so the child inherits clean
             * buffers (prevents duplicate output on the child's fflush) */
            fflush(NULL);

            pid = libcmd_fork();
            if (pid < 0) {
                if (fds[0] >= 0) libcmd_close(fds[0]);
                if (fds[1] >= 0) libcmd_close(fds[1]);
                return 1;
            }

            if (pid == 0) {
                int r;

                /* Child: wire up stdin/stdout, close all pipe ends */
                if (prev_read >= 0 && prev_read != LIBCMD_STDIN_FILENO)
                    libcmd_dup2(prev_read, LIBCMD_STDIN_FILENO);
                if (i + 1 < n)
                    libcmd_dup2(fds[1], LIBCMD_STDOUT_FILENO);
                else if (stdout_fd >= 0 && stdout_fd != LIBCMD_STDOUT_FILENO)
                    libcmd_dup2(stdout_fd, LIBCMD_STDOUT_FILENO);
                if (fds[0] >= 0) libcmd_close(fds[0]);
                if (fds[1] >= 0) libcmd_close(fds[1]);
                if (prev_read >= 0 && prev_read != LIBCMD_STDIN_FILENO)
                    libcmd_close(prev_read);

                /* Reset signal dispositions: the shell's SIGINT handler
                 * must not longjmp across the fork, and SIGPIPE must
                 * terminate pipeline stages like a real shell */
                signal(SIGINT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);
                signal(SIGPIPE, SIG_DFL);

                /* Apply this stage's own redirections */
                if (apply_redirs(stages[i]->redirs) < 0)
                    libcmd_exit(1);

                r = exec_node_fds(ctx, stages[i],
                                  LIBCMD_STDIN_FILENO,
                                  LIBCMD_STDOUT_FILENO,
                                  LIBCMD_STDERR_FILENO);

                /* Flush stdio buffers written by builtins before _exit */
                fflush(NULL);
                libcmd_exit(r);
            }

            /* Parent */
            pids[i] = (int)pid;
            if (prev_read >= 0 && prev_read != stdin_fd)
                libcmd_close(prev_read);
            if (i + 1 < n) {
                libcmd_close(fds[1]);
                prev_read = fds[0];
            }
        }

        /* Wait for all children; the exit code is the last stage's */
        ret = 0;
        for (i = 0; i < n; i++) {
            libcmd_exit_info_t ei;
            if (libcmd_wait_pid(pids[i], &ei) == 0 && i == n - 1)
                ret = ei.exit_code;
        }
        break;
    }

    case NODE_AND:
        ret = exec_node_fds(ctx, node->left, stdin_fd, stdout_fd, stderr_fd);
        if (ret == 0)
            ret = exec_node_fds(ctx, node->right, stdin_fd, stdout_fd, stderr_fd);
        break;

    case NODE_OR:
        ret = exec_node_fds(ctx, node->left, stdin_fd, stdout_fd, stderr_fd);
        if (ret != 0)
            ret = exec_node_fds(ctx, node->right, stdin_fd, stdout_fd, stderr_fd);
        break;

    case NODE_SEQ:
        exec_node_fds(ctx, node->left, stdin_fd, stdout_fd, stderr_fd);
        ret = exec_node_fds(ctx, node->right, stdin_fd, stdout_fd, stderr_fd);
        break;
    }

    return ret;
}

/* Apply redirections and execute */
int cmd_exec_node(cmd_context_t *ctx, cmd_node_t *node)
{
    int old_stdin, old_stdout, old_stderr;
    int ret;

    if (node == NULL)
        return 0;

    /* Save current fds */
    old_stdin  = libcmd_dup(LIBCMD_STDIN_FILENO);
    old_stdout = libcmd_dup(LIBCMD_STDOUT_FILENO);
    old_stderr = libcmd_dup(LIBCMD_STDERR_FILENO);

    /* Apply any redirections on this node */
    if (apply_redirs(node->redirs) < 0) {
        ret = 1;
        ctx->exit_code = 1;
    } else {
        ret = exec_node_fds(ctx, node, LIBCMD_STDIN_FILENO,
                            LIBCMD_STDOUT_FILENO, LIBCMD_STDERR_FILENO);
    }

    /* Restore original fds */
    if (old_stdin  >= 0) { libcmd_dup2(old_stdin,  LIBCMD_STDIN_FILENO);  libcmd_close(old_stdin);  }
    if (old_stdout >= 0) { libcmd_dup2(old_stdout, LIBCMD_STDOUT_FILENO); libcmd_close(old_stdout); }
    if (old_stderr >= 0) { libcmd_dup2(old_stderr, LIBCMD_STDERR_FILENO); libcmd_close(old_stderr); }

    return ret;
}
