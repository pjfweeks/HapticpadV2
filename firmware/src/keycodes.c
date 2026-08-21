#include "keycodes.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "class/hid/hid.h"
#include "log.h"

#define TAG "keys"

/* --- name tables -------------------------------------------------------- */

typedef struct { const char *name; uint8_t code; } key_entry_t;

static const key_entry_t k_keys[] = {
    { "ENTER", HID_KEY_ENTER },       { "RETURN", HID_KEY_ENTER },
    { "ESC", HID_KEY_ESCAPE },        { "ESCAPE", HID_KEY_ESCAPE },
    { "TAB", HID_KEY_TAB },           { "SPACE", HID_KEY_SPACE },
    { "SPC", HID_KEY_SPACE },         { "BACKSPACE", HID_KEY_BACKSPACE },
    { "BKSP", HID_KEY_BACKSPACE },    { "DELETE", HID_KEY_DELETE },
    { "DEL", HID_KEY_DELETE },        { "INSERT", HID_KEY_INSERT },
    { "INS", HID_KEY_INSERT },        { "HOME", HID_KEY_HOME },
    { "END", HID_KEY_END },           { "PGUP", HID_KEY_PAGE_UP },
    { "PAGEUP", HID_KEY_PAGE_UP },    { "PGDN", HID_KEY_PAGE_DOWN },
    { "PAGEDOWN", HID_KEY_PAGE_DOWN },{ "UP", HID_KEY_ARROW_UP },
    { "DOWN", HID_KEY_ARROW_DOWN },   { "LEFT", HID_KEY_ARROW_LEFT },
    { "RIGHT", HID_KEY_ARROW_RIGHT }, { "CAPS", HID_KEY_CAPS_LOCK },
    { "PRTSC", HID_KEY_PRINT_SCREEN },{ "PRINTSCREEN", HID_KEY_PRINT_SCREEN },
    { "MENU", HID_KEY_APPLICATION },  { "APP", HID_KEY_APPLICATION },
    { "NUMLOCK", HID_KEY_NUM_LOCK },  { "SCROLLLOCK", HID_KEY_SCROLL_LOCK },
    { "PAUSE", HID_KEY_PAUSE },
};

static const key_entry_t k_modifiers[] = {
    { "CTRL",   KEYBOARD_MODIFIER_LEFTCTRL },
    { "CONTROL",KEYBOARD_MODIFIER_LEFTCTRL },
    { "SHIFT",  KEYBOARD_MODIFIER_LEFTSHIFT },
    { "ALT",    KEYBOARD_MODIFIER_LEFTALT },
    { "OPT",    KEYBOARD_MODIFIER_LEFTALT },
    { "GUI",    KEYBOARD_MODIFIER_LEFTGUI },
    { "WIN",    KEYBOARD_MODIFIER_LEFTGUI },
    { "CMD",    KEYBOARD_MODIFIER_LEFTGUI },
    { "RCTRL",  KEYBOARD_MODIFIER_RIGHTCTRL },
    { "RSHIFT", KEYBOARD_MODIFIER_RIGHTSHIFT },
    { "RALT",   KEYBOARD_MODIFIER_RIGHTALT },
    { "ALTGR",  KEYBOARD_MODIFIER_RIGHTALT },
    { "RGUI",   KEYBOARD_MODIFIER_RIGHTGUI },
};

typedef struct { const char *name; uint16_t code; } consumer_entry_t;

static const consumer_entry_t k_consumer[] = {
    { "VOL_UP",   HID_USAGE_CONSUMER_VOLUME_INCREMENT },
    { "VOLUP",    HID_USAGE_CONSUMER_VOLUME_INCREMENT },
    { "VOL_DOWN", HID_USAGE_CONSUMER_VOLUME_DECREMENT },
    { "VOLDOWN",  HID_USAGE_CONSUMER_VOLUME_DECREMENT },
    { "MUTE",     HID_USAGE_CONSUMER_MUTE },
    { "PLAY",     HID_USAGE_CONSUMER_PLAY_PAUSE },
    { "PAUSE_MEDIA", HID_USAGE_CONSUMER_PLAY_PAUSE },
    { "STOP",     HID_USAGE_CONSUMER_STOP },
    { "NEXT",     HID_USAGE_CONSUMER_SCAN_NEXT },
    { "PREV",     HID_USAGE_CONSUMER_SCAN_PREVIOUS },
    { "BRIGHT_UP",   HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT },
    { "BRIGHT_DOWN", HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT },
};

/* --- helpers ------------------------------------------------------------ */

/* Case-insensitive compare of a bounded token against a null-terminated name. */
static bool token_is(const char *tok, size_t len, const char *name) {
    return strlen(name) == len && strncasecmp(tok, name, len) == 0;
}

static bool lookup_modifier(const char *tok, size_t len, uint8_t *out) {
    for (size_t i = 0; i < sizeof k_modifiers / sizeof k_modifiers[0]; i++) {
        if (token_is(tok, len, k_modifiers[i].name)) { *out = k_modifiers[i].code; return true; }
    }
    return false;
}

static bool lookup_named_key(const char *tok, size_t len, uint8_t *out) {
    for (size_t i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++) {
        if (token_is(tok, len, k_keys[i].name)) { *out = k_keys[i].code; return true; }
    }
    /* F1..F24 */
    if (len >= 2 && len <= 3 && (tok[0] == 'F' || tok[0] == 'f')) {
        int n = 0;
        for (size_t i = 1; i < len; i++) {
            if (!isdigit((unsigned char)tok[i])) return false;
            n = n * 10 + (tok[i] - '0');
        }
        if (n >= 1 && n <= 12) { *out = (uint8_t)(HID_KEY_F1 + n - 1); return true; }
        if (n >= 13 && n <= 24) { *out = (uint8_t)(HID_KEY_F13 + n - 13); return true; }
    }
    return false;
}

static bool lookup_consumer(const char *tok, size_t len, uint16_t *out) {
    for (size_t i = 0; i < sizeof k_consumer / sizeof k_consumer[0]; i++) {
        if (token_is(tok, len, k_consumer[i].name)) { *out = k_consumer[i].code; return true; }
    }
    return false;
}

bool hp_char_to_key(char c, uint8_t *key, uint8_t *modifier) {
    *modifier = 0;
    if (c >= 'a' && c <= 'z') { *key = (uint8_t)(HID_KEY_A + (c - 'a')); return true; }
    if (c >= 'A' && c <= 'Z') { *key = (uint8_t)(HID_KEY_A + (c - 'A')); *modifier = KEYBOARD_MODIFIER_LEFTSHIFT; return true; }
    if (c >= '1' && c <= '9') { *key = (uint8_t)(HID_KEY_1 + (c - '1')); return true; }
    if (c == '0') { *key = HID_KEY_0; return true; }

    /* Unshifted punctuation, US layout. */
    switch (c) {
        case ' ':  *key = HID_KEY_SPACE;         return true;
        case '\n': *key = HID_KEY_ENTER;         return true;
        case '\t': *key = HID_KEY_TAB;           return true;
        case '-':  *key = HID_KEY_MINUS;         return true;
        case '=':  *key = HID_KEY_EQUAL;         return true;
        case '[':  *key = HID_KEY_BRACKET_LEFT;  return true;
        case ']':  *key = HID_KEY_BRACKET_RIGHT; return true;
        case '\\': *key = HID_KEY_BACKSLASH;     return true;
        case ';':  *key = HID_KEY_SEMICOLON;     return true;
        case '\'': *key = HID_KEY_APOSTROPHE;    return true;
        case '`':  *key = HID_KEY_GRAVE;         return true;
        case ',':  *key = HID_KEY_COMMA;         return true;
        case '.':  *key = HID_KEY_PERIOD;        return true;
        case '/':  *key = HID_KEY_SLASH;         return true;
        default: break;
    }

    /* Shifted punctuation. */
    static const char *plain   = "1234567890-=[]\\;',./`";
    static const char *shifted = "!@#$%^&*()_+{}|:\"<>?~";
    const char *pos = strchr(shifted, c);
    if (pos) {
        uint8_t dummy;
        if (hp_char_to_key(plain[pos - shifted], key, &dummy)) {
            *modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
            return true;
        }
    }
    return false;
}

/* --- parsing ------------------------------------------------------------ */

/* Splits "CTRL+SHIFT+F12" on '+' and folds each token into the step.  A '+'
 * that is meant literally can be written as the last token, since a trailing
 * empty token is treated as the plus key. */
static bool parse_combo(const char *s, hp_step_t *out) {
    size_t nkeys = 0;
    const char *tok = s;

    for (;;) {
        const char *plus = strchr(tok, '+');
        size_t len = plus ? (size_t)(plus - tok) : strlen(tok);

        if (len == 0 && plus) {
            /* "CTRL++" - the second plus is the literal key. */
            uint8_t k, m;
            if (hp_char_to_key('+', &k, &m) && nkeys < HP_STEP_KEYS) {
                out->keys[nkeys++] = k;
                out->modifiers |= m;
            }
        } else if (len > 0) {
            uint8_t mod;
            if (lookup_modifier(tok, len, &mod)) {
                out->modifiers |= mod;
            } else {
                uint8_t key;
                if (lookup_named_key(tok, len, &key)) {
                    if (nkeys < HP_STEP_KEYS) out->keys[nkeys++] = key;
                } else if (len == 1) {
                    uint8_t k, m;
                    if (!hp_char_to_key(tok[0], &k, &m)) return false;
                    if (nkeys < HP_STEP_KEYS) out->keys[nkeys++] = k;
                    out->modifiers |= m;
                } else {
                    return false;
                }
            }
        }

        if (!plus) break;
        tok = plus + 1;
    }

    return nkeys > 0 || out->modifiers != 0;
}

bool hp_action_parse(const char *action, hp_step_t *out) {
    memset(out, 0, sizeof *out);
    if (!action) return false;

    while (*action == ' ' || *action == '\t') action++;
    if (!*action) return false;

    /* Prefixed forms first - their payload must not be touched by the combo
     * splitter, since typed text can contain '+'. */
    if (strncasecmp(action, "TEXT:", 5) == 0) {
        out->type = HP_STEP_TEXT;
        size_t n = strlen(action + 5);
        if (n >= sizeof out->text) n = sizeof out->text - 1;
        memcpy(out->text, action + 5, n);
        out->text[n] = '\0';
        return n > 0;
    }
    if (strncasecmp(action, "HOLD:", 5) == 0) {
        out->type = HP_STEP_HOLD;
        return parse_combo(action + 5, out);
    }
    if (strcasecmp(action, "RELEASE") == 0 || strncasecmp(action, "RELEASE:", 8) == 0) {
        out->type = HP_STEP_RELEASE;
        return true;
    }
    if (strcasecmp(action, "WAIT") == 0 || strcasecmp(action, "DELAY") == 0) {
        out->type = HP_STEP_WAIT;
        return true;
    }

    /* Mouse and scroll verbs, optionally prefixed with modifiers, e.g.
     * "CTRL+SCROLL_UP" is how the zoom wheel function is expressed. */
    const char *verb = action;
    uint8_t mods = 0;
    for (;;) {
        const char *plus = strchr(verb, '+');
        if (!plus) break;
        uint8_t m;
        if (!lookup_modifier(verb, (size_t)(plus - verb), &m)) break;
        mods |= m;
        verb = plus + 1;
    }

    static const struct { const char *name; int8_t wheel, pan; uint8_t button; }
    k_mouse[] = {
        { "SCROLL_UP",    1,  0, 0 },
        { "SCROLL_DOWN", -1,  0, 0 },
        { "SCROLL_RIGHT", 0,  1, 0 },
        { "SCROLL_LEFT",  0, -1, 0 },
        { "MOUSE_LEFT",   0,  0, MOUSE_BUTTON_LEFT },
        { "MOUSE_RIGHT",  0,  0, MOUSE_BUTTON_RIGHT },
        { "MOUSE_MIDDLE", 0,  0, MOUSE_BUTTON_MIDDLE },
        { "MOUSE_BACK",   0,  0, MOUSE_BUTTON_BACKWARD },
        { "MOUSE_FWD",    0,  0, MOUSE_BUTTON_FORWARD },
    };
    for (size_t i = 0; i < sizeof k_mouse / sizeof k_mouse[0]; i++) {
        if (strcasecmp(verb, k_mouse[i].name) == 0) {
            out->type = HP_STEP_MOUSE;
            out->modifiers = mods;
            out->mouse_wheel = k_mouse[i].wheel;
            out->mouse_pan = k_mouse[i].pan;
            out->mouse_buttons = k_mouse[i].button;
            return true;
        }
    }

    uint16_t consumer;
    if (lookup_consumer(action, strlen(action), &consumer)) {
        out->type = HP_STEP_CONSUMER;
        out->consumer = consumer;
        return true;
    }

    out->type = HP_STEP_KEY;
    if (parse_combo(action, out)) return true;

    memset(out, 0, sizeof *out);
    HP_LOGW(TAG, "unrecognised action '%s'", action);
    return false;
}

/* --- formatting --------------------------------------------------------- */

static void append(char *dst, size_t size, const char *s) {
    size_t used = strlen(dst);
    if (used + 1 >= size) return;
    snprintf(dst + used, size - used, "%s", s);
}

void hp_action_format(const hp_step_t *step, char *dst, size_t dst_size) {
    if (!dst_size) return;
    dst[0] = '\0';

    switch (step->type) {
        case HP_STEP_NONE:
            return;
        case HP_STEP_WAIT:
            append(dst, dst_size, "WAIT");
            return;
        case HP_STEP_RELEASE:
            append(dst, dst_size, "RELEASE");
            return;
        case HP_STEP_TEXT:
            snprintf(dst, dst_size, "TEXT:%s", step->text);
            return;
        case HP_STEP_CONSUMER:
            for (size_t i = 0; i < sizeof k_consumer / sizeof k_consumer[0]; i++) {
                if (k_consumer[i].code == step->consumer) {
                    append(dst, dst_size, k_consumer[i].name);
                    return;
                }
            }
            snprintf(dst, dst_size, "CONSUMER:0x%04X", step->consumer);
            return;
        default:
            break;
    }

    if (step->type == HP_STEP_HOLD) append(dst, dst_size, "HOLD:");

    static const struct { uint8_t bit; const char *name; } k_mod_names[] = {
        { KEYBOARD_MODIFIER_LEFTCTRL,  "CTRL"   },
        { KEYBOARD_MODIFIER_LEFTSHIFT, "SHIFT"  },
        { KEYBOARD_MODIFIER_LEFTALT,   "ALT"    },
        { KEYBOARD_MODIFIER_LEFTGUI,   "GUI"    },
        { KEYBOARD_MODIFIER_RIGHTCTRL, "RCTRL"  },
        { KEYBOARD_MODIFIER_RIGHTSHIFT,"RSHIFT" },
        { KEYBOARD_MODIFIER_RIGHTALT,  "RALT"   },
        { KEYBOARD_MODIFIER_RIGHTGUI,  "RGUI"   },
    };
    for (size_t i = 0; i < sizeof k_mod_names / sizeof k_mod_names[0]; i++) {
        if (step->modifiers & k_mod_names[i].bit) {
            append(dst, dst_size, k_mod_names[i].name);
            append(dst, dst_size, "+");
        }
    }

    if (step->type == HP_STEP_MOUSE) {
        if (step->mouse_wheel > 0)      append(dst, dst_size, "SCROLL_UP");
        else if (step->mouse_wheel < 0) append(dst, dst_size, "SCROLL_DOWN");
        else if (step->mouse_pan > 0)   append(dst, dst_size, "SCROLL_RIGHT");
        else if (step->mouse_pan < 0)   append(dst, dst_size, "SCROLL_LEFT");
        else if (step->mouse_buttons == MOUSE_BUTTON_LEFT)   append(dst, dst_size, "MOUSE_LEFT");
        else if (step->mouse_buttons == MOUSE_BUTTON_RIGHT)  append(dst, dst_size, "MOUSE_RIGHT");
        else if (step->mouse_buttons == MOUSE_BUTTON_MIDDLE) append(dst, dst_size, "MOUSE_MIDDLE");
        return;
    }

    for (int i = 0; i < HP_STEP_KEYS; i++) {
        uint8_t k = step->keys[i];
        if (!k) continue;
        if (i > 0) append(dst, dst_size, "+");

        char one[12];
        if (k >= HID_KEY_A && k <= HID_KEY_Z) {
            snprintf(one, sizeof one, "%c", 'A' + (k - HID_KEY_A));
        } else if (k >= HID_KEY_1 && k <= HID_KEY_9) {
            snprintf(one, sizeof one, "%c", '1' + (k - HID_KEY_1));
        } else if (k == HID_KEY_0) {
            snprintf(one, sizeof one, "0");
        } else if (k >= HID_KEY_F1 && k <= HID_KEY_F12) {
            snprintf(one, sizeof one, "F%d", k - HID_KEY_F1 + 1);
        } else {
            one[0] = '\0';
            for (size_t j = 0; j < sizeof k_keys / sizeof k_keys[0]; j++) {
                if (k_keys[j].code == k) { snprintf(one, sizeof one, "%s", k_keys[j].name); break; }
            }
            if (!one[0]) snprintf(one, sizeof one, "0x%02X", k);
        }
        append(dst, dst_size, one);
    }

    /* A trailing '+' means the action was modifiers only, which is legal. */
    size_t n = strlen(dst);
    if (n && dst[n - 1] == '+') dst[n - 1] = '\0';
}
