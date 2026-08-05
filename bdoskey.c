/*
 * bdoskey.c - DOSKEY builtin
 *
 * DOSKEY [macroname=[commandline]]
 * DOSKEY /MACROS [ALL]
 * DOSKEY /HISTORY
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <stdio.h>
#include <string.h>

int builtin_doskey(cmd_context_t *ctx, int argc, char **argv)
{
    int i;

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_DOSKEY), stdout);
        return 0;
    }

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/MACROS") == 0) {
        for (i = 0; i < ctx->macro_count; i++)
            printf("%s=%s\n", ctx->macros[i].name, ctx->macros[i].value);
        return 0;
    }

    if (argc >= 2 && libcmd_strcasecmp(argv[1], "/HISTORY") == 0) {
        FILE *f = fopen(".cmd_history", "r");
        char line[CMD_MAX_LINE];
        if (f == NULL) {
            const char *home = libcmd_getenv("HOME");
            if (home) {
                char path[CMD_MAX_PATH];
                libcmd_sprintf_s(path, sizeof(path), "%s/.cmd_history", home);
                f = fopen(path, "r");
            }
        }
        if (f == NULL)
            return 0;
        while (fgets(line, sizeof(line), f) != NULL) {
            line[strcspn(line, "\n")] = '\0';
            printf("%s\n", line);
        }
        fclose(f);
        return 0;
    }

    if (argc >= 2) {
        /* macro definition: name=command */
        const char *eq = strchr(argv[1], '=');
        if (eq != NULL) {
            size_t nlen = (size_t)(eq - argv[1]);
            char name[64];
            int existing = -1;

            if (nlen >= sizeof(name))
                nlen = sizeof(name) - 1;
            memcpy(name, argv[1], nlen);
            name[nlen] = '\0';

            for (i = 0; i < ctx->macro_count; i++) {
                if (libcmd_strcasecmp(ctx->macros[i].name, name) == 0) {
                    existing = i;
                    break;
                }
            }

            if (eq[1] == '\0') {
                /* delete */
                if (existing >= 0) {
                    for (i = existing; i < ctx->macro_count - 1; i++)
                        ctx->macros[i] = ctx->macros[i + 1];
                    ctx->macro_count--;
                }
                return 0;
            }

            if (existing < 0) {
                if (ctx->macro_count >= CMD_DOSKEY_MACROS)
                    return 1;
                existing = ctx->macro_count++;
                memset(ctx->macros[existing].name, 0,
                       sizeof(ctx->macros[existing].name));
            }
            libcmd_sprintf_s(ctx->macros[existing].name,
                             sizeof(ctx->macros[existing].name), "%s", name);
            {
                /* body = text after '=' in argv[1], plus remaining argv */
                char body[CMD_MAX_LINE];
                size_t blen = 0;
                const char *tail = eq + 1;
                while (*tail && blen + 1 < sizeof(body))
                    body[blen++] = *tail++;
                for (i = 2; i < argc && blen + 2 < sizeof(body); i++) {
                    size_t len = strlen(argv[i]);
                    if (blen + 1 + len >= sizeof(body))
                        break;
                    body[blen++] = ' ';
                    memcpy(body + blen, argv[i], len);
                    blen += len;
                }
                body[blen] = '\0';
                libcmd_sprintf_s(ctx->macros[existing].value,
                                 sizeof(ctx->macros[existing].value), "%s",
                                 body);
            }
            return 0;
        }
    }

    /* No arguments or unrecognized: list macros */
    for (i = 0; i < ctx->macro_count; i++)
        printf("%s=%s\n", ctx->macros[i].name, ctx->macros[i].value);
    return 0;
}

/* -------------------------------------------------------------------------
 * Macro expansion (called from cmd_run_line before variable expansion)
 * line is rewritten in place if the first word matches a macro.
 * ---------------------------------------------------------------------- */

void cmd_doskey_expand(cmd_context_t *ctx, char *line, size_t size)
{
    const char *p = line;
    const char *name_start, *name_end;
    char name[64];
    const cmd_macro_t *m = NULL;
    int i;
    char out[CMD_MAX_LINE * 2];
    size_t o = 0;
    const char *args;

    if (ctx->macro_count <= 0)
        return;

    while (*p == ' ' || *p == '\t')
        p++;
    name_start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '=')
        p++;
    name_end = p;
    if (name_end == name_start)
        return;

    {
        size_t nlen = (size_t)(name_end - name_start);
        if (nlen >= sizeof(name))
            nlen = sizeof(name) - 1;
        memcpy(name, name_start, nlen);
        name[nlen] = '\0';
    }

    for (i = 0; i < ctx->macro_count; i++) {
        if (libcmd_strcasecmp(ctx->macros[i].name, name) == 0) {
            m = &ctx->macros[i];
            break;
        }
    }
    if (m == NULL)
        return;

    args = name_end;
    while (*args == ' ' || *args == '\t')
        args++;

    /* Replace $* / $1-$9 / $T / $$ in the macro body */
    {
        const char *v = m->value;
        while (*v && o + 1 < sizeof(out)) {
            if (*v == '$' && v[1] != '\0') {
                char c = v[1];
                if (c == '*') {
                    const char *a = args;
                    while (*a && o + 1 < sizeof(out))
                        out[o++] = *a++;
                    v += 2;
                    continue;
                } else if (c >= '1' && c <= '9') {
                    /* take the n-th argument */
                    int n = c - '1';
                    const char *a = args;
                    int k;
                    for (k = 0; k < n && *a; k++) {
                        while (*a && *a != ' ') a++;
                        while (*a == ' ') a++;
                    }
                    while (*a && *a != ' ' && o + 1 < sizeof(out))
                        out[o++] = *a++;
                    v += 2;
                    continue;
                } else if (c == 'T') {
                    out[o++] = '&';
                    v += 2;
                    continue;
                } else if (c == '$') {
                    out[o++] = '$';
                    v += 2;
                    continue;
                }
            }
            out[o++] = *v++;
        }
        /* No $* / $n parameter marker in the macro:
         * append the call arguments (cmd.exe behaviour) */
        if (strstr(m->value, "$*") == NULL &&
            strchr(m->value, '$') == NULL) {
            if (*args && o + 1 < sizeof(out))
                out[o++] = ' ';
            while (*args && o + 1 < sizeof(out))
                out[o++] = *args++;
        }
        out[o] = '\0';
    }

    if (o < size)
        libcmd_sprintf_s(line, size, "%s", out);
}
