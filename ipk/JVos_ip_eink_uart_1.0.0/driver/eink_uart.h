/* eink_uart - BSP driver of the APB high speed UART with 16 word FIFOs (IP JVos:ip:eink_uart, RTL apb_eink_uart.v).
 *
 * Project and documentation: https://github.com/javos65/XO4_Companion_Archive
 *
 * This BSP package "eink_uart" collects all driver files of the e-ink display link:
 *   eink_uart.h / eink_uart.c      the UART driver (this file)
 *   eink154.h / eink154.c          the display driver (screen structure, renderer, protocol)
 *   eink_font3x5.*, font_1bpp_5x7.*   the fonts used by the display driver
 *
 * The UART is a small polled block for fixed fast links: 8N1, 16 word TX FIFO, 16 word RX FIFO, baud rate from a
 * fractional tick generator, internal loopback for self test, no interrupts. The driver is deliberately tiny: it
 * works on an instance (the base address of the IP) that is passed to the functions, like the other BSP drivers
 * (i2c_master, fxls8971, eink154):
 *
 *   static struct eink_uart_instance link;
 *   eink_uart_init(&link, UART1_INST_BASE_ADDR);              // checks the ID register, flushes both FIFOs
 *   eink_uart_set_baud(&link, apb_clk_hz, 230400);            // returns the real baud rate
 *   eink_uart_putc(&link, 'a');                               // waits while the TX FIFO is full
 *   uint8_t c;
 *   if (eink_uart_getc(&link, &c) == 0) { ... }               // 0 = a character was read, 1 = RX FIFO empty
 *
 * FIFO behaviour: a full RX FIFO drops the NEW character and keeps the oldest 16 (sticky OVERRUN flag), so the
 * first characters after a flush are never lost, whatever the time between two reads. A character costs 43 us at
 * 230400 baud: the 16 word FIFO tolerates about 690 us between two reads.
 *
 * Registers (word addresses): 0x00 ID, 0x04 DATA, 0x08 STATUS, 0x0C CONTROL, 0x10 BAUD, see apb_eink_uart.v.
 */
#ifndef EINK_UART_H
#define EINK_UART_H

#include <stdint.h>

#define EINK_UART_ID_VALUE      0x55415446u   /* "UATF" */
#define EINK_UART_DEPTH         16u

/* register offsets */
#define EINK_UART_REG_ID        0x00u
#define EINK_UART_REG_DATA      0x04u
#define EINK_UART_REG_STATUS    0x08u
#define EINK_UART_REG_CONTROL   0x0Cu
#define EINK_UART_REG_BAUD      0x10u

/* STATUS bits */
#define EINK_UART_ST_RX_NOT_EMPTY  (1u << 0)
#define EINK_UART_ST_RX_FULL       (1u << 1)
#define EINK_UART_ST_RX_OVERRUN    (1u << 2)   /* sticky */
#define EINK_UART_ST_TX_IDLE       (1u << 3)   /* TX FIFO empty and shifter idle */
#define EINK_UART_ST_TX_FULL       (1u << 4)
#define EINK_UART_ST_TX_BUSY       (1u << 5)
#define EINK_UART_ST_FRAME_ERR     (1u << 6)   /* sticky */
#define EINK_UART_ST_RX_COUNT(s)   (((s) >> 8) & 0x1Fu)
#define EINK_UART_ST_TX_COUNT(s)   (((s) >> 16) & 0x1Fu)

/* CONTROL bits */
#define EINK_UART_CTL_CLR_RX       (1u << 0)
#define EINK_UART_CTL_CLR_TX       (1u << 1)
#define EINK_UART_CTL_CLR_FLAGS    (1u << 2)
#define EINK_UART_CTL_LOOPBACK     (1u << 3)

/* return codes */
#define EINK_UART_OK            0
#define EINK_UART_ERR_NULL      1
#define EINK_UART_ERR_ID        2    /* ID register does not match: wrong address or IP */

struct eink_uart_instance {
	const char *instance_name;
	uint32_t    base_address;
	uint8_t     loopback;        /* copy of the loopback bit of CONTROL */
};

/* Links the instance to the IP, checks the ID register and flushes both FIFOs. */
uint8_t eink_uart_init(struct eink_uart_instance *u, uint32_t base_addr);

/* Sets the baud rate from the APB clock: BAUD = round(baud * 16 * 65536 / clk). Returns the real baud rate. */
uint32_t eink_uart_set_baud(struct eink_uart_instance *u, uint32_t clk_hz, uint32_t baud);

/* Status register (EINK_UART_ST_*). */
uint32_t eink_uart_status(struct eink_uart_instance *u);

/* Sends a character; waits while the TX FIFO is full. */
void eink_uart_putc(struct eink_uart_instance *u, uint8_t c);

/* 1 when the TX FIFO has room for a character. */
uint8_t eink_uart_tx_ready(struct eink_uart_instance *u);

/* Reads a character without waiting: returns 0 and *c when one was read, 1 when the RX FIFO is empty. */
uint8_t eink_uart_getc(struct eink_uart_instance *u, uint8_t *c);

/* Number of characters in the RX FIFO (0..16). */
uint32_t eink_uart_rx_count(struct eink_uart_instance *u);

/* Empties the RX / TX FIFO. */
void eink_uart_flush_rx(struct eink_uart_instance *u);
void eink_uart_flush_tx(struct eink_uart_instance *u);

/* Clears the sticky OVERRUN and FRAME_ERR flags. */
void eink_uart_clear_flags(struct eink_uart_instance *u);

/* 1 connects TX to RX inside the IP (self test), 0 normal operation. */
void eink_uart_set_loopback(struct eink_uart_instance *u, uint8_t on);

#endif /* EINK_UART_H */
