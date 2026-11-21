/* Register map of the I2C controller of the FXLS8971_I2C block (custom:module:fxls8971_i2c:1.0.0). */
#ifndef I2C_MASTER_REGS_H
#define I2C_MASTER_REGS_H

/* register offsets */
#define I2C_MASTER_REG_ID        0x00u
#define I2C_MASTER_REG_PRESCALE  0x04u
#define I2C_MASTER_REG_CMD       0x08u
#define I2C_MASTER_REG_TXDATA    0x0Cu
#define I2C_MASTER_REG_RXDATA    0x10u
#define I2C_MASTER_REG_STATUS    0x14u

#define I2C_MASTER_ID_VALUE      0x49324331u   /* "I2C1" */

/* CMD bits */
#define I2C_MASTER_CMD_START     0x01u   /* START, or repeated START when the bus is owned */
#define I2C_MASTER_CMD_STOP      0x02u   /* STOP after the byte */
#define I2C_MASTER_CMD_WRITE     0x04u   /* send TXDATA */
#define I2C_MASTER_CMD_READ      0x08u   /* receive one byte into RXDATA */
#define I2C_MASTER_CMD_NACK      0x10u   /* with READ: answer NACK (last byte) */

/* STATUS bits */
#define I2C_MASTER_STATUS_BUSY   0x01u
#define I2C_MASTER_STATUS_RXNACK 0x02u
#define I2C_MASTER_STATUS_DONE   0x04u
#define I2C_MASTER_STATUS_OWNED  0x08u
#define I2C_MASTER_STATUS_SCL    0x10u
#define I2C_MASTER_STATUS_SDA    0x20u

#endif /* I2C_MASTER_REGS_H */
