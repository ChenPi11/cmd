/*
 * bset.c - SET builtin
 *
 * SET [variable=[string]]
 * SET /A expression
 * SET /P variable=[promptString]
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Simple arithmetic expression evaluator for SET /A
 * Supports: + - * / % () ! ~ unary- unary~ & ^ | << >> = op= ,
 * ---------------------------------------------------------------------- */

typedef struct {
    const char *p;
    int err;
} eval_ctx_t;

/* cmd.exe evaluates with 32-bit signed integers (wrap-around) */
static long wrap32(long v)
{
    return (long)(int)v;
}

static long eval_expr(eval_ctx_t *e);

static void skip_ws(eval_ctx_t *e)
{
    while (*e->p == ' ' || *e->p == '\t') e->p++;
}

static long eval_primary(eval_ctx_t *e)
{
    long val = 0;
    char *end;

    skip_ws(e);

    if (*e->p == '(') {
        e->p++;
        val = eval_expr(e);
        skip_ws(e);
        if (*e->p == ')') e->p++;
        return val;
    }

    /* Unary operators */
    if (*e->p == '-') { e->p++; return -eval_primary(e); }
    if (*e->p == '~') { e->p++; return ~eval_primary(e); }
    if (*e->p == '!') { e->p++; return !eval_primary(e); }

    /* Hex literal */
    if (e->p[0] == '0' && (e->p[1] == 'x' || e->p[1] == 'X')) {
        val = strtol(e->p, &end, 16);
        e->p = end;
        return wrap32(val);
    }

    /* Octal literal */
    if (e->p[0] == '0' && isdigit((unsigned char)e->p[1])) {
        val = strtol(e->p, &end, 8);
        e->p = end;
        return wrap32(val);
    }

    /* Decimal literal */
    if (isdigit((unsigned char)*e->p) || *e->p == '+') {
        val = strtol(e->p, &end, 10);
        e->p = end;
        return wrap32(val);
    }

    /* Variable name */
    if (isalpha((unsigned char)*e->p) || *e->p == '_') {
        char name[256];
        int npos = 0;
        while ((isalnum((unsigned char)*e->p) || *e->p == '_') && npos < 255)
            name[npos++] = *e->p++;
        name[npos] = '\0';
        {
            const char *v = libcmd_getenv(name);
            if (v) val = strtol(v, NULL, 0);
        }
        return wrap32(val);
    }

    e->err = 1;
    return 0;
}

static long eval_mul(eval_ctx_t *e)
{
    long left = eval_primary(e);
    for (;;) {
        skip_ws(e);
        if (*e->p == '*') { e->p++; left = wrap32(left * eval_primary(e)); }
        else if (*e->p == '/' && e->p[1] != '/') {
            long r; e->p++; r = eval_primary(e);
            left = r ? wrap32(left / r) : 0;
        }
        else if (*e->p == '%') { e->p++; { long r = eval_primary(e); left = r ? wrap32(left % r) : 0; } }
        else break;
    }
    return left;
}

static long eval_add(eval_ctx_t *e)
{
    long left = eval_mul(e);
    for (;;) {
        skip_ws(e);
        if (*e->p == '+' && e->p[1] != '=') { e->p++; left = wrap32(left + eval_mul(e)); }
        else if (*e->p == '-' && e->p[1] != '=') { e->p++; left = wrap32(left - eval_mul(e)); }
        else break;
    }
    return left;
}

static long eval_shift(eval_ctx_t *e)
{
    long left = eval_add(e);
    for (;;) {
        skip_ws(e);
        if (e->p[0]=='<' && e->p[1]=='<') { e->p+=2; left = wrap32(left << eval_add(e)); }
        else if (e->p[0]=='>' && e->p[1]=='>') { e->p+=2; left = wrap32(left >> eval_add(e)); }
        else break;
    }
    return left;
}

static long eval_bitand(eval_ctx_t *e)
{
    long left = eval_shift(e);
    for (;;) { skip_ws(e); if (*e->p=='&' && e->p[1]!='&') { e->p++; left = wrap32(left & eval_shift(e)); } else break; }
    return left;
}
static long eval_bitxor(eval_ctx_t *e)
{
    long left = eval_bitand(e);
    for (;;) { skip_ws(e); if (*e->p=='^') { e->p++; left = wrap32(left ^ eval_bitand(e)); } else break; }
    return left;
}
static long eval_bitor(eval_ctx_t *e)
{
    long left = eval_bitxor(e);
    for (;;) { skip_ws(e); if (*e->p=='|' && e->p[1]!='|') { e->p++; left = wrap32(left | eval_bitxor(e)); } else break; }
    return left;
}

static long eval_expr(eval_ctx_t *e)
{
    return eval_bitor(e);
}

/* -------------------------------------------------------------------------
 * SET /A
 * ---------------------------------------------------------------------- */

static int do_set_a(const char *expr, int hex_out)
{
    eval_ctx_t e;
    long result;

    /* Handle comma-separated expressions */
    e.p   = expr;
    e.err = 0;

    do {
        skip_ws(&e);
        /* Check for assignment: NAME op= expr or NAME = expr */
        {
            const char *start = e.p;
            char name[256];
            int npos = 0;
            while ((isalnum((unsigned char)*e.p) || *e.p == '_') && npos < 255)
                name[npos++] = *e.p++;
            name[npos] = '\0';
            skip_ws(&e);

            if (npos > 0 && (*e.p == '=' ||
                ((*e.p == '+' || *e.p == '-' || *e.p == '*' || *e.p == '/' ||
                  *e.p == '%' || *e.p == '&' || *e.p == '^' || *e.p == '|') &&
                 e.p[1] == '='))) {
                char op = *e.p;
                if (e.p[1] == '=') e.p += 2; else e.p++;
                result = eval_expr(&e);

                if (op != '=') {
                    const char *cur = libcmd_getenv(name);
                    long cur_val = cur ? strtol(cur, NULL, 0) : 0;
                    switch (op) {
                    case '+': result = wrap32(cur_val + result); break;
                    case '-': result = wrap32(cur_val - result); break;
                    case '*': result = wrap32(cur_val * result); break;
                    case '/': result = result ? wrap32(cur_val / result) : 0; break;
                    case '%': result = result ? wrap32(cur_val % result) : 0; break;
                    case '&': result = wrap32(cur_val & result); break;
                    case '^': result = wrap32(cur_val ^ result); break;
                    case '|': result = wrap32(cur_val | result); break;
                    }
                }
                {
                    char val_str[32];
                    libcmd_sprintf_s(val_str, sizeof(val_str), "%ld", wrap32(result));
                    libcmd_setenv(name, val_str, 1);
                }
            } else {
                /* Not an assignment; just evaluate */
                e.p = start;
                result = wrap32(eval_expr(&e));
            }
        }

        skip_ws(&e);
        if (*e.p == ',') e.p++;
        else break;
    } while (!e.err);

    if (e.err) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_SET_EXPR));
        return 1;
    }

    if (hex_out)
        printf("0x%x\n", (unsigned int)result);
    else
        printf("%ld\n", result);
    return 0;
}

/* -------------------------------------------------------------------------
 * SET /P
 * ---------------------------------------------------------------------- */

static int do_set_p(const char *rest)
{
    /* rest = "VAR=[promptString]" */
    const char *eq = strchr(rest, '=');
    char var_name[256];
    char line[CMD_MAX_LINE];

    if (eq == NULL) {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_SET_P_EQUALS));
        return 1;
    }

    {
        size_t nlen = (size_t)(eq - rest);
        if (nlen >= sizeof(var_name)) nlen = sizeof(var_name) - 1;
        memcpy(var_name, rest, nlen);
        var_name[nlen] = '\0';
    }

    /* Print prompt string */
    fputs(eq + 1, stdout);
    fflush(stdout);

    if (fgets(line, sizeof(line), stdin) == NULL)
        return 1;

    /* Strip trailing newline */
    {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
    }

    libcmd_setenv(var_name, line, 1);
    return 0;
}

/* -------------------------------------------------------------------------
 * Main SET logic
 * ---------------------------------------------------------------------- */

int builtin_set(cmd_context_t *ctx, int argc, char **argv)
{
    (void)ctx;

    if (argc <= 1) {
        /* Print all environment variables */
        char **env = libcmd_get_environ();
        if (env) {
            int i;
            for (i = 0; env[i]; i++)
                printf("%s\n", env[i]);
        }
        cmd_print_dynamic_vars(ctx, NULL);
        return 0;
    }

    if (libcmd_strcasecmp(argv[1], "/?") == 0) {
        fputs(cmd_gettext(MSG_HELP_SET), stdout);
        return 0;
    }

    /* SET /A arithmetic */
    if (libcmd_strcasecmp(argv[1], "/A") == 0) {
        int hex_out = 0;
        int start_i = 2;

        /* SET /A /X expr - display the result in hexadecimal (extension) */
        if (argc > start_i && libcmd_is_switch(argv[start_i], "X")) {
            hex_out = 1;
            start_i++;
        }
        /* /M is accepted and ignored (cmd.exe has no such option either) */
        if (argc > start_i && libcmd_is_switch(argv[start_i], "M"))
            start_i++;

        if (argc <= start_i) {
            fprintf(stderr, "%s", cmd_gettext(MSG_ERR_SET_A_EXPR));
            return 1;
        }
        /* Reconstruct expression from remaining args */
        {
            char expr[CMD_MAX_LINE];
            int i;
            expr[0] = '\0';
            for (i = start_i; i < argc; i++) {
                if (i > start_i) strncat(expr, " ", sizeof(expr) - strlen(expr) - 1);
                strncat(expr, argv[i], sizeof(expr) - strlen(expr) - 1);
            }
            return do_set_a(expr, hex_out);
        }
    }

    /* SET /P prompt */
    if (libcmd_strcasecmp(argv[1], "/P") == 0) {
        if (argc < 3) {
            fprintf(stderr, "%s", cmd_gettext(MSG_ERR_SET_P_VAR));
            return 1;
        }
        return do_set_p(argv[2]);
    }

    /*
     * SET variable=[value]  or  SET prefixOnly
     * argv[1] may already contain the full "VAR=VALUE" because the shell
     * parser treats 'SET VAR=VALUE' with the '=' as part of a single token
     * in some parsers.  However our parser splits on spaces so we get:
     *   argv[1] = "VAR=VALUE"    (if typed: set VAR=VALUE)
     * or
     *   argv[1] = "VAR"          (if typed: set VAR  -> display)
     */
    {
        char *full;
        const char *eq;

        /* Join all args after SET to form "VAR=VALUE" */
        {
            char buf[CMD_MAX_LINE];
            int i;
            buf[0] = '\0';
            for (i = 1; i < argc; i++) {
                if (i > 1) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
            }
            full = libcmd_strdup(buf);
        }

        if (full == NULL) return 1;

        eq = strchr(full, '=');
        if (eq == NULL) {
            /* No '=': display all vars matching the prefix */
            size_t plen = strlen(full);
            char **env   = libcmd_get_environ();
            int found = 0;

            if (env) {
                int i;
                for (i = 0; env[i]; i++) {
                    if (libcmd_strncasecmp(env[i], full, plen) == 0 &&
                        env[i][plen] == '=') {
                        printf("%s\n", env[i]);
                        found = 1;
                    }
                }
            }
            found += cmd_print_dynamic_vars(ctx, full);

            free(full);
            if (!found) {
                fprintf(stderr, "%s",
                        cmd_gettext(MSG_ERR_ENV_NOT_DEFINED));
                return 1;
            }
            return 0;
        }

        /* VAR= (delete) or VAR=VALUE (set) */
        {
            char var_name[256];
            size_t nlen = (size_t)(eq - full);
            if (nlen >= sizeof(var_name)) nlen = sizeof(var_name) - 1;
            memcpy(var_name, full, nlen);
            var_name[nlen] = '\0';

            if (*(eq + 1) == '\0') {
                /* Delete the variable */
                libcmd_unsetenv(var_name);
            } else {
                libcmd_setenv(var_name, eq + 1, 1);
            }
        }

        free(full);
        return 0;
    }
}
