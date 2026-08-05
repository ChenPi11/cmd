/*
 * lsprintf_s.c - safe sprintf implementation
 *
 * On systems without vsnprintf (e.g. old System V libc), compile with
 * -DLIBCMD_NO_VSNPRINTF to use the built-in mini formatter below.
 *
 * License: GNU GPLv3
 */

#include "glibcmd.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Mini formatter (used only when LIBCMD_NO_VSNPRINTF is defined)
 *
 * Supports the conversions actually used by this project: %d %i %u %o
 * %x %X %c %s %% with flags (- + space 0), field width, precision, and
 * length modifiers h, l, z.  There is deliberately no %ll support (no
 * long long in the project's format strings), so the code compiles even
 * with pre-C99 compilers.  Return value follows vsnprintf semantics:
 * the number of characters that would have been written had the buffer
 * been large enough (null terminator not counted).
 * ---------------------------------------------------------------------- */

#if defined(LIBCMD_NO_VSNPRINTF)

static const char mini_digits_lo[] = "0123456789abcdef";
static const char mini_digits_hi[] = "0123456789ABCDEF";

/* Write the unsigned value v in the given base into buf; returns the
 * number of digits written (trailing spaces are left untouched). */
static size_t mini_utoa(unsigned long v, char *buf, size_t buflen,
                        int base, int upcase)
{
    const char *dig = upcase ? mini_digits_hi : mini_digits_lo;
    char tmp[64];
    size_t n = 0, i;

    if (base < 2 || base > 16 || buflen == 0)
        return 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v > 0) {
        tmp[n++] = dig[v % (unsigned long)base];
        v /= (unsigned long)base;
    }
    for (i = 0; i < n && i < buflen; i++)
        buf[i] = tmp[n - 1 - i];
    return n;
}

/* Append len bytes from s to the output, tracking the total count even
 * when the buffer runs out of space.  buf[size-1] is guaranteed to be
 * overwritten with '\0' by the caller afterwards. */
static void mini_emit(char *buf, size_t size, size_t *pos,
                      const char *s, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        if (*pos + 1 < size)
            buf[*pos] = s[i];
        (*pos)++;
    }
}

static void mini_pad(char *buf, size_t size, size_t *pos,
                     int n, char ch)
{
    size_t i;

    for (i = 0; i < (size_t)n; i++)
        mini_emit(buf, size, pos, &ch, 1);
}

static void mini_emit_number(char *buf, size_t size, size_t *pos,
                             unsigned long uv, int neg,
                             int base, int upcase,
                             int width, int prec, int left, int zero,
                             int plus, int space)
{
    char digits[64];
    size_t dlen;
    int sign, total, pad, use_zero, i;

    dlen = mini_utoa(uv, digits, sizeof(digits), base, upcase);

    /* C99: a precision of zero with a value of zero prints nothing */
    if (prec == 0 && uv == 0)
        dlen = 0;

    /* Precision: minimum number of digits (zero padding before digits) */
    if (prec > (int)dlen) {
        /* shift digits right to make room for leading zeros */
        for (i = (int)dlen - 1; i >= 0; i--)
            digits[i + (prec - (int)dlen)] = digits[i];
        for (i = 0; i < prec - (int)dlen; i++)
            digits[i] = '0';
        dlen = (size_t)prec;
    }

    sign = neg ? 1 : (plus || space ? 1 : 0);
    total = sign + (int)dlen;
    pad = width - total;
    if (pad < 0) pad = 0;
    use_zero = zero && prec < 0 && !left;

    if (!left && pad && !use_zero)
        mini_pad(buf, size, pos, pad, ' ');
    if (neg)
        mini_emit(buf, size, pos, "-", 1);
    else if (plus)
        mini_emit(buf, size, pos, "+", 1);
    else if (space)
        mini_emit(buf, size, pos, " ", 1);
    if (!left && pad && use_zero)
        mini_pad(buf, size, pos, pad, '0');
    mini_emit(buf, size, pos, digits, dlen);
    if (left && pad)
        mini_pad(buf, size, pos, pad, ' ');
}

static int libcmd_vsnprintf_mini(char *buf, size_t size,
                                 const char *fmt, va_list ap)
{
    size_t pos = 0;
    const char *p = fmt;

    if (buf == NULL || size == 0 || fmt == NULL)
        return -1;

    for (p = fmt; *p; p++) {
        if (*p != '%') {
            mini_emit(buf, size, &pos, p, 1);
            continue;
        }
        p++;
        if (*p == '\0')
            return -1;
        if (*p == '%') {
            mini_emit(buf, size, &pos, "%", 1);
            continue;
        }

        /* Flags */
        {
            int left = 0, zero = 0, plus = 0, space = 0;
            int width = 0, prec = -1, lng = 0;
            char conv;

            for (;;) {
                if (*p == '-')      { left = 1; p++; }
                else if (*p == '0') { zero = 1; p++; }
                else if (*p == '+') { plus = 1; p++; }
                else if (*p == ' ') { space = 1; p++; }
                else break;
            }
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
            if (*p == '.') {
                p++;
                prec = 0;
                while (*p >= '0' && *p <= '9') {
                    prec = prec * 10 + (*p - '0');
                    p++;
                }
            }
            if (*p == 'l') { lng = 1; p++; }
            else if (*p == 'z') { lng = 2; p++; }
            else if (*p == 'h') {
                p++;
                if (*p == 'h') { lng = 4; p++; }
                else           { lng = 3; }
            }
            conv = *p;
            if (conv == '\0')
                return -1;
            /* note: do not advance p here; the outer for loop's p++
             * consumes the conversion character */

            switch (conv) {
            case 'd':
            case 'i': {
                long v;
                if (lng == 1)      v = va_arg(ap, long);
                else if (lng == 2) v = (long)va_arg(ap, size_t);
                else if (lng == 3) v = (long)(short)va_arg(ap, int);
                else if (lng == 4) v = (long)(signed char)va_arg(ap, int);
                else               v = va_arg(ap, int);
                mini_emit_number(buf, size, &pos,
                                 (unsigned long)(v < 0 ? -v : v),
                                 v < 0, 10, 0, width, prec, left, zero,
                                 plus, space);
                break;
            }
            case 'u':
            case 'o':
            case 'x':
            case 'X': {
                unsigned long uv;
                if (lng == 1)      uv = va_arg(ap, unsigned long);
                else if (lng == 2) uv = (unsigned long)va_arg(ap, size_t);
                else if (lng == 3) uv = (unsigned short)va_arg(ap, int);
                else if (lng == 4) uv = (unsigned char)va_arg(ap, int);
                else               uv = va_arg(ap, unsigned int);
                mini_emit_number(buf, size, &pos, uv,
                                 0, conv == 'o' ? 8 :
                                    (conv == 'u' ? 10 : 16),
                                 conv == 'X', width, prec, left, zero,
                                 plus, space);
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                int pad = width - 1;
                if (pad < 0) pad = 0;
                if (!left) mini_pad(buf, size, &pos, pad, ' ');
                mini_emit(buf, size, &pos, &c, 1);
                if (left) mini_pad(buf, size, &pos, pad, ' ');
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                size_t len;
                int pad;
                if (s == NULL)
                    s = "(null)";
                len = strlen(s);
                if (prec >= 0 && len > (size_t)prec)
                    len = (size_t)prec;
                pad = width - (int)len;
                if (pad < 0) pad = 0;
                if (!left) mini_pad(buf, size, &pos, pad, ' ');
                mini_emit(buf, size, &pos, s, len);
                if (left) mini_pad(buf, size, &pos, pad, ' ');
                break;
            }
            default:
                return -1;
            }
        }
    }

    return (int)pos;
}

#endif

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int libcmd_vsprintf_s(char *buf, size_t size, const char *fmt, va_list ap)
{
    int n;

    if (buf == NULL || size == 0 || fmt == NULL)
        return -1;

#if defined(LIBCMD_NO_VSNPRINTF)
    n = libcmd_vsnprintf_mini(buf, size, fmt, ap);
#else
    n = vsnprintf(buf, size, fmt, ap);
#endif

    /* Terminate the string at the right place: the standard says the
     * null terminator goes at the end of the output when it fits, and
     * at size-1 when the output was truncated. */
    if (n < 0)
        return -1;
    if ((size_t)n < size - 1)
        buf[n] = '\0';
    else
        buf[size - 1] = '\0';

    /* Return how many characters were actually written (clamped to size-1) */
    return (n < (int)(size - 1)) ? n : (int)(size - 1);
}

int libcmd_sprintf_s(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = libcmd_vsprintf_s(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
