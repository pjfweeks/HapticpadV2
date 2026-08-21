/* board.h - HapticPad hardware map and compile-time configuration.
 *
 * Every pin number, peripheral assignment and hard limit lives here so that a
 * hardware revision touches exactly one file.  Pin choices are inherited from
 * the existing PCB (see the v1 Arduino firmware Hardware.h) and are
 * NOT free to change without respinning the board.
 */
#ifndef HP_BOARD_H
#define HP_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Display - SSD1309 128x64, 4-wire SPI                               */
/* ------------------------------------------------------------------ */
/* GP28 (clock) and GP22 (data) are NOT a valid hardware-SPI pin pair on the
 * RP2040, which is why the Arduino firmware fell back to bit-banged software
 * SPI and spent tens of milliseconds per frame.  We drive them from PIO
 * instead: same pins, ~8 MHz, DMA-fed, zero CPU time per frame.            */
#define HP_DISP_SCK   28
#define HP_DISP_MOSI  22
#define HP_DISP_CS     6
#define HP_DISP_DC     7
#define HP_DISP_RST    8

#define HP_DISP_W    128
#define HP_DISP_H     64
/* Set to true if the panel is mounted upside down relative to the enclosure. */
#define HP_DISP_FLIP true

/* ------------------------------------------------------------------ */
/* microSD - SPI0 (a genuine hardware-SPI pin set)                     */
/* ------------------------------------------------------------------ */
#define HP_SD_SPI      spi0
#define HP_SD_SCK       2
#define HP_SD_MOSI      3
#define HP_SD_MISO      4
#define HP_SD_CS        5
#define HP_SD_BAUD_INIT   400000    /* card spec: <=400 kHz until initialised */
#define HP_SD_BAUD_RUN  12000000

/* ------------------------------------------------------------------ */
/* WS2812 LED ring                                                     */
/* ------------------------------------------------------------------ */
#define HP_LED_PIN     15
#define HP_LED_COUNT   20

/* ------------------------------------------------------------------ */
/* BLDC motor - TMC6300, 6-PWM                                         */
/* ------------------------------------------------------------------ */
/* Each high/low pair shares one PWM slice, which lets the hardware generate
 * complementary outputs with dead time instead of doing it in software:
 *   slice 0 -> GP16/GP17,  slice 1 -> GP18/GP19,  slice 2 -> GP20/GP21      */
#define HP_MOT_U_H     20
#define HP_MOT_U_L     21
#define HP_MOT_V_H     18
#define HP_MOT_V_L     19
#define HP_MOT_W_H     16
#define HP_MOT_W_L     17

#define HP_MOT_POLE_PAIRS      7
#define HP_MOT_VOLTAGE_SUPPLY  5.0f
#define HP_MOT_VOLTAGE_LIMIT   5.0f
#define HP_MOT_ALIGN_VOLTAGE   3.0f
#define HP_MOT_PWM_FREQ_HZ     24000
#define HP_MOT_DEAD_TIME       0.02f   /* fraction of the PWM period */

/* ------------------------------------------------------------------ */
/* Quadrature encoder - decoded in PIO, not by interrupts              */
/* ------------------------------------------------------------------ */
/* The PIO program samples both phases with `in pins`, so A and B must be
 * consecutive GPIOs with B at the lower number.                             */
#define HP_ENC_B       26
#define HP_ENC_A       27
#define HP_ENC_PPR   1024           /* pulses per revolution, x4 in quadrature */
#define HP_ENC_CPR   (HP_ENC_PPR * 4)

/* ------------------------------------------------------------------ */
/* Buttons                                                             */
/* ------------------------------------------------------------------ */
#define HP_BTN_COUNT    8
#define HP_MACRO_COUNT  6           /* buttons 0..5 run macros            */
#define HP_BTN_NEXT     6           /* profile forward                    */
#define HP_BTN_PREV     7           /* profile back                       */

/* Index 0..5 follow the physical grid order shown on the display.          */
#define HP_BTN_PINS { 9, 1, 0, 12, 11, 10, 13, 14 }

#define HP_BTN_DEBOUNCE_US   8000   /* signal must hold this long to count  */
#define HP_BTN_SCAN_US        500   /* scan period                          */
#define HP_BTN_REPEAT_MS      400   /* hold-to-repeat: first repeat delay   */
#define HP_BTN_REPEAT_RATE_MS  80   /* hold-to-repeat: subsequent interval  */
#define HP_BTN_COMBO_MS       250   /* window for the NEXT+PREV combo       */

/* ------------------------------------------------------------------ */
/* PIO / DMA allocation                                                */
/* ------------------------------------------------------------------ */
/* Fixed up front so two subsystems can never claim the same state machine.
 * pio0/sm0 is left free as headroom: the display and LED programs both fit
 * alongside it, and a spare state machine on the same block is what any
 * future addition (a second data pin, an IR receiver) would need.          */
#define HP_PIO_LED       pio0
#define HP_PIO_LED_SM    1
#define HP_PIO_DISP      pio0
#define HP_PIO_DISP_SM   2
#define HP_PIO_ENC       pio1
#define HP_PIO_ENC_SM    0

/* ------------------------------------------------------------------ */
/* Sizing limits - all storage is static, nothing is malloc'd          */
/* ------------------------------------------------------------------ */
#define HP_MAX_PROFILES      32
#define HP_MAX_STEPS          8     /* macro steps per button (was 3)       */
#define HP_LABEL_LEN         12
#define HP_NAME_LEN          20
#define HP_PATH_LEN          64
#define HP_TEXT_LEN          64     /* payload of a TEXT "type this" step   */
#define HP_ICON_BYTES        72     /* 24x24 monochrome, XBM bit order      */
#define HP_ICON_W            24
#define HP_ICON_H            24

/* Firmware identity, reported to the configurator over USB CDC. */
#define HP_FW_NAME     "HapticPad"
#define HP_FW_VERSION  "2.0.0"
#define HP_PROTO_VERSION 1

#ifdef __cplusplus
}
#endif

#endif /* HP_BOARD_H */
