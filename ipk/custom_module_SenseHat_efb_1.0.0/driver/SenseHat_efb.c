/* SenseHat_efb 1.0.0: BSP driver, see SenseHat_efb.h.
 *
 * EFB I2C controller sequence: exactly FPGA-IPUG-02287 Figure 7.9 (I2C Controller Read/Write
 * Example through WISHBONE), with CKSDIS = 0 (clock stretching on, Table 5.3 for controller use),
 * so the next command may come late; the end of every byte is taken from TIP (byte_done):
 *   write: TXDR <= addr+W, CMDR <= 0x90 (STA+WR), byte done;
 *          per byte TXDR <= data, CMDR <= 0x10 (WR), byte done;   end CMDR <= 0x40 (STO).
 *   read:  TXDR <= addr+R, CMDR <= 0x90, byte done, CMDR <= 0x20 (RD);
 *          per byte except the last: wait TRRDY, data <= RXDR;
 *          last byte: real-time wait, CMDR <= 0x68 (RD+NACK+STO), wait TRRDY, data <= RXDR.
 * Status rules (Table 5.7): SR bits other than BUSY are valid only while BUSY = 1; TIP can be
 * high after configuration before the first transfer. A NACK while transmitting sets TROE and RARC.
 * CR is written as 0x8C (I2CEN, SDA delay off): the SDA delay needs the internal oscillator,
 * which this design does not instantiate (FPGA-TN-02403 section 4.3.6).
 */
#include <stddef.h>
#include "SenseHat_efb.h"

#define CMD_START_WR  (SENSEHAT_EFB_CMDR_STA | SENSEHAT_EFB_CMDR_WR)                          /* 0x90 */
#define CMD_WR        (SENSEHAT_EFB_CMDR_WR)                                                  /* 0x10 */
#define CMD_RD        (SENSEHAT_EFB_CMDR_RD)                                                  /* 0x20 */
#define CMD_RD_LAST   (SENSEHAT_EFB_CMDR_RD | SENSEHAT_EFB_CMDR_ACK | SENSEHAT_EFB_CMDR_STO)  /* 0x68 */
#define CMD_STOP      (SENSEHAT_EFB_CMDR_STO)                                                 /* 0x40 */

/* ---------------------------------------------------------------- EFB register access */
static inline uint8_t efb_rd(const struct sensehat_efb_instance *inst, uint32_t off)
{
    return (uint8_t)(*(volatile uint32_t *)(inst->base_address + off) & 0xFFu);
}

static inline void efb_wr(const struct sensehat_efb_instance *inst, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(inst->base_address + off) = v & 0xFFu;
}

/* Delay in microseconds: the application hook when set, else a rough SR-read loop. */
static void delay_us(const struct sensehat_efb_instance *inst, uint32_t us)
{
    if (inst->delay_us != NULL) {
        inst->delay_us(us);
        return;
    }
    for (uint32_t n = 0; n < us * 4u; n++)
        (void)efb_rd(inst, SENSEHAT_EFB_I2C_SR);
}

/* SCL period in microseconds (10 at 100 kHz) */
static uint32_t tscl_us(const struct sensehat_efb_instance *inst)
{
    return (inst->i2c_hz != 0u) ? (1000000u + inst->i2c_hz - 1u) / inst->i2c_hz : 10u;
}

/* Waits until (SR & mask) == want; records SR in last_sr on timeout */
static int wait_sr(struct sensehat_efb_instance *inst, uint8_t mask, uint8_t want)
{
    uint8_t sr = 0;
    for (uint32_t n = 0; n < inst->poll_limit; n++) {
        sr = efb_rd(inst, SENSEHAT_EFB_I2C_SR);
        if ((sr & mask) == want)
            return SENSEHAT_OK;
    }
    inst->last_sr = sr;
    return SENSEHAT_ERR_TIMEOUT;
}

/* End of one byte (address or data, sent or received): wait until TIP has been seen high and is
 * low again, with BUSY = 1, and only then read RARC. RARC is not meaningful while TIP = 1 or
 * before the first transfer. TIP rises half an SCL period after START (Table 5.7), so "TIP low"
 * right after the command is not "byte done". */
static int byte_done(struct sensehat_efb_instance *inst)
{
    uint8_t sr = 0;
    int seen_tip = 0;
    for (uint32_t n = 0; n < inst->poll_limit; n++) {
        sr = efb_rd(inst, SENSEHAT_EFB_I2C_SR);
        if ((sr & SENSEHAT_EFB_SR_BUSY) == 0u)
            continue;                                   /* START not yet on the bus */
        if (sr & SENSEHAT_EFB_SR_ARBL) { inst->last_sr = sr; return SENSEHAT_ERR_ARBL; }
        if (sr & SENSEHAT_EFB_SR_TIP) { seen_tip = 1; continue; }
        if (seen_tip) {
            inst->last_sr = sr;
            return (sr & SENSEHAT_EFB_SR_RARC) ? SENSEHAT_ERR_NACK : SENSEHAT_OK;
        }
    }
    inst->last_sr = sr;
    return SENSEHAT_ERR_TIMEOUT;
}

/* STOP after an error; resets the core when the bus does not become free. Keeps last_sr. */
static void abort_transfer(struct sensehat_efb_instance *inst)
{
    uint8_t keep = inst->last_sr;
    efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, CMD_STOP);
    if (wait_sr(inst, SENSEHAT_EFB_SR_BUSY, 0) != SENSEHAT_OK) {
        efb_wr(inst, SENSEHAT_EFB_I2C_CR, SENSEHAT_EFB_CR_SDA_DEL_OFF);   /* a write to CR resets the core */
        efb_wr(inst, SENSEHAT_EFB_I2C_CR, SENSEHAT_EFB_CR_RUN);
    }
    efb_wr(inst, SENSEHAT_EFB_I2C_IRQ, 0x0Fu);
    inst->last_sr = keep;
}

/* ---------------------------------------------------------------- set-up */
unsigned char sensehat_efb_init(struct sensehat_efb_instance *inst, uint32_t base_addr)
{
    if (inst == NULL)
        return 1;
    inst->base_address = base_addr;
    inst->poll_limit   = SENSEHAT_POLL_LIMIT_DEFAULT;
    inst->wb_clk_hz    = 0;
    inst->i2c_hz       = 0;
    inst->last_sr      = 0;
    inst->last_addr    = 0;
    inst->delay_us     = NULL;
    return 0;
}

int sensehat_i2c_init(struct sensehat_efb_instance *inst, uint32_t wb_clk_hz, uint32_t i2c_hz)
{
    if (inst == NULL || i2c_hz == 0u || wb_clk_hz == 0u)
        return SENSEHAT_ERR_ARG;
    uint32_t div  = 4u * i2c_hz;
    uint32_t pres = (wb_clk_hz + div / 2u) / div;       /* rounded; 12 MHz, 100 kHz -> 30 */
    if (pres < 1u || pres > 1023u)
        return SENSEHAT_ERR_ARG;
    efb_wr(inst, SENSEHAT_EFB_I2C_CR, SENSEHAT_EFB_CR_SDA_DEL_OFF);  /* core off while the rate changes */
    efb_wr(inst, SENSEHAT_EFB_I2C_BR0, pres & 0xFFu);
    efb_wr(inst, SENSEHAT_EFB_I2C_BR1, (pres >> 8) & 0x03u);   /* a write to BR1 resets the core */
    efb_wr(inst, SENSEHAT_EFB_I2C_CR, SENSEHAT_EFB_CR_RUN);
    efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, 0u);  /* idle command, as in Figure 7.9 */
    efb_wr(inst, SENSEHAT_EFB_I2C_IRQ, 0x0Fu);
    inst->wb_clk_hz = wb_clk_hz;
    inst->i2c_hz    = wb_clk_hz / (4u * pres);
    return SENSEHAT_OK;
}

int sensehat_i2c_reset(struct sensehat_efb_instance *inst)
{
    if (inst == NULL)
        return SENSEHAT_ERR_ARG;
    efb_wr(inst, SENSEHAT_EFB_I2C_CR, SENSEHAT_EFB_CR_SDA_DEL_OFF);   /* a write to CR resets the core */
    efb_wr(inst, SENSEHAT_EFB_I2C_CR, SENSEHAT_EFB_CR_RUN);
    efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, 0u);
    efb_wr(inst, SENSEHAT_EFB_I2C_IRQ, 0x0Fu);
    return SENSEHAT_OK;
}

int sensehat_i2c_recover(struct sensehat_efb_instance *inst)
{
    if (inst == NULL)
        return SENSEHAT_ERR_ARG;
    abort_transfer(inst);
    (void)sensehat_i2c_reset(inst);
    return (efb_rd(inst, SENSEHAT_EFB_I2C_SR) & SENSEHAT_EFB_SR_BUSY) ? SENSEHAT_ERR_ARBL : SENSEHAT_OK;
}

/* ---------------------------------------------------------------- raw transfers */
/* START + address + W, then the prefix bytes and the data bytes; STOP when stop != 0. */
static int do_write(struct sensehat_efb_instance *inst, uint8_t addr,
                    const uint8_t *prefix, uint32_t plen,
                    const uint8_t *data, uint32_t len, int stop)
{
    int rc;
    efb_wr(inst, SENSEHAT_EFB_I2C_TXDR, ((uint32_t)addr << 1) | 0u);
    efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, CMD_START_WR);
    rc = byte_done(inst);
    for (uint32_t i = 0; rc == SENSEHAT_OK && i < plen; i++) {
        efb_wr(inst, SENSEHAT_EFB_I2C_TXDR, prefix[i]);
        efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, CMD_WR);
        rc = byte_done(inst);
    }
    for (uint32_t i = 0; rc == SENSEHAT_OK && i < len; i++) {
        efb_wr(inst, SENSEHAT_EFB_I2C_TXDR, data[i]);
        efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, CMD_WR);
        rc = byte_done(inst);
    }
    if (rc != SENSEHAT_OK) {
        abort_transfer(inst);
        return rc;
    }
    if (stop) {
        efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, CMD_STOP);
        if (wait_sr(inst, SENSEHAT_EFB_SR_BUSY, 0) != SENSEHAT_OK)
            return SENSEHAT_ERR_TIMEOUT;
    }
    return SENSEHAT_OK;
}

/* (Repeated) START + address + R, len bytes, NACK + STOP on the last (Figure 7.9 right half). */
static int do_read(struct sensehat_efb_instance *inst, uint8_t addr, uint8_t *data, uint32_t len)
{
    int rc;
    uint32_t t = tscl_us(inst);
    efb_wr(inst, SENSEHAT_EFB_I2C_TXDR, ((uint32_t)addr << 1) | 1u);
    efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, CMD_START_WR);
    rc = byte_done(inst);                               /* address + R acknowledged? */
    if (rc == SENSEHAT_OK && len > 1u) {
        efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, CMD_RD);
        for (uint32_t i = 0; rc == SENSEHAT_OK && i + 1u < len; i++) {
            rc = wait_sr(inst, SENSEHAT_EFB_SR_TRRDY, SENSEHAT_EFB_SR_TRRDY);
            if (rc == SENSEHAT_OK)
                data[i] = efb_rd(inst, SENSEHAT_EFB_I2C_RXDR);
        }
    }
    if (rc == SENSEHAT_OK) {
        if (len == 1u) {                                /* 1 byte: start it, then NACK + STOP 2..7 tSCL later */
            efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, CMD_RD);
            delay_us(inst, 3u * t);
        }
        efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, CMD_RD_LAST);
        rc = wait_sr(inst, SENSEHAT_EFB_SR_TRRDY, SENSEHAT_EFB_SR_TRRDY);
        if (rc == SENSEHAT_OK)
            data[len - 1u] = efb_rd(inst, SENSEHAT_EFB_I2C_RXDR);
    }
    if (rc != SENSEHAT_OK) {
        abort_transfer(inst);
        return rc;
    }
    if (wait_sr(inst, SENSEHAT_EFB_SR_BUSY, 0) != SENSEHAT_OK)
        return SENSEHAT_ERR_TIMEOUT;
    efb_wr(inst, SENSEHAT_EFB_I2C_CMDR, 0u);
    return SENSEHAT_OK;
}

static int begin(struct sensehat_efb_instance *inst, uint8_t addr)
{
    if (inst == NULL || addr > 0x7Fu)
        return SENSEHAT_ERR_ARG;
    inst->last_addr = addr;
    if (wait_sr(inst, SENSEHAT_EFB_SR_BUSY, 0) != SENSEHAT_OK)
        return SENSEHAT_ERR_ARBL;                        /* bus busy before the transfer */
    return SENSEHAT_OK;
}

int sensehat_i2c_write(struct sensehat_efb_instance *inst, uint8_t addr, const uint8_t *data, uint32_t len)
{
    int rc = begin(inst, addr);
    if (rc != SENSEHAT_OK)
        return rc;
    if (data == NULL && len != 0u)
        return SENSEHAT_ERR_ARG;
    return do_write(inst, addr, NULL, 0u, data, len, 1);
}

int sensehat_i2c_write_read(struct sensehat_efb_instance *inst, uint8_t addr,
                            const uint8_t *wdata, uint32_t wlen, uint8_t *rdata, uint32_t rlen)
{
    int rc = begin(inst, addr);
    if (rc != SENSEHAT_OK)
        return rc;
    if (rdata == NULL || rlen == 0u || (wdata == NULL && wlen != 0u))
        return SENSEHAT_ERR_ARG;
    rc = do_write(inst, addr, NULL, 0u, wdata, wlen, 0);   /* no STOP: repeated START follows */
    if (rc != SENSEHAT_OK)
        return rc;
    return do_read(inst, addr, rdata, rlen);
}

int sensehat_i2c_write_block(struct sensehat_efb_instance *inst, uint8_t addr, uint8_t reg,
                             const uint8_t *data, uint32_t len)
{
    int rc = begin(inst, addr);
    if (rc != SENSEHAT_OK)
        return rc;
    if (data == NULL && len != 0u)
        return SENSEHAT_ERR_ARG;
    return do_write(inst, addr, &reg, 1u, data, len, 1);
}

/* ---------------------------------------------------------------- Sense HAT */
int sensehat_joystick_read(struct sensehat_efb_instance *inst, uint8_t *mask)
{
    uint8_t reg = SENSEHAT_REG_JOYSTICK;
    uint8_t v = 0;
    if (mask == NULL)
        return SENSEHAT_ERR_ARG;
    int rc = sensehat_i2c_write_read(inst, SENSEHAT_ADDR_MCU, &reg, 1u, &v, 1u);
    if (rc == SENSEHAT_OK)
        *mask = (uint8_t)(v & SENSEHAT_JOY_MASK);
    return rc;
}

int sensehat_probe(struct sensehat_efb_instance *inst)
{
    uint8_t v;
    return sensehat_joystick_read(inst, &v);
}

int sensehat_fb_write(struct sensehat_efb_instance *inst, const uint8_t fb[SENSEHAT_FB_BYTES])
{
    return sensehat_i2c_write_block(inst, SENSEHAT_ADDR_MCU, SENSEHAT_REG_FB_START, fb, SENSEHAT_FB_BYTES);
}

void sensehat_fb_clear(uint8_t fb[SENSEHAT_FB_BYTES])
{
    for (uint32_t i = 0; i < SENSEHAT_FB_BYTES; i++)
        fb[i] = 0u;
}

static uint8_t clamp_color(uint8_t c)
{
    return (c > SENSEHAT_COLOR_MAX) ? SENSEHAT_COLOR_MAX : c;
}

void sensehat_fb_set_pixel(uint8_t fb[SENSEHAT_FB_BYTES], uint8_t x, uint8_t y,
                           uint8_t r, uint8_t g, uint8_t b)
{
    if (x >= SENSEHAT_COLS || y >= SENSEHAT_ROWS)
        return;
    uint32_t row = (uint32_t)y * (SENSEHAT_PLANES * SENSEHAT_COLS);   /* y * 24, no multiplier needed by compiler */
    fb[row + 0u * SENSEHAT_COLS + x] = clamp_color(r);
    fb[row + 1u * SENSEHAT_COLS + x] = clamp_color(g);
    fb[row + 2u * SENSEHAT_COLS + x] = clamp_color(b);
}

uint8_t sensehat_efb_reg(const struct sensehat_efb_instance *inst, uint32_t off)
{
    return efb_rd(inst, off);
}

const char *sensehat_strerror(int code)
{
    switch (code) {
    case SENSEHAT_OK:          return "ok";
    case SENSEHAT_ERR_TIMEOUT: return "timeout";
    case SENSEHAT_ERR_NACK:    return "no acknowledge";
    case SENSEHAT_ERR_ARBL:    return "arbitration lost or bus busy";
    case SENSEHAT_ERR_ARG:     return "invalid argument";
    default:                   return "unknown error";
    }
}
