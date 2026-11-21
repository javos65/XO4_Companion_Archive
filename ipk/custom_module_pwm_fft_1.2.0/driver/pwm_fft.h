/* Driver of the PWM_FFT block (custom:module:pwm_fft:1.2.0): 12-band audio spectrum of a
 * PWM audio signal (16 kHz samples, 256-point FFT, 62.5 Hz per bin), levels 0..7 per band.
 *
 * Bands (FFT bins, band 1 = lowest): 1-7 (50-500 Hz), then 11 log bands (factor 1.29) to 8 kHz:
 *   8-10, 11-13, 14-17, 18-21, 22-28, 29-36, 37-46, 47-60, 61-77, 78-99, 100-127.
 * Bins per band: 7, 3, 3, 4, 4, 7, 8, 10, 14, 17, 22, 28.
 * A new set of levels is published every 256 samples (16 ms).
 */
#ifndef PWM_FFT_H
#define PWM_FFT_H

#include <stdint.h>
#include "pwm_fft_regs.h"

#define PWM_FFT_NUM_BANDS     12u
#define PWM_FFT_LEVEL_MAX     7u

/* return codes */
#define PWM_FFT_OK            0
#define PWM_FFT_ERR_PARAM     1   /* null pointer or bad argument */
#define PWM_FFT_ERR_ID        2   /* ID register does not match: block not found */

struct pwm_fft_instance {
    const char *instance_name;
    uint32_t    base_address;
};

/* Binds the instance to the base address and checks the ID register.
 * Returns PWM_FFT_OK, PWM_FFT_ERR_PARAM or PWM_FFT_ERR_ID. */
uint8_t pwm_fft_init(struct pwm_fft_instance *this_fft, uint32_t base_addr);

/* CTRL: enable, Hann window on/off, decay (frames per falling level step, 0..15, 0 = follow). */
void pwm_fft_config(struct pwm_fft_instance *this_fft, uint8_t enable, uint8_t window, uint8_t decay);

/* SCALE: floor and step in quarter log2 steps (step 8 = about 6 dB per level). */
void pwm_fft_set_scale(struct pwm_fft_instance *this_fft, uint8_t floor, uint8_t step);

/* 1 when new levels were published since the last pwm_fft_read_levels(). */
uint8_t pwm_fft_ready(struct pwm_fft_instance *this_fft);

/* Reads the 12 levels of one frame (LEVELS_LO, then LEVELS_HI) into levels[0..11]
 * (levels[0] = band 1). Clears READY. */
void pwm_fft_read_levels(struct pwm_fft_instance *this_fft, uint8_t levels[PWM_FFT_NUM_BANDS]);

/* Raw packed words: lo = bands 1..8, hi = bands 9..12 (4 bits per band). Clears READY. */
void pwm_fft_read_packed(struct pwm_fft_instance *this_fft, uint32_t *lo, uint32_t *hi);

/* L = 4 * log2(band power) of band b (0..11), for tuning FLOOR and STEP. */
uint8_t pwm_fft_read_raw(struct pwm_fft_instance *this_fft, uint8_t band);

#endif /* PWM_FFT_H */
