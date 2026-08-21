#include "ipc.h"

#include <string.h>

#include "hardware/sync.h"
#include "pico/stdlib.h"

#define TICK_RING 64u              /* power of two */

static volatile int8_t   s_ticks[TICK_RING];
static volatile uint32_t s_tick_head;   /* written by core 1 */
static volatile uint32_t s_tick_tail;   /* written by core 0 */
static volatile uint32_t s_tick_lost;

/* Seqlock: the writer bumps the counter to odd, writes, bumps to even.  A
 * reader that sees an odd or changed counter retries.  Single writer per
 * block, so no lock is needed on the write side either. */
typedef struct {
    volatile uint32_t seq;
    hp_haptic_cmd_t   value;
} cmd_slot_t;

typedef struct {
    volatile uint32_t seq;
    hp_haptic_state_t value;
} state_slot_t;

static cmd_slot_t   s_cmd;
static state_slot_t s_state;

void hp_ipc_init(void) {
    s_tick_head = s_tick_tail = s_tick_lost = 0;

    s_cmd.seq = 0;
    s_cmd.value.mode      = HP_HAPTIC_CLICKY;
    s_cmd.value.strength  = 1.0f;
    s_cmd.value.detents   = 24;
    s_cmd.value.calibrate_seq = 0;
    s_cmd.value.enabled   = true;

    s_state.seq = 0;
    memset((void *)&s_state.value, 0, sizeof s_state.value);
}

bool hp_ipc_tick_push(int8_t dir) {
    uint32_t head = s_tick_head;
    uint32_t next = head + 1;
    if (next - s_tick_tail > TICK_RING) {
        s_tick_lost++;
        return false;
    }
    s_ticks[head & (TICK_RING - 1)] = dir;
    __dmb();                        /* payload visible before the index moves */
    s_tick_head = next;
    return true;
}

bool hp_ipc_tick_pop(int8_t *dir) {
    uint32_t tail = s_tick_tail;
    if (tail == s_tick_head) return false;
    __dmb();
    *dir = s_ticks[tail & (TICK_RING - 1)];
    s_tick_tail = tail + 1;
    return true;
}

uint32_t hp_ipc_tick_lost(void) { return s_tick_lost; }

void hp_ipc_cmd_set(const hp_haptic_cmd_t *cmd) {
    s_cmd.seq++;                    /* -> odd: write in progress */
    __dmb();
    s_cmd.value = *cmd;
    __dmb();
    s_cmd.seq++;                    /* -> even: consistent       */
}

void hp_ipc_cmd_get(hp_haptic_cmd_t *out) {
    uint32_t before, after;
    do {
        before = s_cmd.seq;
        __dmb();
        *out = s_cmd.value;
        __dmb();
        after = s_cmd.seq;
    } while ((before & 1u) || before != after);
}

void hp_ipc_state_set(const hp_haptic_state_t *state) {
    s_state.seq++;
    __dmb();
    s_state.value = *state;
    __dmb();
    s_state.seq++;
}

void hp_ipc_state_get(hp_haptic_state_t *out) {
    uint32_t before, after;
    do {
        before = s_state.seq;
        __dmb();
        *out = s_state.value;
        __dmb();
        after = s_state.seq;
    } while ((before & 1u) || before != after);
}
