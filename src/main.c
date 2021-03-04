#include <stdio.h>
#include <math.h>
#include "libmigic.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "fix16.h"
#include "bsp/board.h"
#include "tusb.h"


#define CAPTURE_CHANNEL 0
#define CAPTURE_DEPTH 32
#define LOG_DEPTH 2048


const uint32_t LED_PIN = 25;
const uint32_t fs = 10000;
const uint32_t BENCHMARK_PIN = 2;
static bool ping = true;

/* Ping Pong buffers */
uint16_t cap0[CAPTURE_DEPTH] = {0};
uint16_t cap1[CAPTURE_DEPTH] = {0};
// Processing buffer
fix16_t buffer[CAPTURE_DEPTH] = {0};


#ifndef NDEBUG
int32_t log0[LOG_DEPTH] = {0};
int32_t log1[LOG_DEPTH] = {0};
int32_t *read_logger = log0;
uint32_t log_index = 0;
static bool log_ping = true;
void core1_main();
#endif


//--------------------------------------------------------------------+
// MIDI Stack callback (not neede right now)
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
}

void tud_suspend_cb(bool remote_wakeup_en)
{
}

void tud_resume_cb(void)
{
}


int main()
{
    int midiBufferSize = 10;
    midi_message_t midiMsgs[midiBufferSize];
    int numberOfMessages;
    uint16_t *dma_buffer = cap1;

    stdio_init_all();
    board_init();
    tusb_init();

#ifndef NDEBUG
    int32_t *write_logger = log1;
#endif

    libmigic_init(fs);

    /*
     * GPIO Setup
     */
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_init(BENCHMARK_PIN);
    gpio_set_dir(BENCHMARK_PIN, GPIO_OUT);
    gpio_put(BENCHMARK_PIN, 0);

    /*
     * ADC Setup
     */
    adc_gpio_init(26 + CAPTURE_CHANNEL);
    adc_gpio_init(26 + 1);
    adc_init();

    adc_select_input(CAPTURE_CHANNEL);
    adc_fifo_setup(
        true,    // Write each completed conversion to the sample FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // DREQ (and IRQ) asserted when at least 1 sample present
        false,   // We won't see the ERR bit because of 8 bit reads; disable.
        false    // No need to shift since we want 16 bits
    );
    adc_set_clkdiv(4800); /* Scale down to 10khz */

    sleep_ms(10);

    /*
     * DMA Setup
     */
    // Set up the DMA to start transferring data as soon as it appears in FIFO
    uint dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);

    // Reading from constant address, writing to incrementing byte addresses
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);

    // Pace transfers based on availability of ADC samples
    channel_config_set_dreq(&cfg, DREQ_ADC);

    dma_channel_configure(dma_chan, &cfg,
        cap0,    // dst
        &adc_hw->fifo,  // src
        CAPTURE_DEPTH,  // transfer count
        true            // start immediately
    );

    adc_run(true);

#ifndef NDEBUG
    multicore_launch_core1(core1_main);
#endif

    for (;;)
    {
        /* Measure the idle time */
#ifdef NDEBUG
        tud_task(); // tinyusb device task (Might be moved to the other core)
#endif
        dma_channel_wait_for_finish_blocking(dma_chan);
        if (ping)
        {
            dma_channel_configure(
                dma_chan,
                &cfg,
                cap1,           // dst
                &adc_hw->fifo,  // src
                CAPTURE_DEPTH,  // transfer count
                true            // start immediately
            );
            dma_buffer = cap0;
        }
        else
        {
            dma_channel_configure(
                dma_chan,
                &cfg,
                cap0,           // dst
                &adc_hw->fifo,  // src
                CAPTURE_DEPTH,  // transfer count
                true            // start immediately
            );
            dma_buffer = cap1;
        }
        ping = !ping;

        for (size_t i = 0; i < CAPTURE_DEPTH; i++)
        {
            /* Convert the data to Q16 */
            buffer[i] = ((int32_t)(dma_buffer[i])-(2048-60)) << 5; // 60 for resistor variance..

#ifndef NDEBUG
            write_logger[log_index+i] = buffer[i];
#endif
        }
#ifdef NDEBUG
        /* Call libmigic withe the processing buffer */
        libmigic_track(buffer, CAPTURE_DEPTH, &midiMsgs[0], midiBufferSize, &numberOfMessages, F16(0.1), F16(0.11), false, false, false);
        for (size_t i = 0; i < numberOfMessages; i++)
        {
            tudi_midi_write24(0, midiMsgs[i].bytes[0], midiMsgs[i].bytes[1], midiMsgs[i].bytes[2]);
            if (midiMsgs[i].bytes[0] == 0x90)
            {
                gpio_put(LED_PIN, 1);
                gpio_put(BENCHMARK_PIN, 1);
            }
            else if (midiMsgs[i].bytes[0] == 0x80)
            {
                gpio_put(LED_PIN, 0);
                gpio_put(BENCHMARK_PIN, 0);
            }
        }
#endif

#ifndef NDEBUG
        log_index += CAPTURE_DEPTH;
        if (log_index == LOG_DEPTH)
        {
            if (log_ping)
            {
                write_logger = log0;
                read_logger = log1;
            }
            else
            {
                write_logger = log1;
                read_logger = log0;
            }
            log_ping = !log_ping;
            log_index = 0;
        }
#endif
    }
    return 0;
}

#ifndef NDEBUG

/* Core 1 currently only used for dumping data in the
 * debug build */
void core1_main()
{
    static int32_t *last_read_logger = log1;
    while(true)
    {
        if (last_read_logger != read_logger)
        {
            last_read_logger = read_logger;
            for (size_t i = 0; i < LOG_DEPTH; i++)
            {
                printf("%d\n", read_logger[i]);
            }
        }
    }
}
#endif
