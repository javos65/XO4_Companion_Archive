/* IS25LP064A_efb 1.0.0: BSP driver, see IS25LP064A_efb.h.
 *
 * EFB SPI transaction (proven on the MachXO4 Companion Board, and as the lscc_efb IP
 * testbench task spi_1write): every write to SPICR0/1/2, SPIBR or SPICSR resets the SPI
 * core, so these are written once at init; a transaction is
 *   SPICR2 <= MSTR|MCSH, wait TRDY, SPICSR <= CS0      (CE# low)
 *   per byte: wait TRDY, SPITXDR <= tx, wait RRDY, rx <= SPIRXDR
 *   wait TRDY, SPICR2 <= MSTR, wait not TIP            (CE# high)
 */
#include <stddef.h>
#include "IS25LP064A_efb.h"

/* ---------------------------------------------------------------- EFB register access */
static inline uint8_t efb_rd(const struct is25lp064a_efb_instance *inst, uint32_t off)
{
    return (uint8_t)(*(volatile uint32_t *)(inst->base_address + off) & 0xFFu);
}

static inline void efb_wr(const struct is25lp064a_efb_instance *inst, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(inst->base_address + off) = v & 0xFFu;
}

static int efb_wait(struct is25lp064a_efb_instance *inst, uint8_t mask, uint8_t want)
{
    uint8_t sr = 0;
    for (uint32_t n = 0; n < IS25LP064A_CTRL_POLL_LIMIT; n++) {
        sr = efb_rd(inst, IS25LP064A_EFB_SPISR);
        if ((sr & mask) == want)
            return IS25LP064A_OK;
    }
    inst->last_spisr = sr;
    return IS25LP064A_ERR_SPI;
}

static int spi_begin(struct is25lp064a_efb_instance *inst)
{
    efb_wr(inst, IS25LP064A_EFB_SPICR2, IS25LP064A_EFB_SPICR2_MSTR | IS25LP064A_EFB_SPICR2_MCSH);
    if (efb_wait(inst, IS25LP064A_EFB_SPISR_TRDY, IS25LP064A_EFB_SPISR_TRDY) != IS25LP064A_OK)
        return IS25LP064A_ERR_SPI;
    efb_wr(inst, IS25LP064A_EFB_SPICSR, IS25LP064A_EFB_SPICSR_CS0);
    return IS25LP064A_OK;
}

static int spi_xfer(struct is25lp064a_efb_instance *inst, uint8_t tx, uint8_t *rx)
{
    uint8_t v;
    if (efb_wait(inst, IS25LP064A_EFB_SPISR_TRDY, IS25LP064A_EFB_SPISR_TRDY) != IS25LP064A_OK)
        return IS25LP064A_ERR_SPI;
    efb_wr(inst, IS25LP064A_EFB_SPITXDR, tx);
    if (efb_wait(inst, IS25LP064A_EFB_SPISR_RRDY, IS25LP064A_EFB_SPISR_RRDY) != IS25LP064A_OK)
        return IS25LP064A_ERR_SPI;
    v = efb_rd(inst, IS25LP064A_EFB_SPIRXDR);
    if (rx)
        *rx = v;
    return IS25LP064A_OK;
}

/* Ends the transaction (CE# high); keeps the first error */
static int spi_end(struct is25lp064a_efb_instance *inst, int rc)
{
    int r = efb_wait(inst, IS25LP064A_EFB_SPISR_TRDY, IS25LP064A_EFB_SPISR_TRDY);
    efb_wr(inst, IS25LP064A_EFB_SPICR2, IS25LP064A_EFB_SPICR2_MSTR);
    if (efb_wait(inst, IS25LP064A_EFB_SPISR_TIP, 0) != IS25LP064A_OK)
        r = IS25LP064A_ERR_SPI;
    /* CE# high time before the next command (tCEH, at least 50 ns) */
    for (volatile uint32_t n = 0; n < 8u; n++)
        (void)efb_rd(inst, IS25LP064A_EFB_SPISR);
    return (rc != IS25LP064A_OK) ? rc : r;
}

/* Command byte plus optional 24-bit address, CE# left low */
static int spi_cmd_addr(struct is25lp064a_efb_instance *inst, uint8_t cmd, int with_addr, uint32_t addr)
{
    int rc = spi_begin(inst);
    if (rc == IS25LP064A_OK) rc = spi_xfer(inst, cmd, NULL);
    if (with_addr) {
        if (rc == IS25LP064A_OK) rc = spi_xfer(inst, (uint8_t)(addr >> 16), NULL);
        if (rc == IS25LP064A_OK) rc = spi_xfer(inst, (uint8_t)(addr >> 8), NULL);
        if (rc == IS25LP064A_OK) rc = spi_xfer(inst, (uint8_t)addr, NULL);
    }
    return rc;
}

static int cmd_only(struct is25lp064a_efb_instance *inst, uint8_t cmd)
{
    return spi_end(inst, spi_cmd_addr(inst, cmd, 0, 0));
}

static int read_reg(struct is25lp064a_efb_instance *inst, uint8_t cmd, uint8_t *val)
{
    int rc;
    if (!inst || !val) return IS25LP064A_ERR_ARG;
    rc = spi_cmd_addr(inst, cmd, 0, 0);
    if (rc == IS25LP064A_OK) rc = spi_xfer(inst, 0xFFu, val);
    return spi_end(inst, rc);
}

/* ---------------------------------------------------------------- set-up */
unsigned char is25lp064a_efb_init(struct is25lp064a_efb_instance *inst, uint32_t base_addr)
{
    if (NULL == inst)
        return 1;
    inst->base_address = base_addr;
    inst->spibr = IS25LP064A_EFB_SPIBR_DEFAULT;
    inst->poll_limit = IS25LP064A_POLL_LIMIT_DEFAULT;
    inst->last_polls = 0;
    inst->last_spisr = 0;
    inst->last_sr = 0;
    efb_wr(inst, IS25LP064A_EFB_SPICR2, IS25LP064A_EFB_SPICR2_MSTR);   /* controller, mode 0, MSB first */
    efb_wr(inst, IS25LP064A_EFB_SPIBR, inst->spibr);
    efb_wr(inst, IS25LP064A_EFB_SPICSR, IS25LP064A_EFB_SPICSR_CS0);
    efb_wr(inst, IS25LP064A_EFB_SPICR1, IS25LP064A_EFB_SPICR1_SPE);
    (void)efb_rd(inst, IS25LP064A_EFB_SPIRXDR);                         /* drop a stale byte */
    return 0;
}

uint32_t is25lp064a_set_sck(struct is25lp064a_efb_instance *inst, uint32_t pclk_hz, uint32_t sck_hz)
{
    uint32_t div = IS25LP064A_EFB_SPIBR_MIN;
    if (!inst || sck_hz == 0u) return 0;
    while (div < 255u && pclk_hz / (div + 1u) > sck_hz)
        div++;
    inst->spibr = div;
    efb_wr(inst, IS25LP064A_EFB_SPIBR, div);           /* resets the SPI core: restore the rest */
    efb_wr(inst, IS25LP064A_EFB_SPICSR, IS25LP064A_EFB_SPICSR_CS0);
    efb_wr(inst, IS25LP064A_EFB_SPICR1, IS25LP064A_EFB_SPICR1_SPE);
    return pclk_hz / (div + 1u);
}

int is25lp064a_probe(struct is25lp064a_efb_instance *inst)
{
    uint8_t id[3];
    int rc = is25lp064a_read_jedec_id(inst, id);
    if (rc != IS25LP064A_OK) return rc;
    if ((((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2]) != IS25LP064A_JEDEC_ID)
        return IS25LP064A_ERR_ID;
    return IS25LP064A_OK;
}

/* ---------------------------------------------------------------- identification, registers */
int is25lp064a_read_jedec_id(struct is25lp064a_efb_instance *inst, uint8_t id[3])
{
    int rc;
    if (!inst || !id) return IS25LP064A_ERR_ARG;
    rc = spi_cmd_addr(inst, IS25LP064A_CMD_RDJDID, 0, 0);
    for (uint32_t i = 0; i < 3u && rc == IS25LP064A_OK; i++)
        rc = spi_xfer(inst, 0xFFu, &id[i]);
    return spi_end(inst, rc);
}

int is25lp064a_read_unique_id(struct is25lp064a_efb_instance *inst, uint8_t uid[IS25LP064A_UID_SIZE])
{
    int rc;
    if (!inst || !uid) return IS25LP064A_ERR_ARG;
    rc = spi_cmd_addr(inst, IS25LP064A_CMD_RDUID, 1, 0);      /* A<8:4> = 0, start at byte 0 */
    if (rc == IS25LP064A_OK) rc = spi_xfer(inst, 0xFFu, NULL); /* dummy byte */
    for (uint32_t i = 0; i < IS25LP064A_UID_SIZE && rc == IS25LP064A_OK; i++)
        rc = spi_xfer(inst, 0xFFu, &uid[i]);
    return spi_end(inst, rc);
}

int is25lp064a_read_status(struct is25lp064a_efb_instance *inst, uint8_t *sr)
{
    int rc = read_reg(inst, IS25LP064A_CMD_RDSR, sr);
    if (rc == IS25LP064A_OK) inst->last_sr = *sr;
    return rc;
}

int is25lp064a_read_function(struct is25lp064a_efb_instance *inst, uint8_t *fr)
{
    return read_reg(inst, IS25LP064A_CMD_RDFR, fr);
}

int is25lp064a_is_busy(struct is25lp064a_efb_instance *inst)
{
    uint8_t sr;
    int rc = is25lp064a_read_status(inst, &sr);
    if (rc != IS25LP064A_OK) return rc;
    return (sr & IS25LP064A_SR_WIP) ? 1 : 0;
}

int is25lp064a_wait_ready(struct is25lp064a_efb_instance *inst, uint32_t poll_limit)
{
    uint8_t sr;
    if (!inst) return IS25LP064A_ERR_ARG;
    for (uint32_t n = 0; n < poll_limit; n++) {
        if (is25lp064a_read_status(inst, &sr) != IS25LP064A_OK) return IS25LP064A_ERR_SPI;
        if (!(sr & IS25LP064A_SR_WIP)) {
            inst->last_polls = n;
            return IS25LP064A_OK;
        }
    }
    inst->last_polls = poll_limit;
    return IS25LP064A_ERR_TIMEOUT;
}

int is25lp064a_write_enable(struct is25lp064a_efb_instance *inst)
{
    uint8_t sr;
    int rc;
    if (!inst) return IS25LP064A_ERR_ARG;
    if ((rc = cmd_only(inst, IS25LP064A_CMD_WREN)) != IS25LP064A_OK) return rc;
    if ((rc = is25lp064a_read_status(inst, &sr)) != IS25LP064A_OK) return rc;
    return (sr & IS25LP064A_SR_WEL) ? IS25LP064A_OK : IS25LP064A_ERR_WEL;
}

int is25lp064a_write_disable(struct is25lp064a_efb_instance *inst)
{
    if (!inst) return IS25LP064A_ERR_ARG;
    return cmd_only(inst, IS25LP064A_CMD_WRDI);
}

int is25lp064a_write_status(struct is25lp064a_efb_instance *inst, uint8_t sr)
{
    int rc;
    if (!inst) return IS25LP064A_ERR_ARG;
    if ((rc = is25lp064a_wait_ready(inst, inst->poll_limit)) != IS25LP064A_OK) return rc;
    if ((rc = is25lp064a_write_enable(inst)) != IS25LP064A_OK) return rc;
    rc = spi_cmd_addr(inst, IS25LP064A_CMD_WRSR, 0, 0);
    if (rc == IS25LP064A_OK) rc = spi_xfer(inst, sr, NULL);
    if ((rc = spi_end(inst, rc)) != IS25LP064A_OK) return rc;
    return is25lp064a_wait_ready(inst, inst->poll_limit);
}

/* ---------------------------------------------------------------- protection */
int is25lp064a_set_protection(struct is25lp064a_efb_instance *inst, uint8_t bp)
{
    uint8_t sr;
    int rc;
    if (!inst || bp > 15u) return IS25LP064A_ERR_ARG;
    if ((rc = is25lp064a_read_status(inst, &sr)) != IS25LP064A_OK) return rc;
    sr = (uint8_t)((sr & (IS25LP064A_SR_QE | IS25LP064A_SR_SRWD)) | (bp << IS25LP064A_SR_BP_SHIFT));
    return is25lp064a_write_status(inst, sr);
}

int is25lp064a_is_protected(struct is25lp064a_efb_instance *inst, uint32_t addr)
{
    /* Table 6.4: BP 0..7 protect 0, 1, 2, 4, 8, 16, 32, 64 of the 128 64-KB blocks,
       BP 8..15 all; at the top of the array when TBS = 0, at the bottom when TBS = 1. */
    uint8_t sr, fr;
    uint32_t bp, len;
    int rc;
    if (!inst || addr >= IS25LP064A_SIZE) return IS25LP064A_ERR_ARG;
    if ((rc = is25lp064a_read_status(inst, &sr)) != IS25LP064A_OK) return rc;
    if ((rc = is25lp064a_read_function(inst, &fr)) != IS25LP064A_OK) return rc;
    bp = (sr & IS25LP064A_SR_BP_MASK) >> IS25LP064A_SR_BP_SHIFT;
    if (bp == 0u) return 0;
    len = (bp >= 8u) ? IS25LP064A_SIZE : (IS25LP064A_BLOCK64_SIZE << (bp - 1u));
    if (fr & IS25LP064A_FR_TBS)
        return (addr < len) ? 1 : 0;
    return (addr >= IS25LP064A_SIZE - len) ? 1 : 0;
}

/* ---------------------------------------------------------------- read */
static int read_cmd(struct is25lp064a_efb_instance *inst, uint8_t cmd, uint32_t addr,
                    uint8_t *buf, uint32_t len)
{
    int rc;
    if (!inst || !buf || addr >= IS25LP064A_SIZE || len > IS25LP064A_SIZE - addr)
        return IS25LP064A_ERR_ARG;
    if ((rc = is25lp064a_wait_ready(inst, inst->poll_limit)) != IS25LP064A_OK) return rc;
    rc = spi_cmd_addr(inst, cmd, 1, addr);
    if (cmd == IS25LP064A_CMD_FRD && rc == IS25LP064A_OK)
        rc = spi_xfer(inst, 0xFFu, NULL);                      /* 8 dummy cycles */
    for (uint32_t i = 0; i < len && rc == IS25LP064A_OK; i++)
        rc = spi_xfer(inst, 0xFFu, &buf[i]);
    return spi_end(inst, rc);
}

int is25lp064a_read(struct is25lp064a_efb_instance *inst, uint32_t addr, uint8_t *buf, uint32_t len)
{
    return read_cmd(inst, IS25LP064A_CMD_NORD, addr, buf, len);
}

int is25lp064a_fast_read(struct is25lp064a_efb_instance *inst, uint32_t addr, uint8_t *buf, uint32_t len)
{
    return read_cmd(inst, IS25LP064A_CMD_FRD, addr, buf, len);
}

int is25lp064a_read_block(struct is25lp064a_efb_instance *inst, uint32_t block, uint8_t *buf)
{
    if (block >= IS25LP064A_BLOCK_COUNT) return IS25LP064A_ERR_ARG;
    return is25lp064a_read(inst, block * IS25LP064A_BLOCK_SIZE, buf, IS25LP064A_BLOCK_SIZE);
}

int is25lp064a_blank_check(struct is25lp064a_efb_instance *inst, uint32_t addr, uint32_t len,
                           uint32_t *first_bad)
{
    uint8_t b = 0xFFu;
    int rc;
    if (!inst || addr >= IS25LP064A_SIZE || len > IS25LP064A_SIZE - addr) return IS25LP064A_ERR_ARG;
    if ((rc = is25lp064a_wait_ready(inst, inst->poll_limit)) != IS25LP064A_OK) return rc;
    rc = spi_cmd_addr(inst, IS25LP064A_CMD_NORD, 1, addr);
    for (uint32_t i = 0; i < len && rc == IS25LP064A_OK; i++) {
        rc = spi_xfer(inst, 0xFFu, &b);
        if (rc == IS25LP064A_OK && b != 0xFFu) {
            if (first_bad) *first_bad = addr + i;
            rc = IS25LP064A_ERR_VERIFY;
        }
    }
    return spi_end(inst, rc);
}

/* ---------------------------------------------------------------- program */
int is25lp064a_program_page(struct is25lp064a_efb_instance *inst, uint32_t addr,
                            const uint8_t *buf, uint32_t len)
{
    int rc;
    if (!inst || !buf || len == 0u || addr >= IS25LP064A_SIZE) return IS25LP064A_ERR_ARG;
    if ((addr % IS25LP064A_PAGE_SIZE) + len > IS25LP064A_PAGE_SIZE) return IS25LP064A_ERR_ARG;
    if ((rc = is25lp064a_wait_ready(inst, inst->poll_limit)) != IS25LP064A_OK) return rc;
    if ((rc = is25lp064a_write_enable(inst)) != IS25LP064A_OK) return rc;
    rc = spi_cmd_addr(inst, IS25LP064A_CMD_PP, 1, addr);
    for (uint32_t i = 0; i < len && rc == IS25LP064A_OK; i++)
        rc = spi_xfer(inst, buf[i], NULL);
    if ((rc = spi_end(inst, rc)) != IS25LP064A_OK) return rc;
    return is25lp064a_wait_ready(inst, inst->poll_limit);
}

int is25lp064a_program(struct is25lp064a_efb_instance *inst, uint32_t addr,
                       const uint8_t *buf, uint32_t len)
{
    if (!inst || !buf || addr >= IS25LP064A_SIZE || len > IS25LP064A_SIZE - addr)
        return IS25LP064A_ERR_ARG;
    while (len > 0u) {
        uint32_t n = IS25LP064A_PAGE_SIZE - (addr % IS25LP064A_PAGE_SIZE);
        int rc;
        if (n > len) n = len;
        if ((rc = is25lp064a_program_page(inst, addr, buf, n)) != IS25LP064A_OK) return rc;
        addr += n;
        buf += n;
        len -= n;
    }
    return IS25LP064A_OK;
}

int is25lp064a_write_block(struct is25lp064a_efb_instance *inst, uint32_t block, const uint8_t *buf)
{
    uint32_t addr = block * IS25LP064A_BLOCK_SIZE;
    uint8_t chunk[IS25LP064A_PAGE_SIZE];
    int rc;
    if (!inst || !buf || block >= IS25LP064A_BLOCK_COUNT) return IS25LP064A_ERR_ARG;
    if ((rc = is25lp064a_erase_block(inst, block)) != IS25LP064A_OK) return rc;
    if ((rc = is25lp064a_program(inst, addr, buf, IS25LP064A_BLOCK_SIZE)) != IS25LP064A_OK) return rc;
    for (uint32_t off = 0; off < IS25LP064A_BLOCK_SIZE; off += IS25LP064A_PAGE_SIZE) {
        if ((rc = is25lp064a_read(inst, addr + off, chunk, IS25LP064A_PAGE_SIZE)) != IS25LP064A_OK)
            return rc;
        for (uint32_t i = 0; i < IS25LP064A_PAGE_SIZE; i++)
            if (chunk[i] != buf[off + i]) return IS25LP064A_ERR_VERIFY;
    }
    return IS25LP064A_OK;
}

/* ---------------------------------------------------------------- erase */
static int erase_cmd(struct is25lp064a_efb_instance *inst, uint8_t cmd, uint32_t addr, uint32_t size)
{
    int rc;
    if (!inst || addr >= IS25LP064A_SIZE || (addr % size) != 0u) return IS25LP064A_ERR_ARG;
    rc = is25lp064a_is_protected(inst, addr);
    if (rc < 0) return rc;
    if (rc == 1) return IS25LP064A_ERR_PROTECTED;
    if ((rc = is25lp064a_wait_ready(inst, inst->poll_limit)) != IS25LP064A_OK) return rc;
    if ((rc = is25lp064a_write_enable(inst)) != IS25LP064A_OK) return rc;
    if ((rc = spi_end(inst, spi_cmd_addr(inst, cmd, 1, addr))) != IS25LP064A_OK) return rc;
    return is25lp064a_wait_ready(inst, inst->poll_limit);
}

int is25lp064a_erase_sector(struct is25lp064a_efb_instance *inst, uint32_t addr)
{
    return erase_cmd(inst, IS25LP064A_CMD_SER, addr, IS25LP064A_BLOCK_SIZE);
}

int is25lp064a_erase_block(struct is25lp064a_efb_instance *inst, uint32_t block)
{
    if (block >= IS25LP064A_BLOCK_COUNT) return IS25LP064A_ERR_ARG;
    return is25lp064a_erase_sector(inst, block * IS25LP064A_BLOCK_SIZE);
}

int is25lp064a_erase_32k(struct is25lp064a_efb_instance *inst, uint32_t addr)
{
    return erase_cmd(inst, IS25LP064A_CMD_BER32, addr, IS25LP064A_BLOCK32_SIZE);
}

int is25lp064a_erase_64k(struct is25lp064a_efb_instance *inst, uint32_t addr)
{
    return erase_cmd(inst, IS25LP064A_CMD_BER64, addr, IS25LP064A_BLOCK64_SIZE);
}

int is25lp064a_erase_chip(struct is25lp064a_efb_instance *inst)
{
    uint8_t sr;
    int rc;
    if (!inst) return IS25LP064A_ERR_ARG;
    if ((rc = is25lp064a_read_status(inst, &sr)) != IS25LP064A_OK) return rc;
    if (sr & IS25LP064A_SR_BP_MASK) return IS25LP064A_ERR_PROTECTED;  /* CER is ignored then */
    if ((rc = is25lp064a_wait_ready(inst, inst->poll_limit)) != IS25LP064A_OK) return rc;
    if ((rc = is25lp064a_write_enable(inst)) != IS25LP064A_OK) return rc;
    if ((rc = cmd_only(inst, IS25LP064A_CMD_CER)) != IS25LP064A_OK) return rc;
    return is25lp064a_wait_ready(inst, IS25LP064A_POLL_LIMIT_CHIP);
}

/* ---------------------------------------------------------------- power and reset */
static void short_delay(struct is25lp064a_efb_instance *inst, uint32_t reads)
{
    for (volatile uint32_t n = 0; n < reads; n++)
        (void)efb_rd(inst, IS25LP064A_EFB_SPISR);
}

int is25lp064a_software_reset(struct is25lp064a_efb_instance *inst)
{
    int rc;
    if (!inst) return IS25LP064A_ERR_ARG;
    if ((rc = cmd_only(inst, IS25LP064A_CMD_RSTEN)) != IS25LP064A_OK) return rc;
    if ((rc = cmd_only(inst, IS25LP064A_CMD_RST)) != IS25LP064A_OK) return rc;
    short_delay(inst, 2000u);       /* reset recovery, generous at any PCLK used with the EFB */
    return IS25LP064A_OK;
}

int is25lp064a_deep_power_down(struct is25lp064a_efb_instance *inst)
{
    if (!inst) return IS25LP064A_ERR_ARG;
    return cmd_only(inst, IS25LP064A_CMD_DP);
}

int is25lp064a_release_power_down(struct is25lp064a_efb_instance *inst)
{
    int rc;
    if (!inst) return IS25LP064A_ERR_ARG;
    rc = cmd_only(inst, IS25LP064A_CMD_RDPD);
    short_delay(inst, 200u);        /* tRES1 before the next command */
    return rc;
}

/* ---------------------------------------------------------------- raw access */
int is25lp064a_transfer(struct is25lp064a_efb_instance *inst, const uint8_t *tx, uint32_t tx_len,
                        uint8_t *rx, uint32_t rx_len)
{
    int rc;
    if (!inst || (tx_len && !tx) || (rx_len && !rx) || tx_len == 0u) return IS25LP064A_ERR_ARG;
    rc = spi_begin(inst);
    for (uint32_t i = 0; i < tx_len && rc == IS25LP064A_OK; i++)
        rc = spi_xfer(inst, tx[i], NULL);
    for (uint32_t i = 0; i < rx_len && rc == IS25LP064A_OK; i++)
        rc = spi_xfer(inst, 0xFFu, &rx[i]);
    return spi_end(inst, rc);
}

uint8_t is25lp064a_efb_spisr(struct is25lp064a_efb_instance *inst)
{
    return inst ? efb_rd(inst, IS25LP064A_EFB_SPISR) : 0u;
}

const char *is25lp064a_strerror(int rc)
{
    switch (rc) {
    case IS25LP064A_OK:            return "OK";
    case IS25LP064A_ERR_SPI:       return "EFB SPI controller did not respond";
    case IS25LP064A_ERR_TIMEOUT:   return "flash stayed busy (WIP)";
    case IS25LP064A_ERR_ARG:       return "bad argument";
    case IS25LP064A_ERR_WEL:       return "write enable latch not set after WREN";
    case IS25LP064A_ERR_ID:        return "JEDEC ID is not 9D 60 17";
    case IS25LP064A_ERR_VERIFY:    return "read back differs";
    case IS25LP064A_ERR_PROTECTED: return "address is block protected (BP3-0)";
    default:                       return "unknown error";
    }
}
