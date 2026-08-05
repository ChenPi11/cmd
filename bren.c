/*
 * bren.c - REN / RENAME builtin
 *
 * REN    [drive:][path]filename1 filename2
 * RENAME [drive:][path]filename1 filename2
 *
 * Wildcard renaming: REN *.txt *.bak renames each matched file, with '*'
 * in the target expanding to the text matched by the corresponding '*'
 * in the source pattern and '?' taking the character at the matching
 * position (cmd.exe semantics).
 *
 * License: GNU GPLv3
 */

#include "ccontext.h"
#include "glibcmd.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CAPS 32

typedef struct {
    const char *text;  /* start of the text matched by a '*' */
    const char *end;   /* one past the matched text           */
} cap_t;

/* Wildcard match with capture of each '*' match.  Returns 1 on match.
 * Caps are filled rightmost-first; caller must reverse them. */
static int pm_match(const char *s, const char *p, cap_t *caps,
                    int maxc, int *ncap)
{
    while (*p) {
        if (*p == '*') {
            if (*ncap >= maxc)
                return 0;
            if (p[1] == '\0') {
                caps[*ncap].text = s;
                caps[*ncap].end = s + strlen(s);
                (*ncap)++;
                return 1;
            }
            {
                const char *t = s;
                for (;;) {
                    if (pm_match(t, p + 1, caps, maxc, ncap)) {
                        caps[*ncap].text = s;
                        caps[*ncap].end = t;
                        (*ncap)++;
                        return 1;
                    }
                    if (*t == '\0')
                        break;
                    t++;
                }
            }
            return 0;
        } else if (*p == '?') {
            if (*s == '\0')
                return 0;
            s++;
            p++;
        } else {
            if (toupper((unsigned char)*s) != toupper((unsigned char)*p))
                return 0;
            s++;
            p++;
        }
    }
    return *s == '\0';
}

/* Build the destination name from the source name and patterns.
 * When src has no wildcard, '*' in the target expands to the source
 * name's stem (name without the last extension). */
static int gen_dst(const char *src_name, int src_wild,
                   const char *dst_pat, const cap_t *caps, int ncap,
                   char *out, size_t outsz)
{
    const char *p;
    size_t o = 0, k = 0;
    size_t src_off = 0;
    size_t slen = strlen(src_name);

    for (p = dst_pat; *p && o + 1 < outsz; p++) {
        if (*p == '*') {
            const char *t;
            size_t len;
            if (src_wild) {
                t = (k < (size_t)ncap) ? caps[k].text : "";
                len = (k < (size_t)ncap) ? (size_t)(caps[k].end - caps[k].text) : 0;
                k++;
            } else {
                /* stem: src name without the last extension */
                const char *dot = strrchr(src_name, '.');
                static char stem[CMD_MAX_PATH];
                len = dot ? (size_t)(dot - src_name) : slen;
                if (len >= sizeof(stem))
                    len = sizeof(stem) - 1;
                memcpy(stem, src_name, len);
                stem[len] = '\0';
                t = stem;
            }
            src_off += len;
            while (len-- > 0 && o + 1 < outsz)
                out[o++] = *t++;
        } else if (*p == '?') {
            if (src_off < slen)
                out[o++] = src_name[src_off++];
        } else {
            /* plain character: copy, and align src_off past one char */
            if (src_off < slen)
                src_off++;
            out[o++] = *p;
        }
    }
    out[o] = '\0';
    return o > 0;
}

static const char *base_name(const char *path)
{
    const char *b = strrchr(path, '/');
    return b ? b + 1 : path;
}

int builtin_ren(cmd_context_t *ctx, int argc, char **argv)
{
    const char *src, *newname;
    char dir_part[CMD_MAX_PATH];
    int src_wild;
    int ret = 0;

    (void)ctx;

    if (argc < 3 || (argc == 2 && libcmd_strcasecmp(argv[1], "/?") == 0)) {
        fputs(cmd_gettext(MSG_HELP_REN), stdout);
        return argc < 3 ? 1 : 0;
    }

    src     = argv[1];
    newname = argv[2];
    src_wild = strchr(src, '*') != NULL || strchr(src, '?') != NULL;

    if (!src_wild) {
        /* Plain source name: simple rename (target may still contain
         * wildcards, which expand against the source name) */
        char dst[CMD_MAX_PATH];

        if (libcmd_path_dirname(src, dir_part, sizeof(dir_part)) == 0 &&
            strcmp(dir_part, ".") != 0) {
            char gen[CMD_MAX_PATH];
            if (gen_dst(base_name(src), 0, newname,
                        NULL, 0, gen, sizeof(gen))) {
                if (strchr(newname, '*') != NULL || strchr(newname, '?') != NULL)
                    libcmd_sprintf_s(dst, sizeof(dst), "%s/%s", dir_part, gen);
                else
                    libcmd_sprintf_s(dst, sizeof(dst), "%s/%s", dir_part, newname);
            } else {
                libcmd_sprintf_s(dst, sizeof(dst), "%s/%s", dir_part, newname);
            }
        } else {
            if (!gen_dst(base_name(src), 0, newname,
                         NULL, 0, dst, sizeof(dst))) {
                libcmd_sprintf_s(dst, sizeof(dst), "%s", newname);
            }
        }

        if (libcmd_rename(src, dst) < 0) {
            fprintf(stderr, cmd_gettext(MSG_ERR_RENAME),
                    src, dst, libcmd_strerror());
            return 1;
        }
        return 0;
    }

    /* Wildcard source: expand and rename each match */
    {
        libcmd_glob_result_t gr;
        size_t j;
        const char *src_pat = base_name(src);

        if (libcmd_glob(src, &gr) != 0) {
            fprintf(stderr, cmd_gettext(MSG_ERR_COULD_NOT_FIND), src);
            return 1;
        }

        for (j = 0; j < gr.count; j++) {
            cap_t caps[MAX_CAPS];
            int ncap = 0;
            char dst_name[CMD_MAX_PATH];
            char dst[CMD_MAX_PATH];
            const char *m = gr.paths[j];
            const char *mbase = base_name(m);
            int i;

            if (!pm_match(mbase, src_pat, caps, MAX_CAPS, &ncap))
                continue;

            /* Caps are rightmost-first; reverse them */
            for (i = 0; i < ncap / 2; i++) {
                cap_t t = caps[i];
                caps[i] = caps[ncap - 1 - i];
                caps[ncap - 1 - i] = t;
            }

            if (!gen_dst(mbase, 1, newname,
                         caps, ncap, dst_name, sizeof(dst_name)))
                continue;

            if (libcmd_strcasecmp(dst_name, mbase) == 0) {
                fprintf(stderr, "%s",
                        cmd_gettext(MSG_ERR_DUPLICATE_NAME));
                ret = 1;
                continue;
            }

            if (mbase != m) {
                size_t dlen = (size_t)(mbase - m);
                char dir[CMD_MAX_PATH];
                if (dlen >= sizeof(dir))
                    dlen = sizeof(dir) - 1;
                memcpy(dir, m, dlen);
                dir[dlen] = '\0';
                if (dlen == 0)
                    libcmd_sprintf_s(dst, sizeof(dst), "%s", dst_name);
                else
                    libcmd_sprintf_s(dst, sizeof(dst), "%s/%s", dir, dst_name);
            } else {
                libcmd_sprintf_s(dst, sizeof(dst), "%s", dst_name);
            }

            if (libcmd_rename(m, dst) < 0) {
                fprintf(stderr, cmd_gettext(MSG_ERR_RENAME),
                        m, dst, libcmd_strerror());
                ret = 1;
            }
        }
        libcmd_glob_free(&gr);
    }

    return ret;
}
