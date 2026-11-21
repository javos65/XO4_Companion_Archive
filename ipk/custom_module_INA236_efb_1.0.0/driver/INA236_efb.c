/* INA236_efb 1.0.0: BSP driver, see INA236_efb.h.
 *
 * EFB I2C controller sequence: exactly FPGA-IPUG-02287 Figure 7.9 (I2C Controller Read/Write
 * Example through WISHBONE), with CKSDIS = 0 (clock stretching on, Table 5.3 for controller use),
 * so the next command may come late; the end of every byte is taken from TIP (byte_done):
 *   write: TXDR <= addr+W, CMDR <= 0x90 (STA+WR), wait TRRDY;
 *          per byte TXDR <= data, CMDR <= 0x10 (WR), wait TRRDY;   end CMDR <= 0x40 (STO).
 *   read:  TXDR <= addr+R, CMDR <= 0x90, byte done, CMDR <= 0x20 (RD);
 *          per byte except the last: wait TRRDY, data <= RXDR;
 *          last byte: real-time wait, CMDR <= 0x68 (RD+NACK+STO), wait TRRDY, data <= RXDR.
 *          Real-time wait: 1 byte read 2..7 tSCL, last of 2 or more 0..7 tSCL (3 tSCL used).
 * Status rules (Table 5.7): SR bits other than BUSY are valid only while BUSY = 1; TIP can be
 * high after configuration before the first transfer; SRW is valid 4 WISHBONE clocks after
 * TRRDY. A NACK while transmitting sets TROE and RARC.
 * CR is written as 0x8C (I2CEN, SDA delay off): the SDA delay needs the internal oscillator,
 * which this design does not instantiate (FPGA-TN-02403 section 4.3.6).
 */
#include <stddef.h>
#include "INA236_efb.h"

#define CMD_START_WR  (INA236_EFB_CMDR_STA | INA236_EFB_CMDR_WR)                         /* 0x90 */
#define CMD_WR        (INA236_EFB_CMDR_WR)                                                 /* 0x10 */
#define CMD_RD        (INA236_EFB_CMDR_RD)                                                 /* 0x20 */
#define CMD_RD_LAST   (INA236_EFB_CMDR_RD | INA236_EFB_CMDR_ACK | INA236_EFB_CMDR_STO)     /* 0x68 */
#define CMD_STOP      (INA236_EFB_CMDR_STO)                                                /* 0x40 */

/* ---------------------------------------------------------------- EFB register access */
static inline uint8_t efb_rd(const struct ina236_efb_instance *inst, uint32_t off)
{
    return (uint8_t)(*(volatile uint32_t *)(inst->base_address + off) & 0xFFu);
}

static inline void efb_wr(const struct ina236_efb_instance *inst, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(inst->base_address + off) = v & 0xFFu;
}

/* Delay in microseconds: the application hook when set, else a rough SR-read loop. */
static void delay_us(const struct ina236_efb_instance *inst, uint32_t us)
{
    if (inst->delay_us != NULL) {
        inst->delay_us(us);
        return;
    }
    for (uint32_t n = 0; n < us * 4u; n++)
        (void)efb_rd(inst, INA236_EFB_I2C_SR);
}

/* SCL period in microseconds (10 at 100 kHz) */
static uint32_t tscl_us(const struct ina236_efb_instance *inst)
{
    return (inst->i2c_hz != 0u) ? (1000000u + inst->i2c_hz - 1u) / inst->i2c_hz : 10u;
}

/* Waits until (SR & mask) == want; records SR in last_sr on timeout */
static int wait_sr(struct ina236_efb_instance *inst, uint8_t mask, uint8_t want)
{
    uint8_t sr = 0;
    for (uint32_t n = 0; n < inst->poll_limit; n++) {
        sr = efb_rd(inst, INA236_EFB_I2C_SR);
        if ((sr & mask) == want)
            return INA236_OK;
    }
    inst->last_sr = sr;
    return INA236_ERR_TIMEOUT;
}

/* End of one byte (address or data, sent or received): wait until TIP has been seen high and is
 * low again, with BUSY = 1, and only then read RARC. RARC is not meaningful while TIP = 1 or
 * before the first transfer: hardware 2026-11-18 showed SR = 0x20 (RARC = 1) at idle and
 * SR = 0xE0 (TIP, BUSY, RARC) in the middle of the address byte. TIP rises half an SCL period
 * after START (Table 5.7), so "TIP low" right after the command is not "byte done". */
static int byte_done(struct ina236_efb_instance *inst)
{
    uint8_t sr = 0;
    int seen_tip = 0;
    for (uint32_t n = 0; n < inst->poll_limit; n++) {
        sr = efb_rd(inst, INA236_EFB_I2C_SR);
        if ((sr & INA236_EFB_SR_BUSY) == 0u)
            continue;                                   /* START not yet on the bus */
        if (sr & INA236_EFB_SR_ARBL) { inst->last_sr = sr; return INA236_ERR_ARBL; }
        if (sr & INA236_EFB_SR_TIP) { seen_tip = 1; continue; }
        if (seen_tip) {
            inst->last_sr = sr;
            return (sr & INA236_EFB_SR_RARC) ? INA236_ERR_NACK : INA236_OK;
        }
    }
    inst->last_sr = sr;
    return INA236_ERR_TIMEOUT;
}
/* STOP after an error; resets the core when the bus does not become free. Keeps last_sr. */
static void abort_transfer(struct ina236_efb_instance *inst)
{
    uint8_t keep = inst->last_sr;
    efb_wr(inst, INA236_EFB_I2C_CMDR, CMD_STOP);
    if (wait_sr(inst, INA236_EFB_SR_BUSY, 0) != INA236_OK) {
        efb_wr(inst, INA236_EFB_I2C_CR, INA236_EFB_CR_SDA_DEL_OFF);   /* a write to CR resets the core */
        efb_wr(inst, INA236_EFB_I2C_CR, INA236_EFB_CR_RUN);
    }
    efb_wr(inst, INA236_EFB_I2C_IRQ, 0x0Fu);
    inst->last_sr = keep;
}

/* ---------------------------------------------------------------- set-up */
unsigned char ina236_efb_init(struct ina236_efb_instance *inst, uint32_t base_addr)
{
    if (inst == NULL)
        return 1;
    inst->base_address = base_addr;
    inst->poll_limit   = INA236_POLL_LIMIT_DEFAULT;
    inst->wb_clk_hz    = 0;
    inst->i2c_hz       = 0;
    inst->last_sr      = 0;
    inst->last_addr    = 0;
    inst->delay_us     = NULL;
    return 0;
}

int ina236_i2c_init(struct ina236_efb_instance *inst, uint32_t wb_clk_hz, uint32_t i2c_hz)
{
    if (inst == NULL || i2c_hz == 0u || wb_clk_hz == 0u)
        return INA236_ERR_ARG;
    uint32_t div  = 4u * i2c_hz;
    uint32_t pres = (wb_clk_hz + div / 2u) / div;       /* rounded; 12 MHz, 100 kHz -> 30 */
    if (pres < 1u || pres > 1023u)
        return INA236_ERR_ARG;
    efb_wr(inst, INA236_EFB_I2C_CR, INA236_EFB_CR_SDA_DEL_OFF);  /* core off while the rate changes */
    efb_wr(inst, INA236_EFB_I2C_BR0, pres & 0xFFu);
    efb_wr(inst, INA236_EFB_I2C_BR1, (pres >> 8) & 0x03u);   /* a write to BR1 resets the core */
    efb_wr(inst, INA236_EFB_I2C_CR, INA236_EFB_CR_RUN);
    efb_wr(inst, INA236_EFB_I2C_CMDR, 0u);  /* idle command, as in Figure 7.9 */
    efb_wr(inst, INA236_EFB_I2C_IRQ, 0x0Fu);
    inst->wb_clk_hz = wb_clk_hz;
    inst->i2c_hz    = wb_clk_hz / (4u * pres);
    return INA236_OK;
}

int ina236_i2c_reset(struct ina236_efb_instance *inst)
{
    if (inst == NULL)
        return INA236_ERR_ARG;
    efb_wr(inst, INA236_EFB_I2C_CR, INA236_EFB_CR_SDA_DEL_OFF);   /* a write to CR resets the core */
    efb_wr(inst, INA236_EFB_I2C_CR, INA236_EFB_CR_RUN);
    efb_wr(inst, INA236_EFB_I2C_CMDR, 0u);
    efb_wr(inst, INA236_EFB_I2C_IRQ, 0x0Fu);
    return INA236_OK;
}

int ina236_i2c_recover(struct ina236_efb_instance *inst)
{
    if (inst == NULL)
        return INA236_ERR_ARG;
    abort_transfer(inst);
    (void)ina236_i2c_reset(inst);
    return (efb_rd(inst, INA236_EFB_I2C_SR) & INA236_EFB_SR_BUSY) ? INA236_ERR_ARBL : INA236_OK;
}

/* ---------------------------------------------------------------- raw transfers */
/* START + address + W, then the data bytes; STOP when stop != 0. */
static int do_write(struct ina236_efb_instance *inst, uint8_t addr,
                    const uint8_t *data, uint32_t len, int stop)
{
    int rc;
    efb_wr(inst, INA236_EFB_I2C_TXDR, ((uint32_t)addr << 1) | 0u);
    efb_wr(inst, INA236_EFB_I2C_CMDR, CMD_START_WR);
    rc = byte_done(inst);
    for (uint32_t i = 0; rc == INA236_OK && i < len; i++) {
        efb_wr(inst, INA236_EFB_I2C_TXDR, data[i]);
        efb_wr(inst, INA236_EFB_I2C_CMDR, CMD_WR);
        rc = byte_done(inst);
    }
    if (rc != INA236_OK) {
        abort_transfer(inst);
        return rc;
    }
    if (stop) {
        efb_wr(inst, INA236_EFB_I2C_CMDR, CMD_STOP);
        if (wait_sr(inst, INA236_EFB_SR_BUSY, 0) != INA236_OK)
            return INA236_ERR_TIMEOUT;
    }
    return INA236_OK;
}

/* (Repeated) START + address + R, len bytes, NACK + STOP on the last (Figure 7.9 right half). */
static int do_read(struct ina236_efb_instance *inst, uint8_t addr, uint8_t *data, uint32_t len)
{
    int rc;
    uint32_t t = tscl_us(inst);
    efb_wr(inst, INA236_EFB_I2C_TXDR, ((uint32_t)addr << 1) | 1u);
    efb_wr(inst, INA236_EFB_I2C_CMDR, CMD_START_WR);
    rc = byte_done(inst);                               /* address + R acknowledged? */
    if (rc == INA236_OK && len > 1u) {
        efb_wr(inst, INA236_EFB_I2C_CMDR, CMD_RD);
        for (uint32_t i = 0; rc == INA236_OK && i + 1u < len; i++) {
            rc = wait_sr(inst, INA236_EFB_SR_TRRDY, INA236_EFB_SR_TRRDY);
            if (rc == INA236_OK)
                data[i] = efb_rd(inst, INA236_EFB_I2C_RXDR);
        }
    }
    if (rc == INA236_OK) {
        if (len == 1u) {                                /* 1 byte: start it, then NACK + STOP 2..7 tSCL later */
            efb_wr(inst, INA236_EFB_I2C_CMDR, CMD_RD);
            delay_us(inst, 3u * t);
        }
        efb_wr(inst, INA236_EFB_I2C_CMDR, CMD_RD_LAST);
        rc = wait_sr(inst, INA236_EFB_SR_TRRDY, INA236_EFB_SR_TRRDY);
        if (rc == INA236_OK)
            data[len - 1u] = efb_rd(inst, INA236_EFB_I2C_RXDR);
    }
    if (rc != INA236_OK) {
        abort_transfer(inst);
        return rc;
    }
    if (wait_sr(inst, INA236_EFB_SR_BUSY, 0) != INA236_OK)
        return INA236_ERR_TIMEOUT;
    efb_wr(inst, INA236_EFB_I2C_CMDR, 0u);
    return INA236_OK;
}

static int begin(struct ina236_efb_instance *inst, uint8_t addr)
{
    if (inst == NULL || addr > 0x7Fu)
        return INA236_ERR_ARG;
    inst->last_addr = addr;
    if (wait_sr(inst, INA236_EFB_SR_BUSY, 0) != INA236_OK)
        return INA236_ERR_ARBL;                          /* bus busy before the transfer */
    return INA236_OK;
}

int ina236_i2c_write(struct ina236_efb_instance *inst, uint8_t addr, const uint8_t *data, uint32_t len)
{
    int rc = begin(inst, addr);
    if (rc != INA236_OK)
        return rc;
    if (data == NULL && len != 0u)
        return INA236_ERR_ARG;
    return do_write(inst, addr, data, len, 1);
}

int ina236_i2c_write_read(struct ina236_efb_instance *inst, uint8_t addr,
                          const uint8_t *wdata, uint32_t wlen, uint8_t *rdata, uint32_t rlen)
{
    int rc = begin(inst, addr);
    if (rc != INA236_OK)
        return rc;
    if (rdata == NULL || rlen == 0u || (wdata == NULL && wlen != 0u))
        return INA236_ERR_ARG;
    rc = do_write(inst, addr, wdata, wlen, 0);           /* no STOP: repeated START follows */
    if (rc != INA236_OK)
        return rc;
    return do_read(inst, addr, rdata, rlen);
}
/* ---------------------------------------------------------------- INA236A registers */
int ina236_read_reg(struct ina236_efb_instance *inst, uint8_t addr, uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    if (value == NULL)
        return INA236_ERR_ARG;
    int rc = ina236_i2c_write_read(inst, addr, &reg, 1u, buf, 2u);
    if (rc == INA236_OK)
        *value = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    return rc;
}

int ina236_write_reg(struct ina236_efb_instance *inst, uint8_t addr, uint8_t reg, uint16_t value)
{
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (uint8_t)(value >> 8);
    buf[2] = (uint8_t)value;
    return ina236_i2c_write(inst, addr, buf, 3u);
}

int ina236_probe(struct ina236_efb_instance *inst, uint8_t addr)
{
    uint16_t mfr = 0, dev = 0;
    int rc = ina236_read_reg(inst, addr, INA236_REG_MFR_ID, &mfr);
    if (rc == INA236_OK)
        rc = ina236_read_reg(inst, addr, INA236_REG_DEVICE_ID, &dev);
    if (rc != INA236_OK)
        return rc;
    if (mfr != INA236_MFR_ID_VALUE || (dev & INA236_DEVICE_ID_MASK) != INA236_DEVICE_ID_VALUE)
        return INA236_ERR_ID;
    return INA236_OK;
}

/* ---------------------------------------------------------------- calibration */
uint16_t ina236_calc_shunt_cal(uint32_t rshunt_uohm, uint32_t current_lsb_ua, uint8_t adcrange)
{
    /* 0.00512 / (lsb_A x r_ohm) = 5.12e9 / (lsb_uA x r_uohm). 5.12e9 does not fit 32 bits, so
     * compute 2 x (2.56e9 / d) with the remainder rounded in. */
    if (rshunt_uohm == 0u || current_lsb_ua == 0u || current_lsb_ua > 0x7FFFFFFFu / rshunt_uohm)
        return 0;
    uint32_t d   = current_lsb_ua * rshunt_uohm;
    uint32_t q   = 2560000000u / d;
    uint32_t rem = 2560000000u % d;
    uint32_t cal = 2u * q + ((2u * rem >= d) ? 1u : 0u);
    if (adcrange)
        cal = (cal + 2u) / 4u;                           /* Equation 1 note: divide by 4 */
    return (cal >= 1u && cal <= 32767u) ? (uint16_t)cal : 0;
}

void ina236_channel_default(struct ina236_channel *ch, uint8_t addr, uint32_t rshunt_uohm,
                            uint32_t current_lsb_ua)
{
    if (ch == NULL)
        return;
    ch->addr           = addr;
    ch->config         = INA236_CONFIG_DEFAULT;
    ch->adcrange       = 0;
    ch->current_lsb_ua = current_lsb_ua;
    ch->shunt_cal      = ina236_calc_shunt_cal(rshunt_uohm, current_lsb_ua, 0);
}

int ina236_configure(struct ina236_efb_instance *inst, const struct ina236_channel *ch)
{
    uint16_t rb = 0;
    if (ch == NULL || ch->shunt_cal == 0u)
        return INA236_ERR_ARG;
    int rc = ina236_write_reg(inst, ch->addr, INA236_REG_CONFIG, ch->config);
    if (rc == INA236_OK)
        rc = ina236_write_reg(inst, ch->addr, INA236_REG_SHUNT_CAL, ch->shunt_cal);
    if (rc == INA236_OK)
        rc = ina236_read_reg(inst, ch->addr, INA236_REG_SHUNT_CAL, &rb);
    if (rc != INA236_OK)
        return rc;
    return (rb == ch->shunt_cal) ? INA236_OK : INA236_ERR_CAL;
}

/* ---------------------------------------------------------------- measurement */
int ina236_read_sample(struct ina236_efb_instance *inst, const struct ina236_channel *ch,
                       struct ina236_sample *s)
{
    uint16_t v;
    int rc;
    if (ch == NULL || s == NULL)
        return INA236_ERR_ARG;

    rc = ina236_read_reg(inst, ch->addr, INA236_REG_SHUNT, &v);
    if (rc != INA236_OK) return rc;
    s->raw_shunt = (int16_t)v;
    rc = ina236_read_reg(inst, ch->addr, INA236_REG_BUS, &v);
    if (rc != INA236_OK) return rc;
    s->raw_bus = v;
    rc = ina236_read_reg(inst, ch->addr, INA236_REG_CURRENT, &v);
    if (rc != INA236_OK) return rc;
    s->raw_current = (int16_t)v;
    rc = ina236_read_reg(inst, ch->addr, INA236_REG_POWER, &v);
    if (rc != INA236_OK) return rc;
    s->raw_power = v;

    uint32_t nv = ch->adcrange ? INA236_SHUNT_NV_PER_LSB_R1 : INA236_SHUNT_NV_PER_LSB_R0;
    s->shunt_uv   = ((int32_t)s->raw_shunt * (int32_t)nv) / 1000;
    s->bus_mv     = ((uint32_t)(s->raw_bus & 0x7FFFu) * INA236_BUS_UV_PER_LSB) / 1000u;
    s->current_ua = (int32_t)s->raw_current * (int32_t)ch->current_lsb_ua;
    s->current_ma = s->current_ua / 1000;
    /* Power LSB = 32 x Current_LSB. Multiply first, divide last: exact for any LSB up to 1000 uA
     * (65535 x 1000 x 32 = 2.1e9 fits 32 bits unsigned). */
    s->power_uw   = (uint32_t)s->raw_power * ch->current_lsb_ua * INA236_POWER_LSB_FACTOR;
    s->power_mw   = s->power_uw / 1000u;

    /* Calibration fault: Equation 2, Current = SHUNT x SHUNT_CAL / 2048 (8192 for ADCRANGE 1) */
    int32_t expected = ((int32_t)s->raw_shunt * (int32_t)ch->shunt_cal) / (ch->adcrange ? 8192 : 2048);
    if (s->raw_current == 0 && (expected >= 2 || expected <= -2))
        return INA236_ERR_CAL;
    return INA236_OK;
}

uint8_t ina236_efb_reg(const struct ina236_efb_instance *inst, uint32_t off)
{
    return efb_rd(inst, off);
}

const char *ina236_strerror(int code)
{
    switch (code) {
    case INA236_OK:          return "ok";
    case INA236_ERR_TIMEOUT: return "timeout";
    case INA236_ERR_NACK:    return "no acknowledge";
    case INA236_ERR_ARBL:    return "arbitration lost or bus busy";
    case INA236_ERR_ARG:     return "invalid argument";
    case INA236_ERR_ID:      return "wrong device id";
    case INA236_ERR_CAL:     return "calibration fault";
    default:                 return "unknown error";
    }
}
