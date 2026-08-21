/* Arduino.h - minimal Arduino compatibility shim.
 *
 * This header exists for exactly one reason: to let the upstream SimpleFOC
 * sources compile unmodified against the bare-metal Pico SDK, so the motor
 * tuning carried over from the Arduino firmware keeps behaving identically.
 *
 * It is deliberately tiny.  Only the handful of symbols that the SimpleFOC
 * files we actually build reference are defined here:
 *
 *   pinMode digitalWrite digitalRead
 *   millis micros delay delayMicroseconds
 *   min max abs constrain map
 *   class Print          (FOCMotor::useMonitoring)
 *
 * Nothing in HapticPad's own code may include this header - application code
 * talks to the SDK directly.  SimpleFOC's debug layer is compiled out via
 * -DSIMPLEFOC_DISABLE_DEBUG, which removes the need for String/F()/Serial.
 */
#ifndef HP_ARDUINO_COMPAT_H
#define HP_ARDUINO_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pull real <cmath> in before we shadow abs() with a macro, otherwise the
 * macro would wreck the standard declarations. */
#ifdef __cplusplus
#include <cmath>
#endif

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"

#define HIGH 1
#define LOW  0

#define INPUT        0
#define OUTPUT       1
#define INPUT_PULLUP 2

#ifndef PROGMEM
#define PROGMEM
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- digital I/O ------------------------------------------------------- */
static inline void pinMode(uint pin, uint8_t mode) {
    gpio_init(pin);
    if (mode == OUTPUT) {
        gpio_set_dir(pin, GPIO_OUT);
    } else {
        gpio_set_dir(pin, GPIO_IN);
        if (mode == INPUT_PULLUP) gpio_pull_up(pin);
    }
}

static inline void digitalWrite(uint pin, uint8_t value) { gpio_put(pin, value != 0); }
static inline int  digitalRead(uint pin)                 { return gpio_get(pin) ? HIGH : LOW; }

/* --- time -------------------------------------------------------------- */
static inline unsigned long millis(void) { return (unsigned long)(time_us_64() / 1000ull); }
static inline unsigned long micros(void) { return (unsigned long)time_us_32(); }
static inline void delay(unsigned long ms)             { sleep_ms(ms); }
static inline void delayMicroseconds(unsigned int us)  { sleep_us(us); }

#ifdef __cplusplus
}   /* extern "C" */
#endif

/* --- arithmetic helpers ------------------------------------------------ */
/* Macro forms, matching Arduino, because SimpleFOC calls them with mixed
 * int/float/long arguments that a template overload set would reject. */
#undef min
#undef max
#undef abs
#undef constrain
#undef map
#define min(a, b)          ((a) < (b) ? (a) : (b))
#define max(a, b)          ((a) > (b) ? (a) : (b))
#define abs(x)             ((x) > 0 ? (x) : -(x))
#define constrain(x, l, h) ((x) < (l) ? (l) : ((x) > (h) ? (h) : (x)))
#define map(x, iL, iH, oL, oH) (((x) - (iL)) * ((oH) - (oL)) / ((iH) - (iL)) + (oL))

#ifdef __cplusplus

/* --- Print ------------------------------------------------------------- */
/* FOCMotor keeps a Print* for its monitoring output.  We provide just the
 * overloads FOCMotor::monitor() actually calls; hp_motor_monitor_sink() in
 * haptics.cpp implements write() on top of the CDC debug channel. */
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(const uint8_t *buf, size_t len) = 0;

    size_t print(char c)          { return write((const uint8_t *)&c, 1); }
    size_t print(const char *s)   { return write((const uint8_t *)s, strlen(s)); }
    size_t print(int v)           { char b[16]; int n = snprintf(b, sizeof b, "%d", v); return write((const uint8_t *)b, n); }
    size_t print(float v, int decimals = 2) {
        char b[24];
        int n = snprintf(b, sizeof b, "%.*f", decimals, (double)v);
        return write((const uint8_t *)b, n);
    }
    size_t println()              { return print("\r\n"); }
    size_t println(char c)        { return print(c) + print("\r\n"); }
    size_t println(const char *s) { return print(s) + print("\r\n"); }
    size_t println(float v, int decimals = 2) { return print(v, decimals) + print("\r\n"); }
};

/* --- SimpleFOCDebug -----------------------------------------------------
 * SIMPLEFOC_DISABLE_DEBUG removes upstream's SimpleFOCDebug class, but the
 * RP2040 driver calls it directly outside the debug guards.  Rather than drag
 * in Arduino's String/F()/Serial machinery just to satisfy those few calls,
 * we supply our own class that funnels the text into the firmware log, where
 * it shows up on the CDC debug channel alongside everything else. */
extern "C" void hp_sfoc_debug_putc(char c);
extern "C" void hp_sfoc_debug_puts(const char *s);
extern "C" void hp_sfoc_debug_flush(void);

class SimpleFOCDebug {
public:
    static void print(const char *s) { hp_sfoc_debug_puts(s); }
    static void print(int v)         { char b[16]; snprintf(b, sizeof b, "%d", v); hp_sfoc_debug_puts(b); }
    static void print(float v)       { char b[24]; snprintf(b, sizeof b, "%.4f", (double)v); hp_sfoc_debug_puts(b); }
    static void println()                    { hp_sfoc_debug_flush(); }
    static void println(const char *s)       { hp_sfoc_debug_puts(s); hp_sfoc_debug_flush(); }
    static void println(int v)               { print(v); hp_sfoc_debug_flush(); }
    static void println(float v)             { print(v); hp_sfoc_debug_flush(); }
    static void println(const char *s, int v)   { hp_sfoc_debug_puts(s); print(v); hp_sfoc_debug_flush(); }
    static void println(const char *s, float v) { hp_sfoc_debug_puts(s); print(v); hp_sfoc_debug_flush(); }
    static void enable(Print *p = nullptr)   { (void)p; }
};

#endif /* __cplusplus */

#endif /* HP_ARDUINO_COMPAT_H */
