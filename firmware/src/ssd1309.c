#include "ssd1309.h"

#include "board.h"
#include "log.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "disp_spi.pio.h"

#define TAG "disp"

static int  s_dma = -1;
static uint s_offset;
static bool s_flipped = HP_DISP_FLIP;

/* --- low level ---------------------------------------------------------- */

/* The PIO keeps shifting for a few cycles after the FIFO drains, so CS and DC
 * must not move until the shift register is empty too. */
static void wait_idle(void) {
    while (!pio_sm_is_tx_fifo_empty(HP_PIO_DISP, HP_PIO_DISP_SM)) tight_loop_contents();
    /* One byte at 8 MHz takes 1 us; 3 us is a comfortable margin. */
    busy_wait_us(3);
}

static void send_bytes(const uint8_t *data, size_t len, bool is_data) {
    /* Never interleave with a frame transfer that is still in flight. */
    if (s_dma >= 0) dma_channel_wait_for_finish_blocking((uint)s_dma);
    wait_idle();
    gpio_put(HP_DISP_DC, is_data);
    gpio_put(HP_DISP_CS, 0);
    for (size_t i = 0; i < len; i++) {
        /* The PIO auto-pulls 8 bits from the MSB end of the 32-bit word. */
        pio_sm_put_blocking(HP_PIO_DISP, HP_PIO_DISP_SM, (uint32_t)data[i] << 24);
    }
    wait_idle();
    gpio_put(HP_DISP_CS, 1);
}

static void cmd(uint8_t c)                    { send_bytes(&c, 1, false); }
static void cmd2(uint8_t c, uint8_t a)        { uint8_t b[2] = {c, a}; send_bytes(b, 2, false); }

static void apply_orientation(void) {
    /* Segment remap and COM scan direction together give the 180 degree flip,
     * for free, inside the panel - no framebuffer transformation needed. */
    cmd(s_flipped ? 0xa0 : 0xa1);
    cmd(s_flipped ? 0xc0 : 0xc8);
}

/* --- public ------------------------------------------------------------- */

void hp_disp_init(void) {
    gpio_init(HP_DISP_CS);  gpio_set_dir(HP_DISP_CS, GPIO_OUT);  gpio_put(HP_DISP_CS, 1);
    gpio_init(HP_DISP_DC);  gpio_set_dir(HP_DISP_DC, GPIO_OUT);  gpio_put(HP_DISP_DC, 0);
    gpio_init(HP_DISP_RST); gpio_set_dir(HP_DISP_RST, GPIO_OUT); gpio_put(HP_DISP_RST, 1);

    s_offset = pio_add_program(HP_PIO_DISP, &disp_spi_program);
    disp_spi_program_init(HP_PIO_DISP, HP_PIO_DISP_SM, s_offset,
                          HP_DISP_MOSI, HP_DISP_SCK, 8000000.0f);

    /* Reset pulse: the SSD1309 needs the line low for at least 3 us. */
    gpio_put(HP_DISP_RST, 0);
    sleep_ms(10);
    gpio_put(HP_DISP_RST, 1);
    sleep_ms(50);

    static const uint8_t init_seq[] = {
        0xfd, 0x12,        /* unlock the command interface        */
        0xae,              /* display off                         */
        0xd5, 0xa0,        /* clock divide / oscillator frequency */
        0xa8, 0x3f,        /* multiplex ratio = 64                */
        0xd3, 0x00,        /* display offset                      */
        0x40,              /* start line 0                        */
        0xda, 0x12,        /* COM pin layout: alternate           */
        0x81, 0xdf,        /* contrast                            */
        0xd9, 0x82,        /* pre-charge period                   */
        0xdb, 0x34,        /* VCOMH deselect level                */
        0xa4,              /* follow RAM, not all-on              */
        0xa6,              /* normal (not inverted)               */
        0x2e,              /* scrolling off                       */
        0x20, 0x00,        /* horizontal addressing: one DMA per frame */
        0x21, 0x00, 0x7f,  /* column range 0..127                 */
        0x22, 0x00, 0x07,  /* page range 0..7                     */
    };
    send_bytes(init_seq, sizeof init_seq, false);
    apply_orientation();
    cmd(0xaf);             /* display on */

    s_dma = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)s_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(HP_PIO_DISP, HP_PIO_DISP_SM, true));
    /* Byte writes land in the top byte of the FIFO word, which is where the
     * auto-pull takes its first bit from with MSB-first shifting. */
    dma_channel_configure((uint)s_dma, &c,
                          (uint8_t *)&HP_PIO_DISP->txf[HP_PIO_DISP_SM] + 3,
                          NULL, HP_FB_BYTES, false);

    HP_LOGI(TAG, "SSD1309 up: PIO SPI 8 MHz on GP%d/GP%d, DMA ch%d, flip=%d",
            HP_DISP_SCK, HP_DISP_MOSI, s_dma, (int)s_flipped);
}

/* Chip select is released here rather than in an interrupt: the frame is
 * already on the glass by the time this runs, and keeping it out of an ISR
 * keeps the whole display path free of shared-state hazards. */
void hp_disp_task(void) {
    if (s_dma < 0) return;
    if (!gpio_get(HP_DISP_CS) && !hp_disp_busy()) gpio_put(HP_DISP_CS, 1);
}

bool hp_disp_busy(void) {
    return dma_channel_is_busy((uint)s_dma) ||
           !pio_sm_is_tx_fifo_empty(HP_PIO_DISP, HP_PIO_DISP_SM);
}

bool hp_disp_flush(const uint8_t *fb) {
    if (hp_disp_busy()) return false;

    /* Rewind the window; horizontal addressing mode auto-advances through all
     * eight pages, so the whole frame is a single continuous transfer. */
    static const uint8_t window[] = { 0x21, 0x00, 0x7f, 0x22, 0x00, 0x07 };
    send_bytes(window, sizeof window, false);

    gpio_put(HP_DISP_DC, 1);
    gpio_put(HP_DISP_CS, 0);
    dma_channel_set_read_addr((uint)s_dma, fb, true);
    return true;
}

void hp_disp_set_contrast(uint8_t contrast) { cmd2(0x81, contrast); }

void hp_disp_power(bool on) { cmd(on ? 0xaf : 0xae); }

void hp_disp_set_flip(bool flipped) {
    if (flipped == s_flipped) return;
    s_flipped = flipped;
    apply_orientation();
}
