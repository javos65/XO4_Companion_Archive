/* TCS34725 RGB colour sensor, BSP driver (see tcs34725.h). Register map: TCS3472 datasheet. */
#include <stddef.h>
#include "tcs34725.h"

#define CMD            0x80u    /* command register select */
#define CMD_INC        0xA0u    /* command + auto-increment transaction type */
#define REG_ENABLE     0x00u    /* AIEN[4] WEN[3] AEN[1] PON[0] */
#define REG_ATIME      0x01u
#define REG_CONTROL    0x0Fu    /* AGAIN[1:0] */
#define REG_ID         0x12u
#define REG_STATUS     0x13u    /* AINT[4] AVALID[0] */
#define REG_CDATAL     0x14u    /* C, R, G, B: 16 bit little endian */

#define EN_PON         0x01u
#define EN_AEN         0x02u
#define STATUS_AVALID  0x01u

static uint8_t last_id;

static int rd(struct sensehat_efb_instance *inst, uint8_t reg, uint8_t *buf, uint32_t n)
{
    uint8_t c = (uint8_t)((n > 1u ? CMD_INC : CMD) | reg);
    return sensehat_i2c_write_read(inst, TCS34725_ADDR, &c, 1u, buf, n);
}

static int wr(struct sensehat_efb_instance *inst, uint8_t reg, uint8_t val)
{
    return sensehat_i2c_write_block(inst, TCS34725_ADDR, (uint8_t)(CMD | reg), &val, 1u);
}

int tcs34725_init(struct sensehat_efb_instance *inst, uint8_t atime, uint8_t gain)
{
    uint8_t id = 0, unknown_id = 0;
    int rc = rd(inst, REG_ID, &id, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    last_id = id;
    if (id == 0x00u || id == 0xFFu)
        return TCS34725_ERR_ID;                       /* bus floating or no device */
    if (id != TCS34725_ID_34725 && id != TCS34725_ID_34727)
        unknown_id = 1;                               /* other ID: carry on, report TCS34725_ID_UNKNOWN */
    if ((rc = wr(inst, REG_ATIME, atime)) != SENSEHAT_OK)
        return rc;
    if ((rc = wr(inst, REG_CONTROL, (uint8_t)(gain & 0x03u))) != SENSEHAT_OK)
        return rc;
    if ((rc = wr(inst, REG_ENABLE, EN_PON)) != SENSEHAT_OK)
        return rc;
    if (inst->delay_us != NULL)
        inst->delay_us(3000u);                       /* at least 2.4 ms between PON and AEN */
    rc = wr(inst, REG_ENABLE, (uint8_t)(EN_PON | EN_AEN));
    if (rc != SENSEHAT_OK)
        return rc;
    return unknown_id ? TCS34725_ID_UNKNOWN : SENSEHAT_OK;
}

uint8_t tcs34725_id(void)
{
    return last_id;
}

int tcs34725_read(struct sensehat_efb_instance *inst, struct tcs34725_data *out)
{
    uint8_t st = 0, b[8];
    int32_t r, g, bl;
    int rc;

    if (out == NULL)
        return SENSEHAT_ERR_ARG;
    rc = rd(inst, REG_STATUS, &st, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    if (!(st & STATUS_AVALID))
        return TCS34725_ERR_NOT_READY;
    rc = rd(inst, REG_CDATAL, b, 8u);
    if (rc != SENSEHAT_OK)
        return rc;

    out->clear = (uint16_t)(((uint16_t)b[1] << 8) | b[0]);
    out->red   = (uint16_t)(((uint16_t)b[3] << 8) | b[2]);
    out->green = (uint16_t)(((uint16_t)b[5] << 8) | b[4]);
    out->blue  = (uint16_t)(((uint16_t)b[7] << 8) | b[6]);

    if (out->clear != 0u) {
        r  = ((int32_t)out->red   * 255) / out->clear;
        g  = ((int32_t)out->green * 255) / out->clear;
        bl = ((int32_t)out->blue  * 255) / out->clear;
        out->r8 = (uint8_t)(r  > 255 ? 255 : r);
        out->g8 = (uint8_t)(g  > 255 ? 255 : g);
        out->b8 = (uint8_t)(bl > 255 ? 255 : bl);
    } else {
        out->r8 = out->g8 = out->b8 = 0u;
    }

    /* DN40: lux = -0.32466 R + 1.57837 G - 0.73191 B, with the factors scaled by 256 */
    r  = (-83 * (int32_t)out->red + 404 * (int32_t)out->green - 187 * (int32_t)out->blue) / 256;
    out->lux = r < 0 ? 0 : r;
    return SENSEHAT_OK;
}

int tcs34725_power_down(struct sensehat_efb_instance *inst)
{
    return wr(inst, REG_ENABLE, 0u);
}
