/* storage.h - microSD access.
 *
 * Every write goes through a temp-file-then-rename dance so that yanking USB
 * power mid-save leaves the previous version intact rather than a truncated
 * config.  Reads are bounded and never allocate.
 */
#ifndef HP_STORAGE_H
#define HP_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Largest config or profile file we will read in one go.  Profiles are a few
 * hundred bytes; this leaves generous headroom without eating RAM we need for
 * the framebuffer and icon cache. */
#define HP_FILE_BUF_BYTES 4096

typedef struct {
    char     name[64];
    uint32_t size;
    bool     is_dir;
} hp_dirent_t;

/* Return false to stop the walk. */
typedef bool (*hp_dir_cb)(const hp_dirent_t *entry, void *ctx);

bool hp_storage_init(void);
bool hp_storage_ready(void);
/* Re-runs card detection and mounting; used by the configurator's RESCAN. */
bool hp_storage_remount(void);
const char *hp_storage_last_error(void);

bool hp_storage_read(const char *path, void *buf, size_t buf_size, size_t *out_len);
bool hp_storage_write(const char *path, const void *data, size_t len);
bool hp_storage_append_line(const char *path, const char *line);
bool hp_storage_exists(const char *path);
bool hp_storage_delete(const char *path);
bool hp_storage_mkdir(const char *path);
bool hp_storage_list(const char *dir, hp_dir_cb cb, void *ctx);

/* Free/total bytes, for the configurator's status panel. */
bool hp_storage_stat_fs(uint64_t *free_bytes, uint64_t *total_bytes);

#ifdef __cplusplus
}
#endif

#endif /* HP_STORAGE_H */
