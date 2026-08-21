/* log.h - lock-free diagnostic logging.
 *
 * Both cores log into the same ring buffer; core 0 drains it to the USB CDC
 * debug channel in its main loop.  Logging never blocks and never allocates,
 * so it is safe to call from the FOC loop on core 1 - if the buffer is full
 * the oldest bytes are dropped rather than stalling the writer.
 */
#ifndef HP_LOG_H
#define HP_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HP_LOG_ERROR = 0,
    HP_LOG_WARN  = 1,
    HP_LOG_INFO  = 2,
    HP_LOG_DEBUG = 3,
} hp_log_level_t;

void hp_log_init(void);

/* Messages above the current level are discarded at the call site. */
void hp_log_set_level(hp_log_level_t level);
hp_log_level_t hp_log_get_level(void);

void hp_log_write(hp_log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Copy up to `max` pending bytes out of the ring.  Returns the byte count. */
size_t hp_log_drain(uint8_t *dst, size_t max);
bool   hp_log_pending(void);

/* How many bytes were dropped because the ring was full - a non-zero value
 * means the drain side is not keeping up and is worth surfacing. */
uint32_t hp_log_dropped(void);

#define HP_LOGE(tag, ...) hp_log_write(HP_LOG_ERROR, tag, __VA_ARGS__)
#define HP_LOGW(tag, ...) hp_log_write(HP_LOG_WARN,  tag, __VA_ARGS__)
#define HP_LOGI(tag, ...) hp_log_write(HP_LOG_INFO,  tag, __VA_ARGS__)
#define HP_LOGD(tag, ...) hp_log_write(HP_LOG_DEBUG, tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* HP_LOG_H */
