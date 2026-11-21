/* Register map of the memory-mapped TTS block (see ip/custom_module_tts_apb_1.0.0/rtl/tts_apb.v). */
#ifndef TTS_REGS_H
#define TTS_REGS_H

#define TTS_REG_ID        0x00u   /* read: 0x54545302 (TTS_APB 2.0.0) */
#define TTS_REG_CTRL      0x04u   /* write: control bits */
#define TTS_REG_STATUS    0x08u   /* read: status bits */
#define TTS_REG_DATA      0x0Cu   /* write: one text character; read: last character written */
#define TTS_LINE_MAX      128u    /* size of one line buffer of the block: a line ends itself at 128 characters */

#define TTS_ID_VALUE      0x54545302u

/* Control bits */
#define TTS_CTRL_START    0x01u   /* start speaking the completed line */
#define TTS_CTRL_CLEAR    0x02u   /* discard the queued text (ignored while busy) */

/* Status bits */
#define TTS_STATUS_BUSY   0x01u   /* speech and fade-out in progress */
#define TTS_STATUS_READY  0x02u   /* a complete line is waiting to be started */
#define TTS_STATUS_DONE   0x04u   /* last line finished (sticky) */
#define TTS_STATUS_PLAY   0x08u   /* audio is being played */
#define TTS_STATUS_DROP   0x10u   /* a line or start request was dropped (sticky) */

#endif /* TTS_REGS_H */
