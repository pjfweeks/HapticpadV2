/* ini.h - streaming key=value parser.
 *
 * Deliberately allocation-free and line-at-a-time: the old firmware built an
 * Arduino String for every line and every field of every profile, on a shared
 * heap that both cores were touching.  Here the caller supplies the storage
 * and the parser only ever hands out pointers into a single reusable line
 * buffer.
 *
 * Accepted syntax:
 *   # comment              ; comment
 *   [section]              (reported with a NULL value)
 *   key = value            (whitespace around either side is trimmed)
 */
#ifndef HP_INI_H
#define HP_INI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return false to stop parsing early. */
typedef bool (*hp_ini_cb)(const char *key, const char *value, void *ctx);

bool hp_ini_parse_buffer(const char *buf, size_t len, hp_ini_cb cb, void *ctx);
bool hp_ini_parse_file(const char *path, hp_ini_cb cb, void *ctx);

/* Value conversion helpers, each with an explicit fallback so a malformed
 * config degrades to defaults instead of undefined behaviour. */
bool     hp_ini_bool(const char *value, bool fallback);
int32_t  hp_ini_int(const char *value, int32_t fallback);
float    hp_ini_float(const char *value, float fallback);
/* Accepts 0xRRGGBB, #RRGGBB, RRGGBB or "r,g,b". */
uint32_t hp_ini_color(const char *value, uint32_t fallback);

/* Bounded copy that always null-terminates. */
void hp_ini_copy(char *dst, size_t dst_size, const char *src);

/* True when `key` matches `prefix` followed by a digit and `suffix`, e.g.
 * key_matches_indexed("btn3_label", "btn", "_label", &n) -> n = 3. */
bool hp_ini_indexed(const char *key, const char *prefix, const char *suffix, int *index);

#ifdef __cplusplus
}
#endif

#endif /* HP_INI_H */
