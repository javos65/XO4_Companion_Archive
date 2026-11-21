/* TCS3400 colour light sensor, BSP driver (see tcs3400.h). Register map: datasheet DS000411 v3-00. */
#include <stddef.h>
#include "tcs3400.h"

#define REG_ENABLE     0x80u    /* SAI[6] AIEN[4] WEN[3] AEN[1] PON[0] */
#define REG_ATIME      0x81u
#define REG_CONFIG     0x8Du    /* bit 6 reserved, must be written as 1 */
#define REG_CONTROL    0x8Fu    /* AGAIN[1:0] */
#define REG_ID         0x92u    /* ID[7:2] = 100100, VID[1:0] */
#define REG_STATUS     0x93u    /* ASAT[7] AINT[4] AVALID[0] */
#define REG_CDATAL     0x94u    /* C, R, G, B at 0x94, 0x96, 0x98, 0x9A: 16 bit little endian, read as pairs */

#define EN_PON         0x01u
#define EN_AEN         0x02u
#define STATUS_AVALID  0x01u
#define STATUS_ASAT    0x80u

static uint8_t last_id;

static int rd(struct sensehat_efb_instance *inst, uint8_t reg, uint8_t *buf, uint32_t n)
{
    return sensehat_i2c_write_read(inst, TCS3400_ADDR, &reg, 1u, buf, n);
}

static int wr(struct sensehat_efb_instance *inst, uint8_t reg, uint8_t val)
{
    return sensehat_i2c_write_block(inst, TCS3400_ADDR, reg, &val, 1u);
}

uint8_t tcs3400_id(void)
{
    return last_id;
}

int tcs3400_init(struct sensehat_efb_instance *inst, uint8_t atime, uint8_t gain)
{
    uint8_t id = 0;
    int rc = rd(inst, REG_ID, &id, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    last_id = id;
    if ((id & TCS3400_ID_MASK) != TCS3400_ID_VALUE)
        return TCS3400_ERR_ID;
    if ((rc = wr(inst, REG_ATIME, atime)) != SENSEHAT_OK)
        return rc;
    if ((rc = wr(inst, REG_CONFIG, 0x40u)) != SENSEHAT_OK)
        return rc;
    if ((rc = wr(inst, REG_CONTROL, (uint8_t)(gain & 0x03u))) != SENSEHAT_OK)
        return rc;
    if ((rc = wr(inst, REG_ENABLE, EN_PON)) != SENSEHAT_OK)
        return rc;
    if (inst->delay_us != NULL)
        inst->delay_us(3000u);                       /* oscillator start-up before the ADC is enabled */
    return wr(inst, REG_ENABLE, (uint8_t)(EN_PON | EN_AEN));
}

int tcs3400_read(struct sensehat_efb_instance *inst, struct tcs3400_data *out)
{
    uint8_t st = 0, p[2];
    uint16_t ch[4];
    int32_t v;
    int rc, i;

    if (out == NULL)
        return SENSEHAT_ERR_ARG;
    rc = rd(inst, REG_STATUS, &st, 1u);
    if (rc != SENSEHAT_OK)
        return rc;
    if (!(st & STATUS_AVALID))
        return TCS3400_ERR_NOT_READY;

    /* the data sheet requires byte pairs (low, then high) starting on an even address */
    for (i = 0; i < 4; i++) {
        rc = rd(inst, (uint8_t)(REG_CDATAL + 2 * i), p, 2u);
        if (rc != SENSEHAT_OK)
            return rc;
        ch[i] = (uint16_t)(((uint16_t)p[1] << 8) | p[0]);
    }
    out->clear = ch[0];
    out->red   = ch[1];
    out->green = ch[2];
    out->blue  = ch[3];
    out->saturated = (uint8_t)((st & STATUS_ASAT) ? 1u : 0u);

    if (ch[0] != 0u) {
        v = ((int32_t)ch[1] * 255) / ch[0]; out->r8 = (uint8_t)(v > 255 ? 255 : v);
        v = ((int32_t)ch[2] * 255) / ch[0]; out->g8 = (uint8_t)(v > 255 ? 255 : v);
        v = ((int32_t)ch[3] * 255) / ch[0]; out->b8 = (uint8_t)(v > 255 ? 255 : v);
    } else {
        out->r8 = out->g8 = out->b8 = 0u;
    }
    return SENSEHAT_OK;
}

int tcs3400_power_down(struct sensehat_efb_instance *inst)
{
    return wr(inst, REG_ENABLE, 0u);
}
