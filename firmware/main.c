/**
 * @file main.c
 * @author Arnošt Trtúšek (arnytrty@seznam.cz)
 * @brief logic analyzer with streaming capability
 * @version 0.7
 * @date 2022-01-06
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/structs/bus_ctrl.h"

// onboard leds & button
#define HW_LED1 16
#define HW_LED2 17
#define HW_LED3 18
#define HW_SW1 19

// hardware preferences
#define BUFFER_WORDS 16
#define USE_PIO pio0
#define USE_SM 0
#define USE_DMA 0

// buffer & buffer statuses
uint32_t buffer[BUFFER_WORDS * 2];
bool lowerHalf, halfDone;

// pio status & current program with offset
bool running = false;
struct pio_program sample_program;
uint offset;

//uint32_t time;

/**
 * @brief dma finished interrupt
 *
 */
void sampler_dma_handler() {
	// start writing to other half of buffer
	dma_channel_set_write_addr(USE_DMA, lowerHalf ? buffer + (BUFFER_WORDS * sizeof(uint32_t)) : buffer, true);
	lowerHalf = !lowerHalf;

	//uint32_t time2 = time_us_32();
	//printf("\n%i\n", time2 - time);
	//time = time2;

	// clear interrupt request
	dma_hw->ints0 = 1u << USE_DMA;
}

void sampler_init() {
	// bit overclock
	set_sys_clock_khz(200000, true);

	// configure dma
	dma_channel_config dma_config = dma_channel_get_default_config(USE_DMA);
	channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
	channel_config_set_read_increment(&dma_config, false);
	channel_config_set_write_increment(&dma_config, true);
	channel_config_set_dreq(&dma_config, pio_get_dreq(USE_PIO, USE_SM, false)); // transfer request signal from pio to fifo

	dma_channel_configure(USE_DMA, &dma_config, buffer, &USE_PIO->rxf[USE_SM], BUFFER_WORDS, false);

	// set dma high bus priority
	bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

	// dma interrupt
	dma_channel_set_irq0_enabled(USE_DMA, true);

	irq_set_exclusive_handler(DMA_IRQ_0, sampler_dma_handler);
	irq_set_enabled(DMA_IRQ_0, true);
}

/**
 * @brief start sampling to buffer
 *
 * @param pin_base starting pin
 * @param pin_count number from pins from that pin
 * @param div clock divider 1.f = 133MHz
 */
void sampler_run(uint pin_base, uint pin_count, float div) {
	if(running) return;
	running = true;

	gpio_put(HW_LED3, true);

	// create & compile PIO program
	uint16_t sampler_program_instructions = pio_encode_in(pin_base, pin_count);
	sample_program.instructions = &sampler_program_instructions;
	sample_program.length = 1;
	sample_program.origin = 1;
	offset = pio_add_program(USE_PIO, &sample_program);

	// configure state machine to loop over
	pio_sm_config sm_config = pio_get_default_sm_config();
	sm_config_set_in_pins(&sm_config, pin_base);
	sm_config_set_wrap(&sm_config, offset, offset); // loop to same position
	sm_config_set_clkdiv(&sm_config, div);
	sm_config_set_in_shift(&sm_config, true, true, 32 - (32 % pin_count)); // autopack sampled bits together starting at MSB
	sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_RX);

	// init PIO state machine
	pio_sm_init(USE_PIO, USE_SM, offset, &sm_config);

	// clear input fifo
	pio_sm_set_enabled(USE_PIO, USE_SM, false);
	pio_sm_clear_fifos(USE_PIO, USE_SM);
	pio_sm_restart(USE_PIO, USE_SM);

	// start sampling
	lowerHalf = halfDone = true;
	dma_channel_set_write_addr(USE_DMA, buffer, true);
	pio_sm_set_enabled(USE_PIO, USE_SM, true);
}

/**
 * @brief reset dma & pio
 *
 */
void sampler_stop() {
	if(!running) return;
	running = false;

	gpio_put(HW_LED3, false);

	// stop sampling
	dma_channel_abort(USE_DMA);
	pio_sm_set_enabled(USE_PIO, USE_SM, false);

	// remove program
	pio_remove_program(USE_PIO, &sample_program, offset);
}

/**
 * @brief main loop
 *
 * @return int should never return
 */
int main() {
	// init usb serial
	stdio_init_all();

	// delay & clear input
	sleep_ms(500);
	fflush(stdin);

	// init power led & set on
	gpio_init(HW_LED1);
	gpio_set_dir(HW_LED1, GPIO_OUT);
	gpio_put(HW_LED1, true);

	// init sampling led
	gpio_init(HW_LED3);
	gpio_set_dir(HW_LED3, GPIO_OUT);
	gpio_put(HW_LED3, false);

	// init sw1
	gpio_init(HW_SW1);
	gpio_set_dir(HW_SW1, GPIO_IN);
	gpio_pull_up(HW_SW1);

	// init secret led
	gpio_init(HW_LED2);
	gpio_set_dir(HW_LED2, GPIO_OUT);
	gpio_put(HW_LED2, !gpio_get(HW_SW1));

	// pull up 0-15 gpio
	for(int i = 0; i < 16; i++)
		gpio_pull_up(i);

	// init sampler
	sampler_init();

	// loop
	while (true) {
		switch(getchar_timeout_us(0)) {
			case PICO_ERROR_TIMEOUT:
				// no data (leave here to not go over other possibilities)
				break;

			case 'I':
				// discovery message
				printf("IAMIOTOOL");
				printf("%i", gpio_get(HW_LED2));
				break;

			case 'S':
				// stop
				sampler_stop();
				break;

			case 'A':
				// 1MHz 1ch
				sampler_run(0, 1, (float)clock_get_hz(clk_sys) / 1000000);
				break;

			case 'B':
				// 500kHz 2ch
				sampler_run(0, 2, (float)clock_get_hz(clk_sys) / 500000);
				break;

			case 'C':
				// 250kHz 4ch
				sampler_run(0, 4, (float)clock_get_hz(clk_sys) / 250000);
				break;

			case 'D':
				// 125kHz 8ch
				sampler_run(0, 8, (float)clock_get_hz(clk_sys) / 100000);
				break;

			case 'E':
				// 60kHz 16ch
				sampler_run(0, 16, (float)clock_get_hz(clk_sys) / 50000);
				break;

			case 'F':
				// 10kHz 16ch
				sampler_run(0, 16, (float)clock_get_hz(clk_sys) / 10000);
				break;

			case 'G':
				// 5kHz 16ch
				sampler_run(0, 16, (float)clock_get_hz(clk_sys) / 5000);
				break;

			case 'X':
				// 1MHz 4ch
				sampler_run(0, 4, (float)clock_get_hz(clk_sys) / 1000000);
				break;

			case 'Y':
				// 500kHz 8ch
				sampler_run(0, 8, (float)clock_get_hz(clk_sys) / 500000);
				break;

			case 'Z':
				// 250kHz 16ch
				sampler_run(0, 16, (float)clock_get_hz(clk_sys) / 250000);
				break;
		}

		// check available buffer
		if(lowerHalf != halfDone) {
			fwrite(halfDone ? buffer : buffer + (BUFFER_WORDS * sizeof(uint32_t)), sizeof(uint32_t), BUFFER_WORDS, stdout);

			halfDone = !halfDone;
		}

		tight_loop_contents();
	}
}
