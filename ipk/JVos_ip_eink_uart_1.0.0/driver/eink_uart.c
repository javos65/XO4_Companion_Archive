/* eink_uart - BSP driver of the APB high speed UART with 16 word FIFOs, see eink_uart.h.
 * Project and documentation: https://github.com/javos65/XO4_Companion_Archive */
#include "eink_uart.h"

static inline uint32_t rd(const struct eink_uart_instance *u, uint32_t off)
{
	return *(volatile uint32_t *)(u->base_address + off);
}

static inline void wr(const struct eink_uart_instance *u, uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(u->base_address + off) = v;
}

static void write_control(struct eink_uart_instance *u, uint32_t bits)
{
	wr(u, EINK_UART_REG_CONTROL, bits | (u->loopback ? EINK_UART_CTL_LOOPBACK : 0u));
}

uint8_t eink_uart_init(struct eink_uart_instance *u, uint32_t base_addr)
{
	if (u == 0) {
		return EINK_UART_ERR_NULL;
	}
	u->instance_name = "eink_uart";
	u->base_address = base_addr;
	u->loopback = 0;
	if (rd(u, EINK_UART_REG_ID) != EINK_UART_ID_VALUE) {
		return EINK_UART_ERR_ID;
	}
	write_control(u, EINK_UART_CTL_CLR_RX | EINK_UART_CTL_CLR_TX | EINK_UART_CTL_CLR_FLAGS);
	return EINK_UART_OK;
}

uint32_t eink_uart_set_baud(struct eink_uart_instance *u, uint32_t clk_hz, uint32_t baud)
{
	/* inc = baud * 2^20 / clk, rounded, in 32 bit arithmetic: (baud << 10) / (clk >> 10) loses less than 0.01 % */
	uint32_t div = clk_hz >> 10;
	uint32_t inc = ((baud << 10) + (div >> 1)) / div;

	if ((inc == 0u) || (inc > 0xFFFFu)) {
		return 0u;
	}
	wr(u, EINK_UART_REG_BAUD, inc);
	/* real baud rate = clk * inc / 2^20 (32 bit arithmetic, accurate to about 0.01 %) */
	return ((clk_hz >> 10) * inc) >> 10;
}

uint32_t eink_uart_status(struct eink_uart_instance *u)
{
	return rd(u, EINK_UART_REG_STATUS);
}

uint8_t eink_uart_tx_ready(struct eink_uart_instance *u)
{
	return (rd(u, EINK_UART_REG_STATUS) & EINK_UART_ST_TX_FULL) ? 0u : 1u;
}

void eink_uart_putc(struct eink_uart_instance *u, uint8_t c)
{
	while (rd(u, EINK_UART_REG_STATUS) & EINK_UART_ST_TX_FULL) {
	}
	wr(u, EINK_UART_REG_DATA, c);
}

uint8_t eink_uart_getc(struct eink_uart_instance *u, uint8_t *c)
{
	uint32_t d = rd(u, EINK_UART_REG_DATA);      /* pops the RX FIFO */

	if ((d & 0x100u) == 0u) {
		return 1u;                               /* empty */
	}
	*c = (uint8_t)d;
	return 0u;
}

uint32_t eink_uart_rx_count(struct eink_uart_instance *u)
{
	return EINK_UART_ST_RX_COUNT(rd(u, EINK_UART_REG_STATUS));
}

void eink_uart_flush_rx(struct eink_uart_instance *u)
{
	write_control(u, EINK_UART_CTL_CLR_RX);
}

void eink_uart_flush_tx(struct eink_uart_instance *u)
{
	write_control(u, EINK_UART_CTL_CLR_TX);
}

void eink_uart_clear_flags(struct eink_uart_instance *u)
{
	write_control(u, EINK_UART_CTL_CLR_FLAGS);
}

void eink_uart_set_loopback(struct eink_uart_instance *u, uint8_t on)
{
	u->loopback = on ? 1u : 0u;
	write_control(u, 0u);
}
