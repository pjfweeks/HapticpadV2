#include "storage.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "log.h"

#include "ff.h"
#include "tf_card.h"

#define TAG "sd"

static FATFS s_fs;
static bool  s_mounted;
static char  s_error[64] = "not initialised";

static void set_error(const char *what, FRESULT fr) {
    snprintf(s_error, sizeof s_error, "%s: FatFs error %d", what, (int)fr);
    HP_LOGE(TAG, "%s", s_error);
}

static bool mount(void) {
    pico_fatfs_spi_config_t cfg = {
        HP_SD_SPI,
        HP_SD_BAUD_INIT,
        HP_SD_BAUD_RUN,
        HP_SD_MISO,
        HP_SD_CS,
        HP_SD_SCK,
        HP_SD_MOSI,
        true,          /* internal pull-ups on MISO/MOSI */
    };
    /* These pins are a legal SPI0 set, so the card runs on the real SPI
     * peripheral rather than a bit-banged fallback. */
    pico_fatfs_set_config(&cfg);

    FRESULT fr = f_mount(&s_fs, "", 1);
    if (fr != FR_OK) {
        set_error("mount", fr);
        s_mounted = false;
        return false;
    }
    s_mounted = true;
    s_error[0] = '\0';
    return true;
}

bool hp_storage_init(void) {
    if (!mount()) {
        HP_LOGE(TAG, "card not mounted - profiles and settings unavailable");
        return false;
    }

    uint64_t freeb = 0, totalb = 0;
    hp_storage_stat_fs(&freeb, &totalb);
    HP_LOGI(TAG, "mounted, %llu MB free of %llu MB",
            (unsigned long long)(freeb >> 20), (unsigned long long)(totalb >> 20));
    return true;
}

bool hp_storage_ready(void) { return s_mounted; }
const char *hp_storage_last_error(void) { return s_error; }

bool hp_storage_remount(void) {
    if (s_mounted) f_unmount("");
    s_mounted = false;
    return mount();
}

bool hp_storage_read(const char *path, void *buf, size_t buf_size, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!s_mounted) return false;

    FIL f;
    FRESULT fr = f_open(&f, path, FA_READ);
    if (fr != FR_OK) {
        snprintf(s_error, sizeof s_error, "open %s: %d", path, (int)fr);
        return false;
    }

    /* Compare against the real size rather than against how much we managed
     * to read: an icon is exactly 72 bytes into a 72-byte buffer, which is a
     * complete read, not a truncated one. */
    FSIZE_t size = f_size(&f);

    UINT got = 0;
    fr = f_read(&f, buf, (UINT)buf_size, &got);
    f_close(&f);

    if (fr != FR_OK) { set_error("read", fr); return false; }
    if (size > (FSIZE_t)buf_size) {
        HP_LOGW(TAG, "%s is %lu bytes but only %u fit - truncated",
                path, (unsigned long)size, (unsigned)buf_size);
    }

    s_error[0] = '\0';
    if (out_len) *out_len = got;
    return true;
}

bool hp_storage_write(const char *path, const void *data, size_t len) {
    if (!s_mounted) return false;

    /* Write beside the target, flush, then swap into place: an interrupted
     * save can only ever cost the temporary file. */
    char tmp[HP_PATH_LEN + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    FIL f;
    FRESULT fr = f_open(&f, tmp, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) { set_error("open tmp", fr); return false; }

    UINT written = 0;
    fr = f_write(&f, data, (UINT)len, &written);
    if (fr == FR_OK) fr = f_sync(&f);
    f_close(&f);

    if (fr != FR_OK || written != len) {
        set_error("write", fr);
        f_unlink(tmp);
        return false;
    }

    f_unlink(path);                     /* f_rename will not clobber */
    fr = f_rename(tmp, path);
    if (fr != FR_OK) { set_error("rename", fr); return false; }

    s_error[0] = '\0';
    HP_LOGD(TAG, "wrote %s (%u bytes)", path, (unsigned)len);
    return true;
}

bool hp_storage_append_line(const char *path, const char *line) {
    if (!s_mounted) return false;
    FIL f;
    FRESULT fr = f_open(&f, path, FA_WRITE | FA_OPEN_APPEND);
    if (fr != FR_OK) { set_error("open append", fr); return false; }
    UINT w = 0;
    f_write(&f, line, (UINT)strlen(line), &w);
    f_write(&f, "\n", 1, &w);
    fr = f_sync(&f);
    f_close(&f);
    return fr == FR_OK;
}

bool hp_storage_exists(const char *path) {
    if (!s_mounted) return false;
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) return false;
    s_error[0] = '\0';
    return true;
}

bool hp_storage_delete(const char *path) {
    if (!s_mounted) return false;
    FRESULT fr = f_unlink(path);
    if (fr != FR_OK) { set_error("delete", fr); return false; }
    return true;
}

bool hp_storage_mkdir(const char *path) {
    if (!s_mounted) return false;
    FRESULT fr = f_mkdir(path);
    return fr == FR_OK || fr == FR_EXIST;
}

bool hp_storage_list(const char *dir, hp_dir_cb cb, void *ctx) {
    if (!s_mounted) return false;

    DIR d;
    FRESULT fr = f_opendir(&d, dir);
    if (fr != FR_OK) {
        snprintf(s_error, sizeof s_error, "opendir %s: %d", dir, (int)fr);
        return false;
    }

    s_error[0] = '\0';

    hp_dirent_t entry;
    for (;;) {
        FILINFO fno;
        fr = f_readdir(&d, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0') break;

        /* Skip the debris Windows and macOS leave on removable media. */
        if (fno.fname[0] == '.' || (fno.fattrib & (AM_HID | AM_SYS))) continue;

        size_t n = strlen(fno.fname);
        if (n >= sizeof entry.name) n = sizeof entry.name - 1;
        memcpy(entry.name, fno.fname, n);
        entry.name[n] = '\0';
        entry.size   = (uint32_t)fno.fsize;
        entry.is_dir = (fno.fattrib & AM_DIR) != 0;

        if (!cb(&entry, ctx)) break;
    }

    f_closedir(&d);
    return true;
}

bool hp_storage_stat_fs(uint64_t *free_bytes, uint64_t *total_bytes) {
    if (!s_mounted) return false;
    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    if (f_getfree("", &free_clusters, &fs) != FR_OK) return false;

    uint64_t cluster_bytes = (uint64_t)fs->csize * 512u;
    if (free_bytes)  *free_bytes  = (uint64_t)free_clusters * cluster_bytes;
    if (total_bytes) *total_bytes = (uint64_t)(fs->n_fatent - 2) * cluster_bytes;
    return true;
}
