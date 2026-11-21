#include "tts.h"
#include "reg_access.h"
#include <stddef.h>

unsigned char tts_init(struct tts_instance *this_tts, uint32_t base_addr)
{
    uint32_t id;

    if (NULL == this_tts) {
        return 1;
    }
    this_tts->base_address = base_addr;
    reg_32b_read(base_addr + TTS_REG_ID, &id);
    return (id == TTS_ID_VALUE) ? 0 : 2;
}

uint32_t read_tts_status(struct tts_instance *this_tts)
{
    uint32_t status;

    reg_32b_read(this_tts->base_address + TTS_REG_STATUS, &status);
    return status;
}

void write_tts_control(struct tts_instance *this_tts, uint32_t value)
{
    reg_32b_write(this_tts->base_address + TTS_REG_CTRL, value);
}

uint32_t write_tts_phrase(struct tts_instance *this_tts, const char *buf, uint32_t length)
{
    uint32_t i;

    for (i = 0; i < length; i++) {
        reg_32b_write(this_tts->base_address + TTS_REG_DATA, (uint8_t)buf[i]);
    }
    return length;
}
