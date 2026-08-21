#include "ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage.h"

#define LINE_MAX 256

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) *end-- = '\0';
    return s;
}

/* Handles one already-isolated line.  Returns the callback's verdict. */
static bool handle_line(char *line, hp_ini_cb cb, void *ctx) {
    char *s = trim(line);
    if (!*s || *s == '#' || *s == ';') return true;

    if (*s == '[') {
        char *close = strchr(s, ']');
        if (close) {
            *close = '\0';
            return cb(trim(s + 1), NULL, ctx);
        }
        return true;
    }

    char *eq = strchr(s, '=');
    if (!eq) return true;               /* not a setting - ignore quietly */
    *eq = '\0';

    char *key = trim(s);
    char *val = trim(eq + 1);
    if (!*key) return true;

    /* Keys are matched case-insensitively; normalise once here so every
     * consumer can use plain strcmp. */
    for (char *k = key; *k; k++) *k = (char)tolower((unsigned char)*k);

    return cb(key, val, ctx);
}

bool hp_ini_parse_buffer(const char *buf, size_t len, hp_ini_cb cb, void *ctx) {
    char line[LINE_MAX];
    size_t n = 0;

    for (size_t i = 0; i <= len; i++) {
        char c = (i < len) ? buf[i] : '\n';
        if (c == '\n' || c == '\r') {
            if (n) {
                line[n] = '\0';
                if (!handle_line(line, cb, ctx)) return true;
                n = 0;
            }
        } else if (n < LINE_MAX - 1) {
            line[n++] = c;
        }
        /* Lines longer than LINE_MAX are truncated rather than split, so a
         * runaway file cannot inject a bogus half-key. */
    }
    return true;
}

bool hp_ini_parse_file(const char *path, hp_ini_cb cb, void *ctx) {
    static char buf[HP_FILE_BUF_BYTES];
    size_t len = 0;
    if (!hp_storage_read(path, buf, sizeof buf, &len)) return false;
    return hp_ini_parse_buffer(buf, len, cb, ctx);
}

/* --- conversions -------------------------------------------------------- */

bool hp_ini_bool(const char *value, bool fallback) {
    if (!value || !*value) return fallback;
    if (!strcasecmp(value, "1") || !strcasecmp(value, "true") ||
        !strcasecmp(value, "on") || !strcasecmp(value, "yes")) return true;
    if (!strcasecmp(value, "0") || !strcasecmp(value, "false") ||
        !strcasecmp(value, "off") || !strcasecmp(value, "no")) return false;
    return fallback;
}

int32_t hp_ini_int(const char *value, int32_t fallback) {
    if (!value || !*value) return fallback;
    char *end;
    long v = strtol(value, &end, 0);
    return end == value ? fallback : (int32_t)v;
}

float hp_ini_float(const char *value, float fallback) {
    if (!value || !*value) return fallback;
    char *end;
    float v = strtof(value, &end);
    return end == value ? fallback : v;
}

uint32_t hp_ini_color(const char *value, uint32_t fallback) {
    if (!value || !*value) return fallback;

    /* "r,g,b" - the form the original XML config used. */
    if (strchr(value, ',')) {
        int r = 0, g = 0, b = 0;
        if (sscanf(value, "%d , %d , %d", &r, &g, &b) == 3) {
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        }
        return fallback;
    }

    const char *p = value;
    if (*p == '#') p++;
    else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;

    char *end;
    unsigned long v = strtoul(p, &end, 16);
    return end == p ? fallback : (uint32_t)(v & 0xFFFFFFu);
}

void hp_ini_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst_size) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool hp_ini_indexed(const char *key, const char *prefix, const char *suffix, int *index) {
    size_t plen = strlen(prefix);
    if (strncmp(key, prefix, plen) != 0) return false;

    const char *p = key + plen;
    if (!isdigit((unsigned char)*p)) return false;

    int n = 0;
    while (isdigit((unsigned char)*p)) n = n * 10 + (*p++ - '0');
    if (strcmp(p, suffix) != 0) return false;

    *index = n;
    return true;
}
