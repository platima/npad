/*
 * npad - Short time and date formats
 * See time_format.h.
 *
 * Author: Platima
 * https://npad.dev
 */

#include "time_format.h"

#include <string.h>

// An output buffer that remembers whether anything failed to fit
typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} Out;

static void out_bytes(Out *o, const char *bytes, size_t n) {
    if (o->overflow || o->len + n >= o->cap) {
        o->overflow = true;
        return;
    }
    memcpy(o->buf + o->len, bytes, n);
    o->len += n;
    o->buf[o->len] = '\0';
}

static void out_str(Out *o, const char *s) {
    out_bytes(o, s, strlen(s));
}

// The length of the conversion starting at fmt[0] == '%': flags, a width, an
// E or O modifier, then the conversion character (which *conv receives; '\0'
// for a lone '%' at the end)
static size_t conversion_length(const char *fmt, char *conv) {
    size_t i = 1;
    while (fmt[i] && strchr("_-0^#", fmt[i]))
        i++;
    while (fmt[i] >= '0' && fmt[i] <= '9')
        i++;
    if (fmt[i] == 'E' || fmt[i] == 'O')
        i++;
    *conv = fmt[i];
    return fmt[i] ? i + 1 : i;
}

// Finish: the fallback when nothing usable is left; false only on overflow
static bool finish(Out *o, const char *fallback) {
    while (o->len > 0 && o->buf[o->len - 1] == ' ')
        o->buf[--o->len] = '\0';
    if (!o->overflow && o->len == 0)
        out_str(o, fallback);
    if (o->overflow) {
        if (o->cap > 0)
            o->buf[0] = '\0';
        return false;
    }
    return true;
}

static bool begin(Out *o, char *out, size_t cap) {
    o->buf = out;
    o->cap = cap;
    o->len = 0;
    o->overflow = (out == NULL || cap == 0);
    if (!o->overflow)
        out[0] = '\0';
    return !o->overflow;
}

bool time_format_short_time(const char *fmt, char *out, size_t cap) {
    Out o;
    if (!begin(&o, out, cap))
        return false;
    for (size_t i = 0; fmt && fmt[i];) {
        if (fmt[i] != '%') {
            out_bytes(&o, fmt + i, 1);
            i++;
            continue;
        }
        char conv = '\0';
        size_t n = conversion_length(fmt + i, &conv);
        if (conv == 'S') {
            // The seconds go, with the separator that leads into them
            // ("%M:%S") or, when there is none, the unit that follows them
            // (ja_JP and ko_KR write a "seconds" character after %S)
            if (o.len > 0 && strchr(":.,", o.buf[o.len - 1])) {
                o.buf[--o.len] = '\0';
                i += n;
            } else {
                i += n;
                while (fmt[i] && fmt[i] != '%' && fmt[i] != ' ')
                    i++;
            }
            continue;
        }
        if (conv == 'T')
            out_str(&o, "%H:%M");
        else if (conv == 'r')
            out_str(&o, "%I:%M %p");
        else
            out_bytes(&o, fmt + i, n);
        i += n;
    }
    return finish(&o, "%H:%M");
}

bool time_format_short_date(const char *fmt, char *out, size_t cap) {
    Out o;
    if (!begin(&o, out, cap))
        return false;
    for (size_t i = 0; fmt && fmt[i];) {
        if (fmt[i] != '%') {
            out_bytes(&o, fmt + i, 1);
            i++;
            continue;
        }
        char conv = '\0';
        size_t n = conversion_length(fmt + i, &conv);
        bool modified = n >= 3 && (fmt[i + n - 2] == 'E' || fmt[i + n - 2] == 'O');
        if (conv == 'y' && !modified) {
            out_bytes(&o, fmt + i, n - 1); // Keep any flags: "%-y" -> "%-Y"
            out_str(&o, "Y");
        } else if (conv == 'D') {
            out_str(&o, "%m/%d/%Y");
        } else {
            out_bytes(&o, fmt + i, n);
        }
        i += n;
    }
    return finish(&o, "%m/%d/%Y");
}
