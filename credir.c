/*
 * credir.c - I/O redirection helpers
 *
 * These are thin wrappers; the main redirection logic lives in cparser.c
 * inside cmd_exec_node().  This file provides helpers used by builtins.
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

int cmd_apply_redir(cmd_context_t *ctx, cmd_node_t *node)
{
    /* Redirections are applied by cmd_exec_node() in cparser.c */
    (void)ctx;
    (void)node;
    return 0;
}

void cmd_restore_redir(cmd_context_t *ctx,
                       int old_stdin, int old_stdout, int old_stderr)
{
    (void)ctx;
    if (old_stdin  >= 0) { libcmd_dup2(old_stdin,  LIBCMD_STDIN_FILENO);  libcmd_close(old_stdin);  }
    if (old_stdout >= 0) { libcmd_dup2(old_stdout, LIBCMD_STDOUT_FILENO); libcmd_close(old_stdout); }
    if (old_stderr >= 0) { libcmd_dup2(old_stderr, LIBCMD_STDERR_FILENO); libcmd_close(old_stderr); }
}
