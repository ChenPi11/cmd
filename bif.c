/*
 * bif.c - IF builtin
 *
 * IF [NOT] ERRORLEVEL number command
 * IF [NOT] string1==string2 command
 * IF [NOT] EXIST filename command
 * IF [NOT] DEFINED variable command
 * IF [/I] string1 compare-op string2 command
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declaration */
extern int cmd_run_line(cmd_context_t *ctx, const char *line);

/*
 * Evaluate the condition part of IF.
 * Returns 1 if condition is true, 0 if false, -1 on error.
 * *rest_out is set to the remainder of the line after the condition.
 */
static int eval_condition(cmd_context_t *ctx, int argc, char **argv, int *argi, int case_insensitive)
{
    int negate = 0;
    const char *kw;

    /* Skip NOT keyword */
    if (*argi < argc && libcmd_strcasecmp(argv[*argi], "NOT") == 0)
    {
        negate = 1;
        (*argi)++;
    }

    if (*argi >= argc)
        return -1;
    kw = argv[*argi];

    /* ERRORLEVEL number */
    if (libcmd_strcasecmp(kw, "ERRORLEVEL") == 0)
    {
        int number;
        (*argi)++;
        if (*argi >= argc)
            return -1;
        number = atoi(argv[(*argi)++]);
        {
            int result = (ctx->exit_code >= number) ? 1 : 0;
            return negate ? !result : result;
        }
    }

    /* EXIST filename */
    if (libcmd_strcasecmp(kw, "EXIST") == 0)
    {
        const char *filename;
        int result;
        (*argi)++;
        if (*argi >= argc)
            return -1;
        filename = argv[(*argi)++];
        result = (libcmd_access(filename, 0) == 0) ? 1 : 0;
        if (!result && (strchr(filename, '*') || strchr(filename, '?'))) {
            /* cmd.exe: EXIST accepts wildcards - true if anything matches */
            libcmd_glob_result_t gr;
            size_t g;
            result = 0;
            if (libcmd_glob(filename, &gr) == 0) {
                for (g = 0; g < gr.count; g++) {
                    if (libcmd_access(gr.paths[g], 0) == 0) {
                        result = 1;
                        break;
                    }
                }
            }
            libcmd_glob_free(&gr);
        }
        return negate ? !result : result;
    }

    /* DEFINED variable */
    if (libcmd_strcasecmp(kw, "DEFINED") == 0)
    {
        const char *varname;
        int result;
        (*argi)++;
        if (*argi >= argc)
            return -1;
        varname = argv[(*argi)++];
        result = (libcmd_getenv(varname) != NULL) ? 1 : 0;
        return negate ? !result : result;
    }

    /* CMDEXTVERSION number */
    if (libcmd_strcasecmp(kw, "CMDEXTVERSION") == 0)
    {
        int number;
        (*argi)++;
        if (*argi >= argc)
            return -1;
        number = atoi(argv[(*argi)++]);
        {
            int result = (2 >= number) ? 1 : 0;
            return negate ? !result : result;
        }
    }

    /* string1 compare-op string2   or   string1==string2 */
    {
        const char *s1 = argv[(*argi)++];
        const char *op;
        const char *s2;
        int result = 0;
        int numeric = 0;

        if (*argi >= argc)
            return -1;
        op = argv[(*argi)++];
        if (*argi >= argc)
            return -1;
        s2 = argv[(*argi)++];

        /* Check if this is the "==" form embedded in s1: "A==B" */
        {
            const char *eq = strstr(s1, "==");
            if (eq)
            {
                /* s1="str1==str2" arrived as one token (quotes and
                 * spaces stripped by the lexer) */
                char lhs[256], rhs[256];
                size_t llen = (size_t)(eq - s1);
                if (llen >= sizeof(lhs))
                    llen = sizeof(lhs) - 1;
                memcpy(lhs, s1, llen);
                lhs[llen] = '\0';
                libcmd_sprintf_s(rhs, sizeof(rhs), "%s", eq + 2);
                result = case_insensitive ? (libcmd_strcasecmp(lhs, rhs) == 0) : (strcmp(lhs, rhs) == 0);
                /* We consumed the wrong thing; back up */
                (*argi) -= 2;
                return negate ? !result : result;
            }
        }

        /* Check if op contains == (e.g. the token "==") */
        if (strcmp(op, "==") == 0)
        {
            result = case_insensitive ? (libcmd_strcasecmp(s1, s2) == 0) : (strcmp(s1, s2) == 0);
            return negate ? !result : result;
        }

        /* Extended compare operators */
        {
            char *e1 = NULL, *e2 = NULL;
            (void)strtol(s1, &e1, 10);
            (void)strtol(s2, &e2, 10);
            numeric = (e1 && *e1 == '\0' && e2 && *e2 == '\0');
        }

        if (libcmd_strcasecmp(op, "EQU") == 0)
        {
            result = numeric ? (atol(s1) == atol(s2))
                             : (case_insensitive ? libcmd_strcasecmp(s1, s2) == 0 : strcmp(s1, s2) == 0);
        }
        else if (libcmd_strcasecmp(op, "NEQ") == 0)
        {
            result = numeric ? (atol(s1) != atol(s2))
                             : (case_insensitive ? libcmd_strcasecmp(s1, s2) != 0 : strcmp(s1, s2) != 0);
        }
        else if (libcmd_strcasecmp(op, "LSS") == 0)
        {
            result = numeric ? (atol(s1) < atol(s2))
                             : (case_insensitive ? libcmd_strcasecmp(s1, s2) < 0 : strcmp(s1, s2) < 0);
        }
        else if (libcmd_strcasecmp(op, "LEQ") == 0)
        {
            result = numeric ? (atol(s1) <= atol(s2))
                             : (case_insensitive ? libcmd_strcasecmp(s1, s2) <= 0 : strcmp(s1, s2) <= 0);
        }
        else if (libcmd_strcasecmp(op, "GTR") == 0)
        {
            result = numeric ? (atol(s1) > atol(s2))
                             : (case_insensitive ? libcmd_strcasecmp(s1, s2) > 0 : strcmp(s1, s2) > 0);
        }
        else if (libcmd_strcasecmp(op, "GEQ") == 0)
        {
            result = numeric ? (atol(s1) >= atol(s2))
                             : (case_insensitive ? libcmd_strcasecmp(s1, s2) >= 0 : strcmp(s1, s2) >= 0);
        }
        else
        {
            /* Unknown operator */
            return -1;
        }

        return negate ? !result : result;
    }
}

/*
 * Reconstruct a command string from argv[argi .. argc-1]
 */
static char *rebuild_cmd(int argc, char **argv, int argi)
{
    char buf[CMD_MAX_LINE];
    int i;
    buf[0] = '\0';
    for (i = argi; i < argc; i++)
    {
        if (i > argi)
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
    }
    return libcmd_strdup(buf);
}

int builtin_if(cmd_context_t *ctx, int argc, char **argv)
{
    int argi = 1;
    int case_insensitive = 0;
    int condition;
    char *then_cmd = NULL;
    char *else_cmd = NULL;
    int i;

    if (argc < 2 || libcmd_strcasecmp(argv[1], "/?") == 0)
    {
        fputs(cmd_gettext(MSG_HELP_IF),
              stdout);
        return 0;
    }

    /* Check for /I flag */
    if (libcmd_strcasecmp(argv[argi], "/I") == 0)
    {
        case_insensitive = 1;
        argi++;
    }

    condition = eval_condition(ctx, argc, argv, &argi, case_insensitive);
    if (condition < 0)
    {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_IF_SYNTAX));
        return 1;
    }

    if (argi >= argc)
    {
        fprintf(stderr, "%s", cmd_gettext(MSG_ERR_IF_MISSING_CMD));
        return 1;
    }

    /* Find ELSE keyword if present */
    /* The THEN command runs until we hit "ELSE" at the same level */
    {
        int then_start = argi;
        int else_start = -1;

        for (i = argi; i < argc; i++)
        {
            if (libcmd_strcasecmp(argv[i], "ELSE") == 0)
            {
                else_start = i + 1;
                /* Build then_cmd from argi .. i-1 */
                {
                    char buf[CMD_MAX_LINE];
                    int j;
                    buf[0] = '\0';
                    for (j = then_start; j < i; j++)
                    {
                        if (j > then_start)
                            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
                        strncat(buf, argv[j], sizeof(buf) - strlen(buf) - 1);
                    }
                    then_cmd = libcmd_strdup(buf);
                }
                if (else_start < argc)
                    else_cmd = rebuild_cmd(argc, argv, else_start);
                break;
            }
        }

        if (then_cmd == NULL)
            then_cmd = rebuild_cmd(argc, argv, then_start);
    }

    if (condition)
    {
        int ret = 0;
        if (then_cmd && then_cmd[0])
            ret = cmd_run_line(ctx, then_cmd);
        free(then_cmd);
        free(else_cmd);
        return ret;
    }
    else
    {
        int ret = 0;
        if (else_cmd && else_cmd[0])
            ret = cmd_run_line(ctx, else_cmd);
        free(then_cmd);
        free(else_cmd);
        return ret;
    }
}
