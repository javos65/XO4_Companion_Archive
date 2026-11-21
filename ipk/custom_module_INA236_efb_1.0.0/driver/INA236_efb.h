/* INA236_efb 1.0.0: BSP driver for INA236A current and voltage monitors on the MachXO4 EFB
 * primary I2C controller, accessed through the INA236_efb APB block.
 *
 * Hardware: the lscc_efb IP (APB, 32-bit data, 10-bit address) behind INA236_efb, primary I2C
 * enabled (access type User), i2c1_scl / i2c1_sda on the fixed pins with external pull-ups.
 * The driver is the only I2C master; it never uses interrupts. All functions are blocking and
 * poll the controller with a bounded loop. Every function returns INA236_OK (0) or a negative
 * INA236_ERR_* code; the set-up function returns 0 or 1 (the Propel driver convention).
 *
 * Arithmetic is integer only (RV32I, no multiplier assumed, no floating point). The conversion
 * uses constants and one division by 5 (library routine) per bus voltage value.
 */
#ifndef INA236_EFB_H
#define INA236_EFB_H

#include <stdint.h>
#include "INA236_efb_regs.h"

/* ---- return codes ---- */
#define INA236_OK            0
#define INA236_ERR_TIMEOUT   (-1)   /* controller flag did not change within the poll limit */
#define INA236_ERR_NACK      (-2)   /* addressed device or data byte not acknowledged */
#define INA236_ERR_ARBL      (-3)   /* arbitration lost or bus busy before the transfer */
#define INA236_ERR_ARG       (-4)   /* null pointer, length zero or address out of range */
#define INA236_ERR_ID        (-5)   /* manufacturer or device ID does not match an INA236 */
#define INA236_ERR_CAL       (-6)   /* calibration fault (zero SHUNT_CAL read back, or bad value) */

/* Default poll limit per wait (each poll is one APB read) */
#define INA236_POLL_LIMIT_DEFAULT  200000u

/* ---- INA236A target addresses (7 bit), Table 7-1: A0 = GND, VS, SDA, SCL ---- */
#define INA236_ADDR_A0_GND   0x40u
#define INA236_ADDR_A0_VS    0x41u
#define INA236_ADDR_A0_SDA   0x42u
#define INA236_ADDR_A0_SCL   0x43u

/* ---- scaling constants (data sheet Table 8-1 and Equations 1 to 4) ---- */
#define INA236_SHUNT_NV_PER_LSB_R0   2500u   /* ADCRANGE 0: 2.5 uV per LSB (in nV) */
#define INA236_SHUNT_NV_PER_LSB_R1   625u    /* ADCRANGE 1: 625 nV per LSB */
#define INA236_BUS_UV_PER_LSB        1600u   /* 1.6 mV per LSB (in uV) */
#define INA236_POWER_LSB_FACTOR      32u     /* Power LSB = 32 x Current_LSB */

struct ina236_efb_instance {
    uint32_t base_address;      /* INA236_efb APB base address */
    uint32_t poll_limit;        /* poll limit per wait */
    uint32_t wb_clk_hz;         /* EFB WISHBONE (APB) clock in Hz, set by ina236_i2c_init */
    uint32_t i2c_hz;            /* I2C clock in Hz actually programmed */
    uint8_t  last_sr;           /* I2C_1_SR when the last error occurred */
    uint8_t  last_addr;         /* 7-bit address of the last transfer */
    void   (*delay_us)(uint32_t us);  /* optional microsecond delay (read real-time wait); NULL = loop */
};

/* Per-channel setting: the shunt value is a constant of the board (10 milliohm). */
struct ina236_channel {
    uint8_t  addr;              /* 7-bit I2C address */
    uint16_t config;            /* value for CONFIG, INA236_CONFIG_DEFAULT (0x4127) */
    uint16_t shunt_cal;         /* value for SHUNT_CAL (512 for 10 milliohm, 1 mA LSB) */
    uint8_t  adcrange;          /* 0 = +/-81.92 mV, 1 = +/-20.48 mV (must match config bit 12) */
    uint32_t current_lsb_ua;    /* current LSB in uA (1000) */
};

/* One converted measurement (integer, signed where the register is two's complement). */
struct ina236_sample {
    int16_t  raw_shunt;
    uint16_t raw_bus;
    int16_t  raw_current;
    uint16_t raw_power;
    int32_t  shunt_uv;          /* shunt voltage in microvolt */
    uint32_t bus_mv;            /* bus voltage in millivolt */
    int32_t  current_ua;        /* current in microampere (raw x current_lsb_ua) */
    int32_t  current_ma;        /* current in milliampere (current_ua / 1000, truncated) */
    uint32_t power_uw;          /* power in microwatt (raw x 32 x current_lsb_ua) */
    uint32_t power_mw;          /* power in milliwatt (power_uw / 1000, truncated) */
};

/* ---- set-up ---- */
/* Binds the instance to the block base address. Does not access the hardware.
 * Returns 0, or 1 for a null pointer. */
unsigned char ina236_efb_init(struct ina236_efb_instance *inst, uint32_t base_addr);
/* Programs the EFB I2C prescale for i2c_hz from wb_clk_hz (f = wb_clk / (prescale x 4)), then
 * enables the controller (CR = I2CEN | SDA delay off, 0x8C). Returns INA236_OK or INA236_ERR_ARG when the prescale is
 * outside 1..1023. Example: 12 MHz and 100 kHz give prescale 30. */
int ina236_i2c_init(struct ina236_efb_instance *inst, uint32_t wb_clk_hz, uint32_t i2c_hz);
/* Resets the I2C core (rewrites CR) and reprograms the enable. Use after a timeout. */
int ina236_i2c_reset(struct ina236_efb_instance *inst);
/* Best-effort bus recovery: issues a STOP and resets the core. The EFB controller cannot
 * generate free-running SCL pulses, so a stuck SDA held by a target can not be cleared here;
 * the function reports INA236_ERR_ARBL when BUSY stays set afterwards. */
int ina236_i2c_recover(struct ina236_efb_instance *inst);

/* ---- raw I2C transfers (7-bit address) ---- */
int ina236_i2c_write(struct ina236_efb_instance *inst, uint8_t addr, const uint8_t *data, uint32_t len);
/* Writes wlen bytes, repeated START, reads rlen bytes (last byte NACK + STOP). */
int ina236_i2c_write_read(struct ina236_efb_instance *inst, uint8_t addr,
                          const uint8_t *wdata, uint32_t wlen, uint8_t *rdata, uint32_t rlen);

/* ---- INA236A register access ---- */
int ina236_read_reg(struct ina236_efb_instance *inst, uint8_t addr, uint8_t reg, uint16_t *value);
int ina236_write_reg(struct ina236_efb_instance *inst, uint8_t addr, uint8_t reg, uint16_t value);
/* Reads the manufacturer and device ID and checks 0x5449 and 0xA080 (bits 3..0 masked). */
int ina236_probe(struct ina236_efb_instance *inst, uint8_t addr);

/* ---- calibration ---- */
/* SHUNT_CAL = 0.00512 / (Current_LSB x Rshunt), divided by 4 for ADCRANGE 1 (Equation 1).
 * rshunt_uohm in micro-ohm, current_lsb_ua in micro-ampere. Returns 0 when the result does not
 * fit 1..32767. Example: 10000 uohm and 1000 uA give 512. */
uint16_t ina236_calc_shunt_cal(uint32_t rshunt_uohm, uint32_t current_lsb_ua, uint8_t adcrange);
/* Fills a channel with the standard setting: CONFIG 0x4127, ADCRANGE 0. */
void ina236_channel_default(struct ina236_channel *ch, uint8_t addr, uint32_t rshunt_uohm,
                            uint32_t current_lsb_ua);
/* Writes CONFIG and SHUNT_CAL, then reads SHUNT_CAL back and checks it. */
int ina236_configure(struct ina236_efb_instance *inst, const struct ina236_channel *ch);

/* ---- measurement ---- */
/* Reads SHUNT, BUS, CURRENT and POWER and converts them. The sample is filled even when the
 * result is INA236_ERR_CAL. Calibration fault: CURRENT reads 0 although SHUNT x SHUNT_CAL / 2048
 * (/ 8192 for ADCRANGE 1, Equation 2) is at least 2 LSB, for example after a device reset.
 * Power = POWER x 32 x current_lsb_ua (in uW), multiplied before any division, so it is exact
 * for any current_lsb_ua up to 1000 (no 32-bit overflow). */
int ina236_read_sample(struct ina236_efb_instance *inst, const struct ina236_channel *ch,
                       struct ina236_sample *s);

/* Raw read of one EFB register (byte offset, e.g. INA236_EFB_I2C_SR) for diagnostics. */
uint8_t ina236_efb_reg(const struct ina236_efb_instance *inst, uint32_t off);

const char *ina236_strerror(int code);

#endif /* INA236_EFB_H */
