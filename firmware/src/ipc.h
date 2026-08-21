/* ipc.h - the only sanctioned channel between core 0 and core 1.
 *
 * The Arduino firmware let both cores call into TinyUSB and share Arduino
 * Strings, which is what corrupted and dropped HID reports.  Here the split
 * is absolute:
 *
 *   core 1 (haptics)          core 0 (application)
 *   -----------------         --------------------
 *   produces wheel ticks  ->  hp_ipc_tick_pop()   -> HID
 *   publishes motor state ->  hp_ipc_state_get()  -> LEDs, UI
 *   hp_ipc_cmd_get()      <-  hp_ipc_cmd_set()       mode/strength changes
 *
 * No mutexes and no blocking anywhere: the tick path is a single-producer /
 * single-consumer ring, the two shared structs use seqlocks.  Core 1 can
 * therefore never be stalled by anything core 0 is doing.
 */
#ifndef HP_IPC_H
#define HP_IPC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Haptic feel applied by the motor.  Values are persisted in config files,
 * so append new modes at the end rather than renumbering. */
typedef enum {
    HP_HAPTIC_OFF = 0,
    HP_HAPTIC_FREE,        /* free spin with simulated inertia          */
    HP_HAPTIC_CLICKY,      /* magnetic detents, detent_count per turn   */
    HP_HAPTIC_RATCHET,     /* free one way, blocked the other           */
    HP_HAPTIC_DAMPED,      /* viscous resistance proportional to speed  */
    HP_HAPTIC_4POINTS,     /* four strong cardinal positions            */
    HP_HAPTIC_COUNT
} hp_haptic_mode_t;

/* Command block: core 0 writes, core 1 reads. */
typedef struct {
    hp_haptic_mode_t mode;
    float    strength;      /* 0.2 .. 5.0, scales detent/damping torque  */
    uint16_t detents;       /* detents per revolution in CLICKY mode     */
    uint32_t calibrate_seq; /* bump to request a fresh FOC alignment     */
    bool     enabled;       /* false parks the motor (screensaver, idle) */
} hp_haptic_cmd_t;

/* State block: core 1 writes, core 0 reads. */
typedef struct {
    float    angle;         /* shaft angle, radians, unwrapped           */
    float    velocity;      /* rad/s, low-pass filtered                  */
    float    voltage_q;     /* commanded q-axis voltage, for diagnostics */
    uint32_t loop_hz;       /* measured FOC loop rate                    */
    uint32_t heartbeat;     /* bumped on every publish; core 0 watches it */
    bool     ready;         /* FOC aligned and running                   */
    uint8_t  fault;         /* 0 = none, see HP_FAULT_* below            */
} hp_haptic_state_t;

#define HP_FAULT_NONE          0
#define HP_FAULT_ALIGN_FAILED  1
#define HP_FAULT_NO_SENSOR     2

void hp_ipc_init(void);

/* --- wheel ticks: core 1 produces, core 0 consumes --------------------- */
/* dir is +1 (clockwise) or -1.  Returns false if the ring is full, which
 * only happens if core 0 has been blocked for an implausibly long time. */
bool hp_ipc_tick_push(int8_t dir);
bool hp_ipc_tick_pop(int8_t *dir);
uint32_t hp_ipc_tick_lost(void);

/* --- command block ------------------------------------------------------ */
void hp_ipc_cmd_set(const hp_haptic_cmd_t *cmd);
void hp_ipc_cmd_get(hp_haptic_cmd_t *out);

/* --- state block -------------------------------------------------------- */
void hp_ipc_state_set(const hp_haptic_state_t *state);
void hp_ipc_state_get(hp_haptic_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HP_IPC_H */
