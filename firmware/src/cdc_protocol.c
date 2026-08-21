#include "cdc_protocol.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "gfx.h"
#include "hid.h"
#include "ini.h"
#include "ipc.h"
#include "keycodes.h"
#include "led.h"
#include "log.h"
#include "macro.h"
#include "profile.h"
#include "settings.h"
#include "storage.h"
#include "system.h"
#include "ui.h"
#include "wheel.h"

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"

#define TAG "cdc"

#define LINE_MAX 512

static char   s_line[LINE_MAX];
static size_t s_line_len;

/* State for a WRITE in progress. */
static bool   s_writing;
static char   s_write_path[HP_PATH_LEN];
static uint8_t s_write_buf[HP_FILE_BUF_BYTES];
static size_t s_write_len;

static bool s_log_stream = true;
static bool s_log_at_line_start = true;

/* --- output ------------------------------------------------------------- */

/* Replies are buffered here and pushed to the CDC endpoint from the main
 * loop.  A file listing or a base64 READ is several kilobytes - far more than
 * the 1 KB endpoint FIFO - and blocking until the host drains it would stall
 * button scanning and the HID queue.  Buffering keeps every command handler
 * free to write as much as it likes and return immediately. */
#define TX_RING 8192u                  /* power of two */

static uint8_t  s_tx[TX_RING];
static uint32_t s_tx_head, s_tx_tail;
static uint32_t s_tx_dropped;

static uint32_t tx_used(void)  { return s_tx_head - s_tx_tail; }
static uint32_t tx_space(void) { return TX_RING - tx_used(); }

static void emit(const char *s) {
    size_t len = strlen(s);
    if (len > tx_space()) {
        /* The host stopped reading.  Dropping is the only alternative to
         * hanging; the count surfaces in STATUS so it is not silent. */
        s_tx_dropped += (uint32_t)len;
        return;
    }
    for (size_t i = 0; i < len; i++) {
        s_tx[(s_tx_head + i) & (TX_RING - 1)] = (uint8_t)s[i];
    }
    s_tx_head += (uint32_t)len;
}

/* Moves as much of the ring as the endpoint will take, without waiting. */
static void tx_pump(void) {
    while (tx_used()) {
        uint32_t avail = tud_cdc_write_available();
        if (!avail) break;

        uint32_t chunk = tx_used();
        if (chunk > avail) chunk = avail;

        /* Stop at the wrap point so one write is always contiguous. */
        uint32_t offset = s_tx_tail & (TX_RING - 1);
        if (offset + chunk > TX_RING) chunk = TX_RING - offset;

        uint32_t written = tud_cdc_write(&s_tx[offset], chunk);
        if (!written) break;
        s_tx_tail += written;
    }
    tud_cdc_write_flush();
}

static void emitf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void emitf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    emit(buf);
}

static void reply_ok_plain(void) { emit("OK\r\n"); }

static void reply_ok(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void reply_ok(const char *fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    emitf("OK%s%s\r\n", buf[0] ? " " : "", buf);
}

static void reply_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void reply_err(const char *fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    emitf("ERR %s\r\n", buf);
}

/* --- base64 -------------------------------------------------------------- */

static const char k_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64_emit(const uint8_t *data, size_t len) {
    char line[81];
    size_t col = 0;

    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) v |= data[i + 2];

        line[col++] = k_b64[(v >> 18) & 0x3F];
        line[col++] = k_b64[(v >> 12) & 0x3F];
        line[col++] = (i + 1 < len) ? k_b64[(v >> 6) & 0x3F] : '=';
        line[col++] = (i + 2 < len) ? k_b64[v & 0x3F] : '=';

        if (col >= 76) {
            line[col] = '\0';
            emitf("+%s\r\n", line);
            col = 0;
        }
    }
    if (col) {
        line[col] = '\0';
        emitf("+%s\r\n", line);
    }
}

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Appends one base64 line's worth of bytes to the write buffer. */
static bool b64_append(const char *text) {
    uint32_t acc = 0;
    int bits = 0;

    for (const char *p = text; *p; p++) {
        if (*p == '=' || *p == '\r' || *p == '\n' || *p == ' ') continue;
        int v = b64_value(*p);
        if (v < 0) return false;

        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (s_write_len >= sizeof s_write_buf) return false;
            s_write_buf[s_write_len++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return true;
}

/* --- command helpers ---------------------------------------------------- */

/* Splits "SET led_mode rainbow" into verb / first argument / remainder.
 * The remainder keeps its spaces, which matters for TEXT: payloads. */
static void split(char *line, char **verb, char **arg1, char **rest) {
    *verb = line;
    *arg1 = NULL;
    *rest = NULL;

    char *sp = strchr(line, ' ');
    if (!sp) return;
    *sp++ = '\0';
    while (*sp == ' ') sp++;
    *arg1 = sp;

    char *sp2 = strchr(sp, ' ');
    if (!sp2) return;
    *sp2++ = '\0';
    while (*sp2 == ' ') sp2++;
    *rest = sp2;
}

static bool list_cb(const hp_dirent_t *entry, void *ctx) {
    (void)ctx;
    emitf("+%s\t%lu\t%s\r\n", entry->name, (unsigned long)entry->size,
          entry->is_dir ? "dir" : "file");
    return true;
}

static void cmd_info(void) {
    char uid[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(uid, sizeof uid);

    uint64_t freeb = 0, totalb = 0;
    hp_storage_stat_fs(&freeb, &totalb);

    emitf("+name\t%s\r\n", HP_FW_NAME);
    emitf("+version\t%s\r\n", HP_FW_VERSION);
    emitf("+protocol\t%d\r\n", HP_PROTO_VERSION);
    emitf("+uid\t%s\r\n", uid);
    emitf("+sd\t%s\r\n", hp_storage_ready() ? "ok" : "missing");
    if (hp_storage_last_error()[0]) {
        emitf("+sd_error\t%s\r\n", hp_storage_last_error());
    }
    emitf("+sd_free\t%llu\r\n", (unsigned long long)freeb);
    emitf("+sd_total\t%llu\r\n", (unsigned long long)totalb);
    emitf("+profiles\t%d\r\n", hp_profile_count());
    emitf("+active\t%s\r\n", hp_profile_current()->file);
    emitf("+buttons\t%d\r\n", HP_MACRO_COUNT);
    emitf("+max_steps\t%d\r\n", HP_MAX_STEPS);
    emitf("+icon\t%dx%d\r\n", HP_ICON_W, HP_ICON_H);
    reply_ok_plain();
}

static void cmd_status(void) {
    hp_haptic_state_t st;
    hp_ipc_state_get(&st);

    emitf("+angle\t%.4f\r\n", (double)st.angle);
    emitf("+velocity\t%.4f\r\n", (double)st.velocity);
    emitf("+voltage_q\t%.4f\r\n", (double)st.voltage_q);
    emitf("+foc_hz\t%lu\r\n", (unsigned long)st.loop_hz);
    emitf("+foc_ready\t%d\r\n", (int)st.ready);
    emitf("+foc_fault\t%u\r\n", st.fault);
    emitf("+ticks_cw\t%lu\r\n", (unsigned long)hp_wheel_ticks_cw());
    emitf("+ticks_ccw\t%lu\r\n", (unsigned long)hp_wheel_ticks_ccw());
    emitf("+ticks_lost\t%lu\r\n", (unsigned long)hp_ipc_tick_lost());
    emitf("+hid_queued\t%u\r\n", hp_hid_queued());
    emitf("+hid_dropped\t%lu\r\n", (unsigned long)hp_hid_dropped());
    emitf("+hid_ready\t%d\r\n", (int)hp_hid_ready());
    emitf("+log_dropped\t%lu\r\n", (unsigned long)hp_log_dropped());
    emitf("+cdc_dropped\t%lu\r\n", (unsigned long)s_tx_dropped);
    emitf("+screen\t%d\r\n", (int)hp_ui_screen());
    emitf("+uptime_ms\t%lu\r\n", (unsigned long)to_ms_since_boot(get_absolute_time()));
    reply_ok_plain();
}

/* Reports the active profile as the pad understood it, not as the file reads.
 * That distinction is the point: it shows exactly what got parsed. */
static void cmd_profile_dump(void) {
    const hp_profile_t *p = hp_profile_current();
    char action[96];

    emitf("+file\t%s\r\n", p->file);
    emitf("+name\t%s\r\n", p->name);
    /* Report both what the profile states and what it will actually do, so the
     * configurator can show inherited values without guessing at the merge. */
    hp_settings_t *cfg = hp_settings();
    if (p->led.has_mode)      emitf("+led_mode\t%s\r\n", hp_led_mode_name(p->led.mode));
    if (p->led.has_primary)   emitf("+led_primary\t#%06lX\r\n", (unsigned long)p->led.primary);
    if (p->led.has_secondary) emitf("+led_secondary\t#%06lX\r\n", (unsigned long)p->led.secondary);
    if (p->led.has_brightness) emitf("+led_brightness\t%u\r\n", p->led.brightness);

    emitf("+led_mode_effective\t%s\r\n",
          hp_led_mode_name(p->led.has_mode ? p->led.mode : cfg->led_mode));
    emitf("+led_primary_effective\t#%06lX\r\n",
          (unsigned long)(p->led.has_primary ? p->led.primary : cfg->led_primary));
    emitf("+led_secondary_effective\t#%06lX\r\n",
          (unsigned long)(p->led.has_secondary ? p->led.secondary : cfg->led_secondary));
    emitf("+led_brightness_effective\t%u\r\n",
          p->led.has_brightness ? p->led.brightness : cfg->led_brightness);
    emitf("+wheel\t%s\r\n", hp_haptic_mode_name(p->wheel_mode));
    emitf("+wheel_func\t%s\r\n", p->wheel_func);
    emitf("+detents\t%u\r\n", p->detents);

    hp_action_format(&p->wheel_cw, action, sizeof action);
    emitf("+wheel_cw\t%s\r\n", action);
    hp_action_format(&p->wheel_ccw, action, sizeof action);
    emitf("+wheel_ccw\t%s\r\n", action);

    for (int i = 0; i < HP_MACRO_COUNT; i++) {
        const hp_button_t *b = &p->buttons[i];
        emitf("+btn%d_label\t%s\r\n", i + 1, b->label);
        emitf("+btn%d_icon\t%s%s\r\n", i + 1, b->icon_file,
              b->has_icon ? "" : " (not loaded)");
        for (int s = 0; s < b->step_count; s++) {
            hp_action_format(&b->steps[s], action, sizeof action);
            emitf("+btn%d_step%d\t%s\t%u\r\n", i + 1, s + 1, action, b->steps[s].delay_ms);
        }
    }
    reply_ok_plain();
}

/* --- command dispatch ---------------------------------------------------- */

static void handle_command(char *line) {
    char *verb, *arg1, *rest;
    split(line, &verb, &arg1, &rest);

    if (!strcasecmp(verb, "PING")) {
        reply_ok("%s %s proto=%d", HP_FW_NAME, HP_FW_VERSION, HP_PROTO_VERSION);
    }
    else if (!strcasecmp(verb, "INFO")) {
        cmd_info();
    }
    else if (!strcasecmp(verb, "STATUS")) {
        cmd_status();
    }
    else if (!strcasecmp(verb, "CONFIG")) {
        static char buf[HP_FILE_BUF_BYTES];
        size_t len = hp_settings_serialize(buf, sizeof buf);
        b64_emit((const uint8_t *)buf, len);
        reply_ok("%u", (unsigned)len);
    }
    else if (!strcasecmp(verb, "SET")) {
        if (!arg1) { reply_err("usage: SET <key> <value>"); return; }
        if (!hp_settings_set(arg1, rest ? rest : "")) { reply_err("unknown key or value"); return; }
        hp_settings_apply();
        hp_settings_touch();
        hp_ui_notify_activity();
        hp_ui_invalidate();
        reply_ok_plain();
    }
    else if (!strcasecmp(verb, "SAVE")) {
        if (hp_settings_save()) reply_ok_plain();
        else reply_err("%s", hp_storage_last_error());
    }
    else if (!strcasecmp(verb, "PROFILES")) {
        for (int i = 0; i < hp_profile_count(); i++) {
            emitf("+%d\t%s\t%s\r\n", i, hp_profile_filename(i),
                  i == hp_profile_active() ? "active" : "");
        }
        reply_ok("%d", hp_profile_count());
    }
    else if (!strcasecmp(verb, "PROFILE")) {
        if (!arg1) { cmd_profile_dump(); return; }
        if (!hp_profile_load_by_name(arg1)) { reply_err("no such profile"); return; }
        hp_settings_apply();
        hp_ini_copy(hp_settings()->last_profile, HP_PATH_LEN, arg1);
        hp_settings_touch();
        hp_ui_invalidate();
        reply_ok("%s", hp_profile_current()->name);
    }
    else if (!strcasecmp(verb, "RESCAN")) {
        hp_storage_remount();
        hp_profile_scan();
        if (hp_profile_count() > 0) {
            if (!hp_profile_load_by_name(hp_settings()->last_profile)) hp_profile_load(0);
        }
        hp_settings_apply();
        hp_ui_invalidate();
        reply_ok("%d", hp_profile_count());
    }
    else if (!strcasecmp(verb, "LIST")) {
        if (!hp_storage_list(arg1 ? arg1 : "/", list_cb, NULL)) {
            reply_err("%s", hp_storage_last_error());
            return;
        }
        reply_ok_plain();
    }
    else if (!strcasecmp(verb, "READ")) {
        if (!arg1) { reply_err("usage: READ <path>"); return; }
        static uint8_t buf[HP_FILE_BUF_BYTES];
        size_t len = 0;
        if (!hp_storage_read(arg1, buf, sizeof buf, &len)) {
            reply_err("%s", hp_storage_last_error());
            return;
        }
        b64_emit(buf, len);
        reply_ok("%u", (unsigned)len);
    }
    else if (!strcasecmp(verb, "WRITE")) {
        if (!arg1) { reply_err("usage: WRITE <path>"); return; }
        hp_ini_copy(s_write_path, sizeof s_write_path, arg1);
        s_write_len = 0;
        s_writing = true;
        reply_ok("send base64, end with a single dot");
    }
    else if (!strcasecmp(verb, "DELETE")) {
        if (!arg1) { reply_err("usage: DELETE <path>"); return; }
        if (!hp_storage_delete(arg1)) { reply_err("%s", hp_storage_last_error()); return; }
        reply_ok_plain();
    }
    else if (!strcasecmp(verb, "MKDIR")) {
        if (!arg1) { reply_err("usage: MKDIR <path>"); return; }
        if (hp_storage_mkdir(arg1)) reply_ok_plain();
        else reply_err("%s", hp_storage_last_error());
    }
    else if (!strcasecmp(verb, "TEST")) {
        if (!arg1) { reply_err("usage: TEST <action>"); return; }
        /* Rebuild the full argument: actions may contain spaces. */
        char action[128];
        snprintf(action, sizeof action, "%s%s%s", arg1, rest ? " " : "", rest ? rest : "");
        hp_step_t step;
        if (!hp_action_parse(action, &step)) { reply_err("cannot parse '%s'", action); return; }
        hp_macro_run_step(&step);
        char canonical[96];
        hp_action_format(&step, canonical, sizeof canonical);
        reply_ok("sent %s", canonical);
    }
    else if (!strcasecmp(verb, "LOG")) {
        if (!arg1) { reply_err("usage: LOG <off|error|warn|info|debug>"); return; }
        if (!strcasecmp(arg1, "off")) { s_log_stream = false; reply_ok("off"); return; }
        s_log_stream = true;
        if (!hp_settings_set("log_level", arg1)) { reply_err("unknown level"); return; }
        hp_settings_apply();
        reply_ok("%s", arg1);
    }
    else if (!strcasecmp(verb, "BOOTSEL")) {
        reply_ok("entering bootloader");
        /* Push the reply out before we go: the reply ring is normally drained
         * at the top of the next task call, which will never happen. */
        tx_pump();
        sleep_ms(120);
        hp_system_enter_bootloader();
    }
    else if (!strcasecmp(verb, "REBOOT")) {
        reply_ok("restarting");
        tx_pump();
        sleep_ms(120);
        hp_system_reboot();
    }
    else if (verb[0] == '\0') {
        /* Blank line: ignore, so a human poking at a terminal is not scolded. */
    }
    else {
        reply_err("unknown command '%s'", verb);
    }
}

static void handle_write_line(const char *line) {
    if (line[0] == '.' && line[1] == '\0') {
        s_writing = false;
        if (hp_storage_write(s_write_path, s_write_buf, s_write_len)) {
            HP_LOGI(TAG, "wrote %s (%u bytes) from USB", s_write_path, (unsigned)s_write_len);
            reply_ok("%u", (unsigned)s_write_len);
        } else {
            reply_err("%s", hp_storage_last_error());
        }
        return;
    }

    if (!b64_append(line)) {
        s_writing = false;
        reply_err("bad base64 or file too large");
    }
}

/* --- log streaming ------------------------------------------------------- */

/* Log output shares the port with command replies, so each line is prefixed
 * with '#'.  Clients skip those lines; a human reading a terminal sees them
 * interleaved exactly where they happened. */
static void stream_logs(void) {
    if (!s_log_stream || !tud_cdc_connected()) return;

    uint8_t buf[128];
    while (hp_log_pending()) {
        /* Leave most of the ring for command replies: a log burst must never
         * be the reason an answer gets truncated. */
        if (tx_space() < TX_RING / 2) break;

        size_t n = hp_log_drain(buf, sizeof buf);
        for (size_t i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (s_log_at_line_start) {
                char prefix[2] = { '#', '\0' };
                emit(prefix);
                s_log_at_line_start = false;
            }
            char one[2] = { c, '\0' };
            emit(one);
            if (c == '\n') s_log_at_line_start = true;
        }
    }
}

/* --- public -------------------------------------------------------------- */

void hp_cdc_init(void) {
    s_line_len = 0;
    s_writing = false;
    s_tx_head = s_tx_tail = s_tx_dropped = 0;
}

void hp_cdc_task(void) {
    tx_pump();
    stream_logs();

    /* Do not start another command while a large reply is still going out:
     * the handler would have nowhere to put its output.  A file upload is
     * exempt - its lines produce no output until the final dot, and stalling
     * mid-transfer would strand the client. */
    if (!s_writing && tx_used() > TX_RING / 4) return;

    while (tud_cdc_available()) {
        char c;
        if (tud_cdc_read(&c, 1) != 1) break;

        if (c == '\r') continue;
        if (c != '\n') {
            if (s_line_len < LINE_MAX - 1) s_line[s_line_len++] = c;
            continue;
        }

        s_line[s_line_len] = '\0';
        s_line_len = 0;

        if (s_writing) handle_write_line(s_line);
        else           handle_command(s_line);
    }
}
