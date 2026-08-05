/*
 * bbreak.c - BREAK builtin
 *
 * BREAK [ON | OFF]
 *
 * On Unix, CTRL+C checking is always on; this builtin is a no-op
 * but accepted for compatibility.
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>

int builtin_break(cmd_context_t *ctx, int argc, char **argv)
{
    (void)ctx;
    (void)argc;
    (void)argv;
    /* No-op on Unix */
    return 0;
}
