// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// All rights reserved. License: 2-clause BSD

#include <stdio.h>
#include <stdbool.h>
#ifdef __unix__
#include <poll.h>
#endif
#include "glue.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define BITS_PER_BYTE 9 /* 8N1 is 9 bits */
#define SPEED_RATIO (25.0/MHZ) /* VERA runs at 25 MHz */

static uint16_t bauddiv;
static uint8_t byte_in;
static float countdown_in;
static float countdown_out;

FILE *uart_in_file = NULL;
FILE *uart_out_file = NULL;

static bool
txbusy()
{
	return countdown_out > 0;
}

static bool
data_available()
{
#ifdef __EMSCRIPTEN__
	return EM_ASM_INT({
		if(uart_data_available)
			return uart_data_available();
		else
			return false;
	});
#endif
	if (countdown_in > 0) {
		return false;
	}
	if (!uart_in_file) {
		return false;
	}
	if (feof(uart_in_file)) {
		return false;
	}
#ifdef __unix__
	struct pollfd fds = { .fd = fileno(uart_in_file), .events = POLLIN };
	if (poll(&fds, 1, 0) != 1) {
		return false;
	}
#endif
	return true;
}

static void
cache_next_char()
{
	if (uart_in_file) {
		byte_in = fgetc(uart_in_file);
	}

#ifdef __EMSCRIPTEN__
	byte_in = EM_ASM_INT({
		return uart_getc();
	});
#endif
}

void
vera_uart_init()
{
	// baud = 25000000 / (bauddiv+1)
	bauddiv = 24; // 1 MHz
	countdown_out = 0;
	countdown_in = 0;

	cache_next_char();
}

void
vera_uart_step()
{
	if (countdown_out > 0) {
		countdown_out -= SPEED_RATIO;
		//printf("countdown_out: %f\n", countdown_out);
	}
	if (countdown_in > 0) {
		countdown_in -= SPEED_RATIO;
		//printf("countdown_in: %f\n", countdown_in);
		if (countdown_in <= 0) {
			cache_next_char();
		}
	}
}

uint8_t
vera_uart_read(uint8_t reg)
{
	switch (reg) {
		case 0: {
			countdown_in = bauddiv * BITS_PER_BYTE;
			//printf("UART read: $%02x\n", byte_in);
			return byte_in;
		}
		case 1: {
			return (txbusy() << 1) | data_available();
		}
		case 2:
			return bauddiv & 0xff;
		case 3:
			return bauddiv >> 8;
	}
	return 0;
}

void
vera_uart_write(uint8_t reg, uint8_t value)
{
	switch (reg) {
		case 0:
			if (txbusy()) {
				printf("UART WRITTEN WHILE BUSY!! $%02x\n", value);
			} else {
				//printf("UART write: $%02x\n", value);
				if (uart_out_file) {
					fputc(value, uart_out_file);
					fflush(uart_out_file);
				}
#ifdef __EMSCRIPTEN__
				EM_ASM({
					if(uart_putc) uart_putc($0);
				}, value);
#endif
				countdown_out = bauddiv * BITS_PER_BYTE;
			}
			break;
		case 1:
			break;
		case 2:
			bauddiv = (bauddiv & 0xff00) | value;
			break;
		case 3:
			bauddiv = (bauddiv & 0xff) | (value << 8);
			break;
	}
}
