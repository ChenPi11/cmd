/*
 * brem.c - REM builtin
 *
 * REM [comment]
 *
 * REM is a no-op; comments in batch files.
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

int builtin_rem(cmd_context_t *ctx, int argc, char **argv)
{
    (void)ctx;
    (void)argc;
    (void)argv;
    return 0;
}
