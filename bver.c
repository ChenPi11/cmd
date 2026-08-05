/*
 * bver.c - VER builtin
 *
 * VER
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>

int builtin_ver(cmd_context_t *ctx, int argc, char **argv)
{
    (void)ctx;
    (void)argc;
    (void)argv;

    printf("%s", cmd_get_banner());
    return 0;
}
