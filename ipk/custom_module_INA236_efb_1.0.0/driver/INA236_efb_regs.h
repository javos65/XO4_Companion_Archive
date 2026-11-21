/* INA236_efb 1.0.0: register and bit definitions.
 *
 * Part 1: MachXO4 EFB primary I2C controller (I2C1) as seen through the INA236_efb APB window
 * (lscc_efb with APB_DATA_WIDTH = 32: each 8-bit EFB register is on its own 32-bit word,
 * offset = EFB register index x 4). Source: FPGA-IPUG-02287 MachXO4 EFB Module IP User Guide,
 * Tables 5.2 to 5.11 (register indexes 0x40 to 0x49).
 *
 * Part 2: TI INA236 registers, Table 7-2 to 7-13 of the INA236 data sheet (SBOSA81D).
 */
#ifndef INA236_EFB_REGS_H
#define INA236_EFB_REGS_H

/* ---- EFB I2C registers (byte offsets from the block base) ----
 * Primary controller (I2C1): EFB index 0x40..0x49 -> offsets 0x100..0x124 (default).
 * Secondary controller (I2C2): EFB index 0x4A..0x53 -> offsets 0x128..0x14C, selected by
 * defining INA236_EFB_USE_I2C2 before including this header. The register order is identical. */
#ifdef INA236_EFB_USE_I2C2
#define INA236_EFB_I2C_BASE    0x128u
#else
#define INA236_EFB_I2C_BASE    0x100u
#endif
#define INA236_EFB_I2C_CR      (INA236_EFB_I2C_BASE + 0x00u)   /* RW  */
#define INA236_EFB_I2C_CMDR    (INA236_EFB_I2C_BASE + 0x04u)   /* RW  */
#define INA236_EFB_I2C_BR0     (INA236_EFB_I2C_BASE + 0x08u)   /* RW  */
#define INA236_EFB_I2C_BR1     (INA236_EFB_I2C_BASE + 0x0Cu)   /* RW  */
#define INA236_EFB_I2C_TXDR    (INA236_EFB_I2C_BASE + 0x10u)   /* WO  */
#define INA236_EFB_I2C_SR      (INA236_EFB_I2C_BASE + 0x14u)   /* RO  */
#define INA236_EFB_I2C_GCDR    (INA236_EFB_I2C_BASE + 0x18u)   /* RO  */
#define INA236_EFB_I2C_RXDR    (INA236_EFB_I2C_BASE + 0x1Cu)   /* RO  */
#define INA236_EFB_I2C_IRQ     (INA236_EFB_I2C_BASE + 0x20u)   /* RW (write 1 to clear) */
#define INA236_EFB_I2C_IRQEN   (INA236_EFB_I2C_BASE + 0x24u)   /* RW  */

/* I2C_1_CR */
#define INA236_EFB_CR_I2CEN    (1u << 7)
#define INA236_EFB_CR_GCEN     (1u << 6)
#define INA236_EFB_CR_WKUPEN   (1u << 5)
/* SDA_DEL_SEL (bits 3..2): 11 = SDA output delay off. The delay is clocked by the internal
 * oscillator; without an OSC instance in the design the I2C core does not run unless the delay is
 * off (FPGA-TN-02403, section 4.3.6). The design has no OSC, so the driver always sets 11. */
#define INA236_EFB_CR_SDA_DEL_OFF (3u << 2)
#define INA236_EFB_CR_RUN      (INA236_EFB_CR_I2CEN | INA236_EFB_CR_SDA_DEL_OFF)   /* 0x8C */
/* I2C_1_CMDR */
#define INA236_EFB_CMDR_STA    (1u << 7)
#define INA236_EFB_CMDR_STO    (1u << 6)
#define INA236_EFB_CMDR_RD     (1u << 5)
#define INA236_EFB_CMDR_WR     (1u << 4)
#define INA236_EFB_CMDR_ACK    (1u << 3)   /* 0 = send ACK, 1 = send NACK (receive) */
#define INA236_EFB_CMDR_CKSDIS (1u << 2)   /* clock-stretch disable; 0 for controller use */
/* I2C_1_SR */
#define INA236_EFB_SR_TIP      (1u << 7)
#define INA236_EFB_SR_BUSY     (1u << 6)
#define INA236_EFB_SR_RARC     (1u << 5)   /* 1 = no acknowledge received */
#define INA236_EFB_SR_SRW      (1u << 4)
#define INA236_EFB_SR_ARBL     (1u << 3)
#define INA236_EFB_SR_TRRDY    (1u << 2)
#define INA236_EFB_SR_TROE     (1u << 1)   /* overrun, also set by a NACK */
#define INA236_EFB_SR_HGC      (1u << 0)
/* I2C_1_IRQ / I2C_1_IRQEN */
#define INA236_EFB_IRQ_ARBL    (1u << 3)
#define INA236_EFB_IRQ_TRRDY   (1u << 2)
#define INA236_EFB_IRQ_TROE    (1u << 1)
#define INA236_EFB_IRQ_HGC     (1u << 0)

/* ---- INA236 registers (pointer byte values) ---- */
#define INA236_REG_CONFIG      0x00u   /* reset 0x4127 */
#define INA236_REG_SHUNT       0x01u   /* 2.5 uV/LSB (ADCRANGE 0), two's complement */
#define INA236_REG_BUS         0x02u   /* 1.6 mV/LSB, bit 15 reads 0 */
#define INA236_REG_POWER       0x03u   /* 32 x CURRENT_LSB per LSB, unsigned */
#define INA236_REG_CURRENT     0x04u   /* CURRENT_LSB per LSB, two's complement */
#define INA236_REG_SHUNT_CAL   0x05u
#define INA236_REG_MASK_ENABLE 0x06u
#define INA236_REG_ALERT_LIMIT 0x07u
#define INA236_REG_MFR_ID      0x3Eu   /* reads 0x5449 ("TI") */
#define INA236_REG_DEVICE_ID   0x3Fu   /* reads 0xA080 in bits 15..4; bits 3..0 read 0 */

#define INA236_MFR_ID_VALUE    0x5449u
#define INA236_DEVICE_ID_VALUE 0xA080u
#define INA236_DEVICE_ID_MASK  0xFFF0u

/* CONFIG fields */
#define INA236_CFG_RST         (1u << 15)
#define INA236_CFG_ADCRANGE    (1u << 12)   /* 0 = +/-81.92 mV, 1 = +/-20.48 mV */
/* Standard setting: reset default 0x4127 = ADCRANGE 0, AVG 1, VBUSCT 1100 us,
 * VSHCT 1100 us, MODE 111b (continuous shunt and bus voltage) */
#define INA236_CONFIG_DEFAULT  0x4127u

/* MASK/ENABLE bits used */
#define INA236_ME_CVRF         (1u << 3)    /* conversion ready flag */

#endif /* INA236_EFB_REGS_H */
