/* Register map of the PWM_FFT block (custom:module:pwm_fft:1.2.0). */
#ifndef PWM_FFT_REGS_H
#define PWM_FFT_REGS_H

#include <stdint.h>

/* byte offsets from the base address */
#define PWM_FFT_REG_ID            0x000u   /* R : PWM_FFT_ID_VALUE */
#define PWM_FFT_REG_CTRL          0x004u   /* W : EN, WINDOW, DECAY */
#define PWM_FFT_REG_STATUS        0x004u   /* R : EN, WINDOW, DECAY, READY, BUSY, frame counter */
#define PWM_FFT_REG_SCALE         0x008u   /* RW: FLOOR, STEP */
#define PWM_FFT_REG_LEVELS_LO     0x00Cu   /* R : bands 1..8, 4 bits each; clears READY, captures LEVELS_HI */
#define PWM_FFT_REG_LEVELS_HI     0x010u   /* R : bands 9..12, 4 bits each */
#define PWM_FFT_REG_RAW(b)        (0x040u + 4u * (uint32_t)(b))   /* R: L = 4*log2(power), band b = 0..11 */

/* CTRL (write) and STATUS (read) at 0x004 */
#define PWM_FFT_CTRL_EN           (1u << 0)
#define PWM_FFT_CTRL_WINDOW       (1u << 1)
#define PWM_FFT_CTRL_DECAY_SHIFT  8u
#define PWM_FFT_CTRL_DECAY_MASK   (0xFu << PWM_FFT_CTRL_DECAY_SHIFT)
#define PWM_FFT_STATUS_READY      (1u << 12)
#define PWM_FFT_STATUS_BUSY       (1u << 13)
#define PWM_FFT_STATUS_FRAMES(v)  (((uint32_t)(v) >> 16) & 0xFFFFu)

/* SCALE */
#define PWM_FFT_SCALE_FLOOR_MASK  0xFFu
#define PWM_FFT_SCALE_STEP_SHIFT  8u
#define PWM_FFT_SCALE_STEP_MASK   (0x1Fu << PWM_FFT_SCALE_STEP_SHIFT)

#define PWM_FFT_ID_VALUE          0x50464654u   /* "PFFT" */

#endif /* PWM_FFT_REGS_H */
