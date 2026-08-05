/*
 * lenv.c - environment variable management
 *
 * Environment variable lookup is case-insensitive, matching cmd.exe
 * semantics.  If several case variants of the same name exist (e.g.
 * PATH and Path), the first match in environ order wins.  Setting a
 * variable updates the first matching existing entry (keeping its
 * original spelling) instead of creating a duplicate.
 *
 * The setters are implemented directly on the environ array so the
 * behaviour is identical on every target: old System V libc only
 * provides putenv() (no setenv/unsetenv), so relying on setenv()
 * would not be portable.
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <stdlib.h>
#include <string.h>

/* Access the process environment */
extern char **environ;

/* Find the environ entry matching name (case-insensitive); returns
 * pointer to the entry or NULL. */
static char **env_find(const char *name)
{
    size_t nlen = strlen(name);
    char **e;

    for (e = environ; *e != NULL; e++) {
        if (libcmd_strncasecmp(*e, name, nlen) == 0 &&
            (*e)[nlen] == '=')
            return e;
    }
    return NULL;
}

const char *libcmd_getenv(const char *name)
{
    char **e = env_find(name);

    if (e == NULL)
        return NULL;
    return *e + strlen(name) + 1;
}

/* Build a fresh "NAME=value" string.  name holds the name spelling to
 * use; when updating an existing variable this is the first namelen
 * bytes of the existing entry (to preserve its original case).
 * Returns a malloc'd string or NULL. */
static char *env_build_entry(const char *name, size_t namelen,
                             const char *value)
{
    size_t vlen = strlen(value);
    char *entry = (char *)malloc(namelen + vlen + 2);

    if (entry == NULL)
        return NULL;
    memcpy(entry, name, namelen);
    entry[namelen] = '=';
    memcpy(entry + namelen + 1, value, vlen + 1);
    return entry;
}

/* Append a brand-new entry to environ.  The environ array itself is
 * owned by the C runtime (not malloc'd), so we allocate a fresh array
 * and copy the existing pointers into it rather than realloc'ing it. */
static int env_append(const char *name, const char *value)
{
    int n = 0;
    int i;
    char **newenv;
    char *entry;

    if (name == NULL || value == NULL)
        return -1;

    entry = env_build_entry(name, strlen(name), value);
    if (entry == NULL)
        return -1;

    while (environ[n] != NULL)
        n++;

    newenv = (char **)malloc((size_t)(n + 2) * sizeof(char *));
    if (newenv == NULL) {
        free(entry);
        return -1;
    }
    for (i = 0; i < n; i++)
        newenv[i] = environ[i];
    newenv[n] = entry;
    newenv[n + 1] = NULL;
    environ = newenv;
    return 0;
}

int libcmd_setenv(const char *name, const char *value, int overwrite)
{
    char **e = env_find(name);
    size_t namelen;
    char *entry;

    if (name == NULL || value == NULL)
        return -1;

    if (e == NULL)
        return env_append(name, value);

    if (!overwrite)
        return 0;

    /* Update the existing entry, keeping its original spelling so no
     * duplicate case variants are created. */
    namelen = (size_t)(strchr(*e, '=') - *e);
    entry = env_build_entry(*e, namelen, value);
    if (entry == NULL)
        return -1;
    *e = entry;
    return 0;
}

int libcmd_unsetenv(const char *name)
{
    char **e = env_find(name);
    char **p;

    if (e == NULL)
        return 0;

    /* Remove the first matching entry, shifting the rest down.  The
     * entry memory is left untouched (it may belong to putenv). */
    for (p = e; *p != NULL; p++)
        *p = *(p + 1);
    return 0;
}

int libcmd_putenv(char *string)
{
    return putenv(string);
}

char **libcmd_get_environ(void)
{
    return environ;
}
