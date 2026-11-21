#include "pwm_fft.h"
#include "reg_access.h"
#include <stddef.h>

uint8_t pwm_fft_init(struct pwm_fft_instance *this_fft, uint32_t base_addr)
{
    uint32_t v = 0;

    if (NULL == this_fft) {
        return PWM_FFT_ERR_PARAM;
    }
    this_fft->base_address = base_addr;
    reg_32b_read(base_addr + PWM_FFT_REG_ID, &v);
    if (PWM_FFT_ID_VALUE != v) {
        return PWM_FFT_ERR_ID;
    }
    return PWM_FFT_OK;
}

void pwm_fft_config(struct pwm_fft_instance *this_fft, uint8_t enable, uint8_t window, uint8_t decay)
{
    uint32_t v;

    if (NULL == this_fft) {
        return;
    }
    v = (enable ? PWM_FFT_CTRL_EN : 0u)
      | (window ? PWM_FFT_CTRL_WINDOW : 0u)
      | (((uint32_t)decay << PWM_FFT_CTRL_DECAY_SHIFT) & PWM_FFT_CTRL_DECAY_MASK);
    reg_32b_write(this_fft->base_address + PWM_FFT_REG_CTRL, v);
}

void pwm_fft_set_scale(struct pwm_fft_instance *this_fft, uint8_t floor, uint8_t step)
{
    uint32_t v;

    if (NULL == this_fft) {
        return;
    }
    v = ((uint32_t)floor & PWM_FFT_SCALE_FLOOR_MASK)
      | (((uint32_t)step << PWM_FFT_SCALE_STEP_SHIFT) & PWM_FFT_SCALE_STEP_MASK);
    reg_32b_write(this_fft->base_address + PWM_FFT_REG_SCALE, v);
}

uint8_t pwm_fft_ready(struct pwm_fft_instance *this_fft)
{
    uint32_t v = 0;

    if (NULL == this_fft) {
        return 0u;
    }
    reg_32b_read(this_fft->base_address + PWM_FFT_REG_STATUS, &v);
    return (v & PWM_FFT_STATUS_READY) ? 1u : 0u;
}

void pwm_fft_read_packed(struct pwm_fft_instance *this_fft, uint32_t *lo, uint32_t *hi)
{
    uint32_t l = 0, h = 0;

    if (NULL == this_fft) {
        return;
    }
    reg_32b_read(this_fft->base_address + PWM_FFT_REG_LEVELS_LO, &l);   /* captures LEVELS_HI */
    reg_32b_read(this_fft->base_address + PWM_FFT_REG_LEVELS_HI, &h);
    if (NULL != lo) {
        *lo = l;
    }
    if (NULL != hi) {
        *hi = h;
    }
}

void pwm_fft_read_levels(struct pwm_fft_instance *this_fft, uint8_t levels[PWM_FFT_NUM_BANDS])
{
    uint32_t lo = 0, hi = 0;
    uint32_t b;

    if ((NULL == this_fft) || (NULL == levels)) {
        return;
    }
    pwm_fft_read_packed(this_fft, &lo, &hi);
    for (b = 0; b < 8u; b++) {
        levels[b] = (uint8_t)((lo >> (4u * b)) & 0xFu);
    }
    for (b = 0; b < 4u; b++) {
        levels[8u + b] = (uint8_t)((hi >> (4u * b)) & 0xFu);
    }
}

uint8_t pwm_fft_read_raw(struct pwm_fft_instance *this_fft, uint8_t band)
{
    uint32_t v = 0;

    if ((NULL == this_fft) || (band >= PWM_FFT_NUM_BANDS)) {
        return 0u;
    }
    reg_32b_read(this_fft->base_address + PWM_FFT_REG_RAW(band), &v);
    return (uint8_t)(v & 0xFFu);
}
