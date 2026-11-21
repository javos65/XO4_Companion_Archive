/* Driver of the I2C controller of the FXLS8971_I2C block (custom:module:fxls8971_i2c:1.0.0).
 *
 * Single-master I2C, 7-bit addresses. All transfer functions are blocking and
 * poll STATUS.BUSY with a timeout, so a stuck bus never hangs the CPU.
 */
#ifndef I2C_MASTER_H
#define I2C_MASTER_H

#include <stdint.h>
#include "i2c_master_regs.h"

/* return codes */
#define I2C_MASTER_OK            0
#define I2C_MASTER_ERR_PARAM     1   /* null pointer or bad argument */
#define I2C_MASTER_ERR_ID        2   /* ID register does not match: block not found */
#define I2C_MASTER_ERR_NACK      3   /* the target did not acknowledge */
#define I2C_MASTER_ERR_TIMEOUT   4   /* the command did not finish (SCL held low?) */

/* polls of STATUS before a command is declared timed out */
#define I2C_MASTER_TIMEOUT_POLLS 100000u

struct i2c_master_instance {
    const char *instance_name;
    uint32_t    base_address;
};

/* Binds the instance to the base address and checks the ID register.
 * Returns I2C_MASTER_OK, I2C_MASTER_ERR_PARAM or I2C_MASTER_ERR_ID. */
uint8_t i2c_master_init(struct i2c_master_instance *this_i2c, uint32_t base_addr);

/* Sets the SCL frequency from the APB clock: PRESCALE = sys_clk / (4 * scl) - 1, rounded up
 * so the bus is never faster than asked. Returns the real SCL frequency in Hz (0 on error). */
uint32_t i2c_master_set_speed(struct i2c_master_instance *this_i2c, uint32_t sys_clk_hz, uint32_t scl_hz);

/* Reads the STATUS register (combination of the I2C_MASTER_STATUS_* bits). */
uint32_t i2c_master_status(struct i2c_master_instance *this_i2c);

/* Writes len bytes to the target: START, address+W, data..., then STOP when stop != 0.
 * Without STOP the bus stays owned, so the next call makes a repeated START. */
uint8_t i2c_master_write(struct i2c_master_instance *this_i2c, uint8_t addr7,
                         const uint8_t *data, uint32_t len, uint8_t stop);

/* Reads len (>= 1) bytes from the target: START, address+R, data... (NACK on the last), STOP. */
uint8_t i2c_master_read(struct i2c_master_instance *this_i2c, uint8_t addr7,
                        uint8_t *data, uint32_t len);

/* Register write: START, address+W, reg, data..., STOP. */
uint8_t i2c_master_write_reg(struct i2c_master_instance *this_i2c, uint8_t addr7, uint8_t reg,
                             const uint8_t *data, uint32_t len);

/* Register read: START, address+W, reg, repeated START, address+R, data..., STOP. */
uint8_t i2c_master_read_reg(struct i2c_master_instance *this_i2c, uint8_t addr7, uint8_t reg,
                            uint8_t *data, uint32_t len);

/* Checks if a target answers on addr7 (address+W, then STOP). Returns I2C_MASTER_OK when it ACKs. */
uint8_t i2c_master_probe(struct i2c_master_instance *this_i2c, uint8_t addr7);

#endif /* I2C_MASTER_H */
