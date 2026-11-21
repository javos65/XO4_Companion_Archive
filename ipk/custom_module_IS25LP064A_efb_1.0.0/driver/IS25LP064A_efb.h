/* IS25LP064A_efb 1.0.0: BSP driver for an ISSI IS25LP064A (64 Mbit / 8 MB) serial flash on
 * the MachXO4 EFB SPI controller, accessed through the IS25LP064A_efb APB block.
 *
 * Hardware: the lscc_efb IP (APB, 32-bit) behind IS25LP064A_efb, SPI controller enabled,
 * flash CE# on CS0 (CSSPIN), and ldc_set_sysconfig {MASTER_SPI_PORT=EFB_USER} in the .pdc.
 * The flash WP# and HOLD# pins must be high (board pull-ups or a GPIO).
 *
 * Organisation: 256-byte pages, 4 KB sectors, 32/64 KB blocks, 2048 sectors.
 * In this driver a "block" of the read_block / write_block / erase_block functions is one
 * 4 KB sector, the smallest erasable unit; erase_32k / erase_64k / erase_chip cover the larger units.
 *
 * All functions are blocking and poll the controller; none uses interrupts.
 * Every function returns IS25LP064A_OK (0) or a negative IS25LP064A_ERR_* code.
 */
#ifndef IS25LP064A_EFB_H
#define IS25LP064A_EFB_H

#include <stdint.h>
#include "IS25LP064A_efb_regs.h"

/* ---- geometry ---- */
#define IS25LP064A_SIZE          0x800000u    /* 8 MB */
#define IS25LP064A_PAGE_SIZE     256u
#define IS25LP064A_BLOCK_SIZE    4096u        /* = sector, erase unit of erase_block() */
#define IS25LP064A_BLOCK_COUNT   (IS25LP064A_SIZE / IS25LP064A_BLOCK_SIZE)   /* 2048 */
#define IS25LP064A_BLOCK32_SIZE  0x8000u
#define IS25LP064A_BLOCK64_SIZE  0x10000u

/* ---- identification ---- */
#define IS25LP064A_MFR_ID        0x9Du        /* ISSI */
#define IS25LP064A_JEDEC_ID      0x9D6017u    /* manufacturer 9Dh, type 60h, capacity 17h */
#define IS25LP064A_UID_SIZE      16u          /* 128-bit unique ID */

/* ---- return codes ---- */
#define IS25LP064A_OK            0
#define IS25LP064A_ERR_SPI       (-1)   /* EFB SPI controller did not respond (see last_spisr) */
#define IS25LP064A_ERR_TIMEOUT   (-2)   /* flash stayed busy (WIP) beyond the poll limit */
#define IS25LP064A_ERR_ARG       (-3)   /* null pointer, address out of range or misaligned */
#define IS25LP064A_ERR_WEL       (-4)   /* write enable latch did not set after WREN */
#define IS25LP064A_ERR_ID        (-5)   /* JEDEC ID is not 9D 60 17 */
#define IS25LP064A_ERR_VERIFY    (-6)   /* read back differs from the data written */
#define IS25LP064A_ERR_PROTECTED (-7)   /* block protection covers the address (BP3-0) */

/* Default busy-poll limits (status reads; one read takes a few tens of microseconds) */
#define IS25LP064A_POLL_LIMIT_DEFAULT  2000000u
#define IS25LP064A_POLL_LIMIT_CHIP     60000000u
#define IS25LP064A_CTRL_POLL_LIMIT     100000u   /* per SPI byte, guards a hung controller */

struct is25lp064a_efb_instance {
    uint32_t base_address;      /* IS25LP064A_efb APB base address */
    uint32_t spibr;             /* SPIBR divider in use */
    uint32_t poll_limit;        /* busy-poll limit for program and erase */
    uint32_t last_polls;        /* status reads the last wait for WIP needed */
    uint8_t  last_spisr;        /* SPISR when the last ERR_SPI occurred */
    uint8_t  last_sr;           /* flash status register read last */
};

/* ---- set-up ---- */
/* Binds the instance to the block base address and programs the EFB SPI controller:
 * controller mode 0, MSB first, CS0, default SCK = PCLK / 8. Does not access the flash.
 * Returns 0, or 1 for a null pointer (the Propel driver convention). */
unsigned char is25lp064a_efb_init(struct is25lp064a_efb_instance *inst, uint32_t base_addr);
/* Sets SCK = pclk_hz / (divider + 1) with the smallest divider (>= 5) that keeps SCK <= sck_hz.
 * NORD (read) is specified up to 50 MHz, so any reachable rate is valid. Returns the SCK in Hz. */
uint32_t is25lp064a_set_sck(struct is25lp064a_efb_instance *inst, uint32_t pclk_hz, uint32_t sck_hz);
/* Reads the JEDEC ID and checks it is an IS25LP064A (9D 60 17). */
int is25lp064a_probe(struct is25lp064a_efb_instance *inst);

/* ---- identification and registers ---- */
int is25lp064a_read_jedec_id(struct is25lp064a_efb_instance *inst, uint8_t id[3]);
int is25lp064a_read_unique_id(struct is25lp064a_efb_instance *inst, uint8_t uid[IS25LP064A_UID_SIZE]);
int is25lp064a_read_status(struct is25lp064a_efb_instance *inst, uint8_t *sr);
int is25lp064a_read_function(struct is25lp064a_efb_instance *inst, uint8_t *fr);
/* Writes the status register (WREN + WRSR, then waits). Only BP3-0, QE and SRWD are writable. */
int is25lp064a_write_status(struct is25lp064a_efb_instance *inst, uint8_t sr);
int is25lp064a_write_enable(struct is25lp064a_efb_instance *inst);
int is25lp064a_write_disable(struct is25lp064a_efb_instance *inst);
/* Polls the status register until WIP is clear; inst->last_polls gets the number of reads. */
int is25lp064a_wait_ready(struct is25lp064a_efb_instance *inst, uint32_t poll_limit);
/* Returns 1 while a program/erase is in progress, 0 when idle, < 0 on error. */
int is25lp064a_is_busy(struct is25lp064a_efb_instance *inst);

/* ---- protection ---- */
/* Sets BP3-0 (0 = nothing protected, 15 = all), keeping QE and SRWD. Non-volatile. */
int is25lp064a_set_protection(struct is25lp064a_efb_instance *inst, uint8_t bp);
/* Returns 1 when addr lies in the area protected by the current BP3-0 and TBS, 0 when not. */
int is25lp064a_is_protected(struct is25lp064a_efb_instance *inst, uint32_t addr);

/* ---- read ---- */
/* Normal read (03h) of len bytes from addr (any alignment, may run to the end of the array). */
int is25lp064a_read(struct is25lp064a_efb_instance *inst, uint32_t addr, uint8_t *buf, uint32_t len);
/* Fast read (0Bh, 8 dummy cycles). */
int is25lp064a_fast_read(struct is25lp064a_efb_instance *inst, uint32_t addr, uint8_t *buf, uint32_t len);
/* Reads 4 KB block (sector) number block (0..2047) into buf[IS25LP064A_BLOCK_SIZE]. */
int is25lp064a_read_block(struct is25lp064a_efb_instance *inst, uint32_t block, uint8_t *buf);
/* Returns IS25LP064A_OK when len bytes from addr are all FFh, IS25LP064A_ERR_VERIFY when not;
 * *first_bad (may be NULL) gets the address of the first byte that is not FFh. */
int is25lp064a_blank_check(struct is25lp064a_efb_instance *inst, uint32_t addr, uint32_t len,
                           uint32_t *first_bad);

/* ---- program ---- */
/* Page program (02h) inside one 256-byte page: (addr % 256) + len must not exceed 256. */
int is25lp064a_program_page(struct is25lp064a_efb_instance *inst, uint32_t addr,
                            const uint8_t *buf, uint32_t len);
/* Programs len bytes from addr, split at page boundaries. The area must be erased (FFh). */
int is25lp064a_program(struct is25lp064a_efb_instance *inst, uint32_t addr,
                       const uint8_t *buf, uint32_t len);
/* Erases 4 KB block (sector) number block, programs buf[IS25LP064A_BLOCK_SIZE] and verifies it. */
int is25lp064a_write_block(struct is25lp064a_efb_instance *inst, uint32_t block, const uint8_t *buf);

/* ---- erase ---- */
/* Erases 4 KB block (sector) number block (0..2047). */
int is25lp064a_erase_block(struct is25lp064a_efb_instance *inst, uint32_t block);
/* Erases the 4 KB sector at addr (addr must be 4 KB aligned). */
int is25lp064a_erase_sector(struct is25lp064a_efb_instance *inst, uint32_t addr);
/* Erases the 32 KB / 64 KB block at addr (addr aligned to the block size). */
int is25lp064a_erase_32k(struct is25lp064a_efb_instance *inst, uint32_t addr);
int is25lp064a_erase_64k(struct is25lp064a_efb_instance *inst, uint32_t addr);
/* Erases the whole array (refused with ERR_PROTECTED unless BP3-0 = 0). */
int is25lp064a_erase_chip(struct is25lp064a_efb_instance *inst);

/* ---- power and reset ---- */
int is25lp064a_software_reset(struct is25lp064a_efb_instance *inst);
int is25lp064a_deep_power_down(struct is25lp064a_efb_instance *inst);
int is25lp064a_release_power_down(struct is25lp064a_efb_instance *inst);

/* ---- raw access ---- */
/* One transaction with CE# held low: sends tx_len bytes, then clocks in rx_len bytes (FFh out).
 * For commands this driver does not wrap (SFDP, information rows, read parameters, ...). */
int is25lp064a_transfer(struct is25lp064a_efb_instance *inst, const uint8_t *tx, uint32_t tx_len,
                        uint8_t *rx, uint32_t rx_len);
/* Current SPISR of the EFB SPI controller (diagnostics). */
uint8_t is25lp064a_efb_spisr(struct is25lp064a_efb_instance *inst);
/* Text for a return code. */
const char *is25lp064a_strerror(int rc);

#endif /* IS25LP064A_EFB_H */
