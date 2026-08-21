#include "encoder.h"

#include "board.h"
#include "log.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "quadrature.pio.h"

#define TAG "enc"

#define TWO_PI_F 6.28318530717958647692f

/* The PIO state machine pushes its counter continuously; two DMA channels
 * chained to each other drain the FIFO into this word forever, so it always
 * holds the newest value without any CPU involvement.  A single channel
 * cannot chain to itself - the SDK treats that as "chaining disabled" - hence
 * the pair. */
static volatile int32_t s_count;
static int  s_dma_a = -1;
static int  s_dma_b = -1;
static bool s_invert;
static int32_t s_zero;

static void start_dma_pair(void) {
    s_dma_a = dma_claim_unused_channel(true);
    s_dma_b = dma_claim_unused_channel(true);

    dma_channel_config ca = dma_channel_get_default_config((uint)s_dma_a);
    channel_config_set_transfer_data_size(&ca, DMA_SIZE_32);
    channel_config_set_read_increment(&ca, false);
    channel_config_set_write_increment(&ca, false);
    channel_config_set_dreq(&ca, pio_get_dreq(HP_PIO_ENC, HP_PIO_ENC_SM, false));
    channel_config_set_chain_to(&ca, (uint)s_dma_b);

    dma_channel_config cb = ca;
    channel_config_set_chain_to(&cb, (uint)s_dma_a);

    dma_channel_configure((uint)s_dma_b, &cb, (void *)&s_count,
                          &HP_PIO_ENC->rxf[HP_PIO_ENC_SM], 0xffffffffu, false);
    dma_channel_configure((uint)s_dma_a, &ca, (void *)&s_count,
                          &HP_PIO_ENC->rxf[HP_PIO_ENC_SM], 0xffffffffu, true);
}

void hp_encoder_init(void) {
    /* The dispatch table must sit at address 0, so this program owns its PIO
     * instruction memory outright - see board.h for the allocation map. */
    uint offset = pio_add_program_at_offset(HP_PIO_ENC, &quadrature_program, 0);
    hard_assert(offset == 0);

    gpio_pull_up(HP_ENC_A);
    gpio_pull_up(HP_ENC_B);
    pio_gpio_init(HP_PIO_ENC, HP_ENC_A);
    pio_gpio_init(HP_PIO_ENC, HP_ENC_B);
    pio_sm_set_consecutive_pindirs(HP_PIO_ENC, HP_PIO_ENC_SM, HP_ENC_B, 2, false);

    pio_sm_config c = quadrature_program_get_default_config(0);
    sm_config_set_in_pins(&c, HP_ENC_B);            /* B = bit 0, A = bit 1 */
    sm_config_set_in_shift(&c, false, false, 32);   /* shift left, no autopush */
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    /* ~6 MHz gives roughly 700 k samples/s.  A brisk flick of the wheel is
     * about 80 k edges/s, so every edge is oversampled ~8x. */
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 6000000.0f);

    pio_sm_init(HP_PIO_ENC, HP_PIO_ENC_SM, 0, &c);
    pio_sm_exec(HP_PIO_ENC, HP_PIO_ENC_SM, pio_encode_set(pio_x, 0));
    pio_sm_exec(HP_PIO_ENC, HP_PIO_ENC_SM, pio_encode_set(pio_y, 0));
    pio_sm_set_enabled(HP_PIO_ENC, HP_PIO_ENC_SM, true);

    start_dma_pair();

    /* Y starts out not matching the pins, so the first dispatch may register
     * one bogus step.  Let it settle, then define here as zero. */
    sleep_ms(2);
    hp_encoder_zero();

    HP_LOGI(TAG, "PIO quadrature decoder up on GP%d/GP%d, %d counts/rev",
            HP_ENC_A, HP_ENC_B, HP_ENC_CPR);
}

int32_t hp_encoder_count(void) {
    int32_t raw = s_count - s_zero;
    return s_invert ? -raw : raw;
}

float hp_encoder_angle(void) {
    return (float)hp_encoder_count() * (TWO_PI_F / (float)HP_ENC_CPR);
}

void hp_encoder_set_invert(bool invert) { s_invert = invert; }
bool hp_encoder_invert(void)            { return s_invert; }
void hp_encoder_zero(void)              { s_zero = s_count; }
