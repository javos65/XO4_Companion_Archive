/* SenseHat_efb 1.0.0: register and bit definitions.
 *
 * Part 1: MachXO4 EFB secondary I2C controller (I2C2) as seen through the SenseHat_efb APB window
 * (lscc_efb with APB_DATA_WIDTH = 32: each 8-bit EFB register is on its own 32-bit word,
 * offset = EFB register index x 4). Source: FPGA-IPUG-02287 MachXO4 EFB Module IP User Guide,
 * Tables 5.1 to 5.11 (secondary register indexes 0x4A to 0x53 -> offsets 0x128 to 0x14C).
 *
 * Part 2: Raspberry Pi Sense HAT, ATTINY88-MUR co-processor at I2C address 0x46 (LED matrix
 * framebuffer and 5-way joystick), as described in doc/Sense Hat Description and Mapping.md.
 */
#ifndef SENSEHAT_EFB_REGS_H
#define SENSEHAT_EFB_REGS_H

/* ---- EFB I2C2 registers (byte offsets from the block base) ---- */
#define SENSEHAT_EFB_I2C_BASE   0x128u
#define SENSEHAT_EFB_I2C_CR     (SENSEHAT_EFB_I2C_BASE + 0x00u)   /* RW  */
#define SENSEHAT_EFB_I2C_CMDR   (SENSEHAT_EFB_I2C_BASE + 0x04u)   /* RW  */
#define SENSEHAT_EFB_I2C_BR0    (SENSEHAT_EFB_I2C_BASE + 0x08u)   /* RW  */
#define SENSEHAT_EFB_I2C_BR1    (SENSEHAT_EFB_I2C_BASE + 0x0Cu)   /* RW  */
#define SENSEHAT_EFB_I2C_TXDR   (SENSEHAT_EFB_I2C_BASE + 0x10u)   /* WO  */
#define SENSEHAT_EFB_I2C_SR     (SENSEHAT_EFB_I2C_BASE + 0x14u)   /* RO  */
#define SENSEHAT_EFB_I2C_GCDR   (SENSEHAT_EFB_I2C_BASE + 0x18u)   /* RO  */
#define SENSEHAT_EFB_I2C_RXDR   (SENSEHAT_EFB_I2C_BASE + 0x1Cu)   /* RO  */
#define SENSEHAT_EFB_I2C_IRQ    (SENSEHAT_EFB_I2C_BASE + 0x20u)   /* RW (write 1 to clear) */
#define SENSEHAT_EFB_I2C_IRQEN  (SENSEHAT_EFB_I2C_BASE + 0x24u)   /* RW  */

/* I2C_2_CR */
#define SENSEHAT_EFB_CR_I2CEN   (1u << 7)
#define SENSEHAT_EFB_CR_GCEN    (1u << 6)
#define SENSEHAT_EFB_CR_WKUPEN  (1u << 5)
/* SDA_DEL_SEL (bits 3..2) = 11: SDA output delay off. The delay is clocked by the internal
 * oscillator; without an OSC instance in the design the I2C core does not run unless the delay is
 * off (FPGA-TN-02403, section 4.3.6). The design has no OSC, so the driver always sets 11. */
#define SENSEHAT_EFB_CR_SDA_DEL_OFF (3u << 2)
#define SENSEHAT_EFB_CR_RUN     (SENSEHAT_EFB_CR_I2CEN | SENSEHAT_EFB_CR_SDA_DEL_OFF)   /* 0x8C */
/* I2C_2_CMDR */
#define SENSEHAT_EFB_CMDR_STA    (1u << 7)
#define SENSEHAT_EFB_CMDR_STO    (1u << 6)
#define SENSEHAT_EFB_CMDR_RD     (1u << 5)
#define SENSEHAT_EFB_CMDR_WR     (1u << 4)
#define SENSEHAT_EFB_CMDR_ACK    (1u << 3)   /* 0 = send ACK, 1 = send NACK (receive) */
#define SENSEHAT_EFB_CMDR_CKSDIS (1u << 2)   /* clock-stretch disable; 0 for controller use */
/* I2C_2_SR */
#define SENSEHAT_EFB_SR_TIP      (1u << 7)
#define SENSEHAT_EFB_SR_BUSY     (1u << 6)
#define SENSEHAT_EFB_SR_RARC     (1u << 5)   /* 1 = no acknowledge received */
#define SENSEHAT_EFB_SR_SRW      (1u << 4)
#define SENSEHAT_EFB_SR_ARBL     (1u << 3)
#define SENSEHAT_EFB_SR_TRRDY    (1u << 2)
#define SENSEHAT_EFB_SR_TROE     (1u << 1)   /* overrun, also set by a NACK */
#define SENSEHAT_EFB_SR_HGC      (1u << 0)
/* I2C_2_IRQ / I2C_2_IRQEN */
#define SENSEHAT_EFB_IRQ_ARBL    (1u << 3)
#define SENSEHAT_EFB_IRQ_TRRDY   (1u << 2)
#define SENSEHAT_EFB_IRQ_TROE    (1u << 1)
#define SENSEHAT_EFB_IRQ_HGC     (1u << 0)

/* ---- Sense HAT ATTINY88-MUR (7-bit address 0x46) ---- */
#define SENSEHAT_ADDR_MCU        0x46u   /* LED matrix framebuffer + joystick */
/* Other I2C targets on the HAT bus (for reference, not used by this driver) */
#define SENSEHAT_ADDR_MAG        0x1Cu   /* LSM9DS1 magnetometer */
#define SENSEHAT_ADDR_IMU        0x6Au   /* LSM9DS1 accelerometer / gyroscope */
#define SENSEHAT_ADDR_HUMIDITY   0x5Fu   /* HTS221 */
#define SENSEHAT_ADDR_PRESSURE   0x5Cu   /* LPS25H */

#define SENSEHAT_REG_FB_START    0x00u   /* start of the auto-incrementing framebuffer */
#define SENSEHAT_REG_JOYSTICK    0xF2u   /* 1 byte, active-high bitmask */

/* Framebuffer: 8 rows x 24 bytes. Row y: bytes 0..7 red, 8..15 green, 16..23 blue (x = 0..7). */
#define SENSEHAT_ROWS            8u
#define SENSEHAT_COLS            8u
#define SENSEHAT_PLANES          3u
#define SENSEHAT_FB_BYTES        (SENSEHAT_ROWS * SENSEHAT_PLANES * SENSEHAT_COLS)   /* 192 */
#define SENSEHAT_COLOR_MAX       0x3Fu   /* 6-bit colour per the HAT description; bits 7..6 stay 0 */

/* Joystick register 0xF2 bitmask (1 = pressed) */
#define SENSEHAT_JOY_DOWN        0x01u
#define SENSEHAT_JOY_RIGHT       0x02u
#define SENSEHAT_JOY_UP          0x04u
#define SENSEHAT_JOY_CENTER      0x08u
#define SENSEHAT_JOY_LEFT        0x10u
#define SENSEHAT_JOY_MASK        0x1Fu

#endif /* SENSEHAT_EFB_REGS_H */
