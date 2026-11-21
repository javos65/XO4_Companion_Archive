#include "i2c_master.h"
#include "reg_access.h"
#include <stddef.h>

/* Starts one byte-level command and waits until it is done. */
static uint8_t i2c_cmd(struct i2c_master_instance *this_i2c, uint32_t cmd, uint8_t tx, uint32_t *status)
{
    uint32_t base = this_i2c->base_address;
    uint32_t st = 0;
    uint32_t n;

    if (cmd & I2C_MASTER_CMD_WRITE) {
        reg_32b_write(base + I2C_MASTER_REG_TXDATA, tx);
    }
    reg_32b_write(base + I2C_MASTER_REG_CMD, cmd);

    for (n = 0; n < I2C_MASTER_TIMEOUT_POLLS; n++) {
        reg_32b_read(base + I2C_MASTER_REG_STATUS, &st);
        if (0u == (st & I2C_MASTER_STATUS_BUSY)) {
            if (NULL != status) {
                *status = st;
            }
            return I2C_MASTER_OK;
        }
    }
    return I2C_MASTER_ERR_TIMEOUT;
}

/* Releases the bus after an error. */
static void i2c_abort(struct i2c_master_instance *this_i2c)
{
    (void)i2c_cmd(this_i2c, I2C_MASTER_CMD_STOP, 0u, NULL);
}

/* Sends one byte (with START when start != 0) and checks the ACK. */
static uint8_t i2c_send(struct i2c_master_instance *this_i2c, uint8_t byte, uint8_t start, uint8_t stop)
{
    uint32_t st;
    uint32_t cmd = I2C_MASTER_CMD_WRITE;
    uint8_t ret;

    if (start) cmd |= I2C_MASTER_CMD_START;
    if (stop)  cmd |= I2C_MASTER_CMD_STOP;

    ret = i2c_cmd(this_i2c, cmd, byte, &st);
    if (I2C_MASTER_OK != ret) {
        return ret;
    }
    if (st & I2C_MASTER_STATUS_RXNACK) {
        if (!stop) {
            i2c_abort(this_i2c);
        }
        return I2C_MASTER_ERR_NACK;
    }
    return I2C_MASTER_OK;
}

/* Receives len bytes, NACK + STOP on the last one. */
static uint8_t i2c_receive(struct i2c_master_instance *this_i2c, uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint32_t rx = 0;
    uint8_t ret;

    for (i = 0; i < len; i++) {
        uint32_t cmd = I2C_MASTER_CMD_READ;
        if (i == (len - 1u)) {
            cmd |= I2C_MASTER_CMD_NACK | I2C_MASTER_CMD_STOP;
        }
        ret = i2c_cmd(this_i2c, cmd, 0u, NULL);
        if (I2C_MASTER_OK != ret) {
            return ret;
        }
        reg_32b_read(this_i2c->base_address + I2C_MASTER_REG_RXDATA, &rx);
        data[i] = (uint8_t)rx;
    }
    return I2C_MASTER_OK;
}

uint8_t i2c_master_init(struct i2c_master_instance *this_i2c, uint32_t base_addr)
{
    uint32_t id = 0;

    if (NULL == this_i2c) {
        return I2C_MASTER_ERR_PARAM;
    }
    this_i2c->base_address = base_addr;
    reg_32b_read(base_addr + I2C_MASTER_REG_ID, &id);
    if (I2C_MASTER_ID_VALUE != id) {
        return I2C_MASTER_ERR_ID;
    }
    /* free a bus that is still owned (e.g. after a CPU reset in the middle of a transfer) */
    if (i2c_master_status(this_i2c) & I2C_MASTER_STATUS_OWNED) {
        i2c_abort(this_i2c);
    }
    return I2C_MASTER_OK;
}

uint32_t i2c_master_set_speed(struct i2c_master_instance *this_i2c, uint32_t sys_clk_hz, uint32_t scl_hz)
{
    uint32_t div;

    if ((NULL == this_i2c) || (0u == scl_hz) || (0u == sys_clk_hz)) {
        return 0u;
    }
    div = (sys_clk_hz + (4u * scl_hz) - 1u) / (4u * scl_hz);   /* ceil: never faster than asked */
    if (div < 1u) {
        div = 1u;
    }
    if (div > 65536u) {
        div = 65536u;
    }
    reg_32b_write(this_i2c->base_address + I2C_MASTER_REG_PRESCALE, div - 1u);
    return sys_clk_hz / (4u * div);
}

uint32_t i2c_master_status(struct i2c_master_instance *this_i2c)
{
    uint32_t st = 0;

    reg_32b_read(this_i2c->base_address + I2C_MASTER_REG_STATUS, &st);
    return st;
}

uint8_t i2c_master_write(struct i2c_master_instance *this_i2c, uint8_t addr7,
                         const uint8_t *data, uint32_t len, uint8_t stop)
{
    uint32_t i;
    uint8_t ret;

    if ((NULL == this_i2c) || ((NULL == data) && (0u != len))) {
        return I2C_MASTER_ERR_PARAM;
    }
    ret = i2c_send(this_i2c, (uint8_t)(addr7 << 1), 1u, (uint8_t)(stop && (0u == len)));
    if (I2C_MASTER_OK != ret) {
        return ret;
    }
    for (i = 0; i < len; i++) {
        ret = i2c_send(this_i2c, data[i], 0u, (uint8_t)(stop && (i == (len - 1u))));
        if (I2C_MASTER_OK != ret) {
            return ret;
        }
    }
    return I2C_MASTER_OK;
}

uint8_t i2c_master_read(struct i2c_master_instance *this_i2c, uint8_t addr7,
                        uint8_t *data, uint32_t len)
{
    uint8_t ret;

    if ((NULL == this_i2c) || (NULL == data) || (0u == len)) {
        return I2C_MASTER_ERR_PARAM;
    }
    ret = i2c_send(this_i2c, (uint8_t)((addr7 << 1) | 1u), 1u, 0u);
    if (I2C_MASTER_OK != ret) {
        return ret;
    }
    return i2c_receive(this_i2c, data, len);
}

uint8_t i2c_master_write_reg(struct i2c_master_instance *this_i2c, uint8_t addr7, uint8_t reg,
                             const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t ret;

    if ((NULL == this_i2c) || ((NULL == data) && (0u != len))) {
        return I2C_MASTER_ERR_PARAM;
    }
    ret = i2c_send(this_i2c, (uint8_t)(addr7 << 1), 1u, 0u);
    if (I2C_MASTER_OK != ret) {
        return ret;
    }
    ret = i2c_send(this_i2c, reg, 0u, (uint8_t)(0u == len));
    if (I2C_MASTER_OK != ret) {
        return ret;
    }
    for (i = 0; i < len; i++) {
        ret = i2c_send(this_i2c, data[i], 0u, (uint8_t)(i == (len - 1u)));
        if (I2C_MASTER_OK != ret) {
            return ret;
        }
    }
    return I2C_MASTER_OK;
}

uint8_t i2c_master_read_reg(struct i2c_master_instance *this_i2c, uint8_t addr7, uint8_t reg,
                            uint8_t *data, uint32_t len)
{
    uint8_t ret;

    if ((NULL == this_i2c) || (NULL == data) || (0u == len)) {
        return I2C_MASTER_ERR_PARAM;
    }
    ret = i2c_send(this_i2c, (uint8_t)(addr7 << 1), 1u, 0u);
    if (I2C_MASTER_OK != ret) {
        return ret;
    }
    ret = i2c_send(this_i2c, reg, 0u, 0u);
    if (I2C_MASTER_OK != ret) {
        return ret;
    }
    ret = i2c_send(this_i2c, (uint8_t)((addr7 << 1) | 1u), 1u, 0u);   /* repeated START */
    if (I2C_MASTER_OK != ret) {
        return ret;
    }
    return i2c_receive(this_i2c, data, len);
}

uint8_t i2c_master_probe(struct i2c_master_instance *this_i2c, uint8_t addr7)
{
    if (NULL == this_i2c) {
        return I2C_MASTER_ERR_PARAM;
    }
    return i2c_send(this_i2c, (uint8_t)(addr7 << 1), 1u, 1u);
}
