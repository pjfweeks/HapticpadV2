#include "log.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/sync.h"

#define LOG_RING_BYTES 4096u        /* must be a power of two */
#define LOG_LINE_MAX    192

static uint8_t  s_ring[LOG_RING_BYTES];
static volatile uint32_t s_head;    /* next write position  */
static volatile uint32_t s_tail;    /* next read position   */
static volatile uint32_t s_dropped;
static hp_log_level_t s_level = HP_LOG_INFO;

/* One hardware spinlock serialises the two cores.  Writes are short (a
 * memcpy of at most LOG_LINE_MAX bytes) so the FOC loop is never held up in
 * any measurable way. */
static spin_lock_t *s_lock;

static const char *level_tag(hp_log_level_t level) {
    switch (level) {
        case HP_LOG_ERROR: return "E";
        case HP_LOG_WARN:  return "W";
        case HP_LOG_INFO:  return "I";
        default:           return "D";
    }
}

void hp_log_init(void) {
    s_head = s_tail = s_dropped = 0;
    s_lock = spin_lock_instance(spin_lock_claim_unused(true));
}

void hp_log_set_level(hp_log_level_t level) { s_level = level; }
hp_log_level_t hp_log_get_level(void)       { return s_level; }
uint32_t hp_log_dropped(void)               { return s_dropped; }

void hp_log_write(hp_log_level_t level, const char *tag, const char *fmt, ...) {
    if (level > s_level) return;

    char line[LOG_LINE_MAX];
    uint32_t ms = to_ms_since_boot(get_absolute_time());

    int n = snprintf(line, sizeof line, "[%7lu.%03lu %s/%s] ",
                     (unsigned long)(ms / 1000), (unsigned long)(ms % 1000),
                     level_tag(level), tag);
    if (n < 0) return;
    if (n > (int)sizeof line) n = sizeof line;

    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    if (m < 0) return;

    size_t len = (size_t)n + (size_t)m;
    if (len > sizeof line - 2) len = sizeof line - 2;
    line[len++] = '\r';
    line[len++] = '\n';

    uint32_t save = spin_lock_blocking(s_lock);
    uint32_t used = s_head - s_tail;
    if (used + len > LOG_RING_BYTES) {
        /* Drop the whole line rather than emitting a truncated one: a
         * half-written log entry is worse than a visible gap. */
        s_dropped += (uint32_t)len;
    } else {
        for (size_t i = 0; i < len; i++) {
            s_ring[(s_head + i) & (LOG_RING_BYTES - 1)] = (uint8_t)line[i];
        }
        s_head += (uint32_t)len;
    }
    spin_unlock(s_lock, save);
}

size_t hp_log_drain(uint8_t *dst, size_t max) {
    uint32_t save = spin_lock_blocking(s_lock);
    uint32_t used = s_head - s_tail;
    size_t n = used < max ? used : max;
    for (size_t i = 0; i < n; i++) {
        dst[i] = s_ring[(s_tail + i) & (LOG_RING_BYTES - 1)];
    }
    s_tail += (uint32_t)n;
    spin_unlock(s_lock, save);
    return n;
}

bool hp_log_pending(void) { return s_head != s_tail; }

/* ---------------------------------------------------------------------- */
/* Bridge for SimpleFOC's own debug output (see compat/Arduino.h).         */
/* Text arrives a fragment at a time, so we buffer a line and emit it as a */
/* single log entry once the library finishes it.                          */
/* ---------------------------------------------------------------------- */
static char   s_sfoc_line[128];
static size_t s_sfoc_len;

void hp_sfoc_debug_putc(char c) {
    if (s_sfoc_len < sizeof s_sfoc_line - 1) s_sfoc_line[s_sfoc_len++] = c;
}

void hp_sfoc_debug_puts(const char *s) {
    while (*s) hp_sfoc_debug_putc(*s++);
}

void hp_sfoc_debug_flush(void) {
    s_sfoc_line[s_sfoc_len] = '\0';
    if (s_sfoc_len) hp_log_write(HP_LOG_DEBUG, "foc", "%s", s_sfoc_line);
    s_sfoc_len = 0;
}
