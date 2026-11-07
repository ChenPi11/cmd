/*
 * Runtime message catalog.
 *
 * The language tables live in the i18n headers and are `const char *` arrays indexed
 * by cmd_msg_id_t (see imessages.h): ien_us.h (English, the fallback
 * baseline) and izh_cn.h (Simplified Chinese).  Each language header
 * provides an init function that fills the table at startup with plain
 * assignments (no designated initializers, C89-clean); the maximum table
 * length is MSG_COUNT from imessages.h.  cmd_gettext() returns the
 * entry of the active language table, falling back to the English text
 * when a translation is not available (NULL) or the ID is out of range.
 */

#define _CRT_SECURE_NO_WARNINGS

#include "ccontext.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * msg_join - build one catalog message from several literal chunks.
 *
 * ISO C90 only obliges a compiler to accept 509-character string
 * literals; several long help pages exceed that.  The generated
 * language tables pass their chunks through this helper instead, so
 * the chunks stay separate compile-time literals and are joined here
 * once, at table-load time.  The result lives until the process ends
 * (tables are built exactly once, at startup).
 * Returns the joined string, or the first chunk when out of memory.
 */
static const char *msg_join(const char *first, ...)
{
    va_list ap;
    const char *s;
    size_t total;
    char *out;
    char *w;

    va_start(ap, first);
    s = first;
    total = 1;
    while (s != NULL) {
        total += strlen(s);
        s = va_arg(ap, const char *);
    }
    va_end(ap);

    out = (char *)malloc(total);
    if (out == NULL)
        return first;
    w = out;
    va_start(ap, first);
    s = first;
    while (s != NULL) {
        size_t n = strlen(s);

        memcpy(w, s, n);
        w += n;
        s = va_arg(ap, const char *);
    }
    va_end(ap);
    *w = '\0';
    return out;
}

#include "ilanguages.h"

static const char *table[MSG_COUNT];

static const char *getenv_any(void)
{
    const char *lang = getenv("LANG");
    if (lang == NULL || lang[0] == '\0')
        lang = getenv("LC_MESSAGES");
    if (lang == NULL || lang[0] == '\0')
        lang = getenv("LC_ALL");
    if (lang == NULL || lang[0] == '\0')
        lang = getenv("LANGUAGE");
    if (lang == NULL || lang[0] == '\0')
        lang = "en_us.utf_8";
    return lang;
}

void cmd_i18n_init(void)
{
    const char *lang_env = getenv_any();
    char lang[16];
    size_t len = strlen(lang_env);
    size_t max_len = sizeof(lang) - 1;
    size_t i;

    memset(lang, 0, sizeof(lang));

    for (i = 0; i < len && i < max_len; i++)
    {
        unsigned char c = (unsigned char)lang_env[i];
        lang[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        if (c == (unsigned char)'-')
            lang[i] = '_';
    }
    lang[i] = '\0';

    cmd_i18n_init_table(lang, table);
}

const char *cmd_gettext(cmd_msg_id_t id)
{
    if ((unsigned int)id >= (unsigned int)MSG_COUNT)
        return "";
    return table[id];
}
