#include "haptics.h"

#include <math.h>

#include "board.h"
#include "encoder.h"
#include "ipc.h"
#include "log.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

/* Include only the pieces we build: SimpleFOC.h drags in every sensor driver,
 * several of which need Arduino's SPI/Wire libraries we deliberately do not
 * have. */
#include "BLDCMotor.h"
#include "common/base_classes/Sensor.h"
#include "drivers/BLDCDriver6PWM.h"

#define TAG "foc"

namespace {

/* The PIO decoder already maintains an exact count, so the sensor adapter is
 * a pure conversion - no interrupts, no shared state, no locking. */
class PioEncoderSensor : public Sensor {
public:
    using Sensor::init;             /* protected upstream; we call it directly */

    float getSensorAngle() override {
        int32_t counts = hp_encoder_count() % HP_ENC_CPR;
        if (counts < 0) counts += HP_ENC_CPR;
        return (float)counts * (_2PI / (float)HP_ENC_CPR);
    }
};

PioEncoderSensor sensor;
BLDCMotor      motor(HP_MOT_POLE_PAIRS);
BLDCDriver6PWM driver(HP_MOT_U_H, HP_MOT_U_L,
                      HP_MOT_V_H, HP_MOT_V_L,
                      HP_MOT_W_H, HP_MOT_W_L);

/* Torque scaling constants, carried over from the tuning that was already
 * working on this hardware.  Strength multiplies all of them. */
constexpr float DETENT_GAIN     = 3.0f;
constexpr float DETENT_DAMPING  = 0.10f;
constexpr float CARDINAL_GAIN   = 6.0f;
constexpr float VISCOUS_GAIN    = 0.35f;
constexpr float RATCHET_GAIN    = 8.0f;
constexpr float FREE_FRICTION   = 0.02f;

/* --- feel ------------------------------------------------------------ */

float torque_for(const hp_haptic_cmd_t &cmd, float angle, float velocity,
                 float &ratchet_anchor) {
    const float k = cmd.strength;

    switch (cmd.mode) {
        case HP_HAPTIC_CLICKY: {
            /* A sine well per detent: the wheel is pulled toward the nearest
             * multiple of 2*pi/detents, with damping so it settles instead of
             * ringing.
             *
             * _sin() is SimpleFOC's table lookup rather than libm's sinf().
             * The M0+ has no FPU, so a software sine in a loop that runs
             * thousands of times a second is worth avoiding. */
            float n = (float)(cmd.detents ? cmd.detents : 24);
            return -DETENT_GAIN * k * _sin(_normalizeAngle(angle * n))
                   - DETENT_DAMPING * k * velocity;
        }

        case HP_HAPTIC_4POINTS:
            return -CARDINAL_GAIN * k * _sin(_normalizeAngle(angle * 4.0f))
                   - 0.2f * k * velocity;

        case HP_HAPTIC_DAMPED:
            /* Resistance grows with speed: the knob feels like it is turning
             * in oil. */
            return -VISCOUS_GAIN * k * velocity;

        case HP_HAPTIC_RATCHET: {
            /* Free in one direction; the anchor follows the wheel forwards and
             * refuses to let it back. */
            if (angle > ratchet_anchor) ratchet_anchor = angle;
            float excursion = ratchet_anchor - angle;
            if (excursion <= 0.0f) return -FREE_FRICTION * velocity;
            return RATCHET_GAIN * k * excursion - 0.2f * k * velocity;
        }

        case HP_HAPTIC_FREE:
            /* Not quite zero: a whisper of friction stops the wheel spinning
             * forever after a flick. */
            return -FREE_FRICTION * velocity;

        case HP_HAPTIC_OFF:
        default:
            return 0.0f;
    }
}

/* --- detent ticks ---------------------------------------------------- */

/* Reports one tick per detent crossed, in whichever mode - a free-spinning
 * wheel still has to scroll. */
void emit_ticks(const hp_haptic_cmd_t &cmd, float angle, float &last_tick_angle) {
    uint16_t detents = cmd.detents ? cmd.detents : 24;
    if (cmd.mode == HP_HAPTIC_4POINTS) detents = 4;

    /* Cached across iterations: the divide only matters when the profile
     * changes the detent count, which is roughly never. */
    static uint16_t cached_detents = 0;
    static float step = 0.0f;
    if (detents != cached_detents) {
        cached_detents = detents;
        step = _2PI / (float)detents;
    }

    float delta = angle - last_tick_angle;

    /* A single pass may need to emit several ticks if the wheel was spun hard
     * between iterations, but cap it so a glitch cannot flood the queue. */
    int guard = 0;
    while (fabsf(delta) >= step && guard++ < 32) {
        int8_t dir = delta > 0 ? 1 : -1;
        hp_ipc_tick_push(dir);
        last_tick_angle += (float)dir * step;
        delta = angle - last_tick_angle;
    }
    if (guard >= 32) last_tick_angle = angle;
}

/* --- core 1 ---------------------------------------------------------- */

void run_setup() {
    driver.voltage_power_supply = HP_MOT_VOLTAGE_SUPPLY;
    driver.voltage_limit        = HP_MOT_VOLTAGE_LIMIT;
    driver.pwm_frequency        = HP_MOT_PWM_FREQ_HZ;
    driver.dead_zone            = HP_MOT_DEAD_TIME;
    driver.init();

    sensor.init();
    motor.linkSensor(&sensor);
    motor.linkDriver(&driver);

    motor.foc_modulation      = FOCModulationType::SpaceVectorPWM;
    motor.controller          = MotionControlType::torque;
    motor.torque_controller   = TorqueControlType::voltage;
    motor.voltage_sensor_align = HP_MOT_ALIGN_VOLTAGE;
    motor.voltage_limit       = HP_MOT_VOLTAGE_LIMIT;
    motor.velocity_limit      = 100.0f;

    /* Velocity filtering matters even in torque mode: the damping terms all
     * read shaft_velocity, and an unfiltered value makes the wheel buzz. */
    motor.LPF_velocity.Tf     = 0.01f;
    motor.PID_velocity.P      = 0.2f;
    motor.PID_velocity.I      = 10.0f;
    motor.PID_velocity.D      = 0.0f;

    motor.init();
    motor.initFOC();

    HP_LOGI(TAG, "FOC ready: zero angle %.3f, direction %d",
            (double)motor.zero_electric_angle, (int)motor.sensor_direction);
}

void core1_entry() {
    run_setup();

    hp_haptic_cmd_t cmd;
    hp_ipc_cmd_get(&cmd);
    uint32_t calibrate_seen = cmd.calibrate_seq;

    float last_tick_angle = sensor.getAngle();
    float ratchet_anchor  = last_tick_angle;
    bool  motor_enabled   = true;

    uint32_t loops = 0;
    absolute_time_t next_publish = get_absolute_time();
    absolute_time_t rate_window  = get_absolute_time();
    uint32_t loop_hz = 0;

    hp_haptic_state_t state = {};
    state.ready = true;

    for (;;) {
        motor.loopFOC();

        hp_ipc_cmd_get(&cmd);

        if (cmd.calibrate_seq != calibrate_seen) {
            calibrate_seen = cmd.calibrate_seq;
            HP_LOGI(TAG, "re-running alignment on request");
            state.ready = false;
            hp_ipc_state_set(&state);
            motor.initFOC();
            state.ready = true;
            state.fault = motor.motor_status == FOCMotorStatus::motor_ready
                        ? HP_FAULT_NONE : HP_FAULT_ALIGN_FAILED;
            last_tick_angle = sensor.getAngle();
            ratchet_anchor  = last_tick_angle;
        }

        if (cmd.enabled != motor_enabled) {
            motor_enabled = cmd.enabled;
            if (motor_enabled) motor.enable(); else motor.disable();
            HP_LOGD(TAG, "motor %s", motor_enabled ? "enabled" : "parked");
        }

        float angle    = sensor.getAngle();
        float velocity = motor.shaft_velocity;

        emit_ticks(cmd, angle, last_tick_angle);

        if (motor_enabled) {
            motor.move(torque_for(cmd, angle, velocity, ratchet_anchor));
        }

        loops++;
        if (absolute_time_diff_us(rate_window, get_absolute_time()) >= 1000000) {
            loop_hz = loops;
            loops = 0;
            rate_window = get_absolute_time();
        }

        /* Telemetry at 100 Hz is plenty for the LEDs and the status screen,
         * and keeps the seqlock writes out of the control loop's way. */
        if (absolute_time_diff_us(get_absolute_time(), next_publish) <= 0) {
            next_publish = make_timeout_time_ms(10);
            state.angle     = angle;
            state.velocity  = velocity;
            state.voltage_q = motor.voltage.q;
            state.loop_hz   = loop_hz;
            state.heartbeat++;
            hp_ipc_state_set(&state);
        }
    }
}

}  /* namespace */

extern "C" void hp_haptics_start(void) {
    multicore_launch_core1(core1_entry);
}
