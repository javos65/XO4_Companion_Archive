/* Driver of the memory-mapped TTS block (APB). */
#ifndef TTS_H
#define TTS_H

#include <stdint.h>
#include "tts_regs.h"

struct tts_instance {
    uint32_t base_address;
};

/* Binds the instance to the block's base address and checks the ID register.
 * Returns 0 on success, 1 on a null pointer, 2 if the ID register does not match. */
unsigned char tts_init(struct tts_instance *this_tts, uint32_t base_addr);

/* Reads the status register; the result is a combination of the TTS_STATUS_* bits. */
uint32_t read_tts_status(struct tts_instance *this_tts);

/* Writes the control register; value is a combination of the TTS_CTRL_* bits. */
void write_tts_control(struct tts_instance *this_tts, uint32_t value);

/* Writes length characters of buf into the block, one data register write per character.
 * The block ends a line at a carriage return or line feed, so the buffer carries its own terminator.
 * Returns the number of characters written. */
uint32_t write_tts_phrase(struct tts_instance *this_tts, const char *buf, uint32_t length);

#endif /* TTS_H */
