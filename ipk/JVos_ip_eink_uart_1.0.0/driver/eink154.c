/* eink154 - BSP driver for the Seeed Grove triple colour e-ink display, 1.54 inch, 152 x 152 pixels.
 *
 * Project and documentation: https://github.com/javos65/XO4_Companion_Archive
 *
 * This file implements the driver described in eink154.h (read the description there first):
 *   - object setup and dynamic text (no printf): eink154_text_set(), eink154_text_set_fixed(),
 *     eink154_text_width(), eink154_text_center()
 *   - verification of the screen before anything is sent: eink154_screen_check() (bounds, characters, fonts,
 *     collision of every pair of objects)
 *   - the renderer: the frame is rendered row by row, 4 rows (76 bytes) per chunk, once per plane
 *   - the display protocol: 'a' -> 'b' handshake, then 2 x 2888 bytes in 76 byte chunks with an 80 ms pause.
 *     The driver works on an instance (struct eink154_instance) that points to the display UART (APB UART with
 *     16 word FIFOs, driver eink_uart) and the machine timer; an optional application hook is called in every
 *     loop (for example to blink LEDs).
 *
 * Structure of this file: font table, object setup, dynamic text, screen check, renderer, instance and protocol.
 */
#include "eink154.h"

typedef struct { int16_t x0, y0, x1, y1; } eink154_rect_t;   /* x1, y1 exclusive */

/* ------------------------------------------------------------------------- font table */
/* glyph data (one byte per pixel row, 1 = ink, leftmost pixel = bit w-1) and metrics of every font; the
 * index is the font id of the text block (EINK154_FONT_3X5_ID, EINK154_FONT_5X7_ID). A new font is added here. */
typedef struct {
    const uint8_t *data;
    uint8_t w, h, adv, first, last;
} eink154_font_t;

static const eink154_font_t fonts[] = {
    { eink_font3x5, EINK_FONT_W,     EINK_FONT_H,     EINK_FONT_ADV,     EINK_FONT_FIRST,     EINK_FONT_LAST     },
    { eink_font5x7, EINK5X7_FONT_W,  EINK5X7_FONT_H,  EINK5X7_FONT_ADV,  EINK5X7_FONT_FIRST,  EINK5X7_FONT_LAST  },
};
#define EINK154_FONTS_N  ((uint8_t)(sizeof(fonts) / sizeof(fonts[0])))

static const eink154_font_t *text_font(const eink154_text_t *t)
{
    return &fonts[(t->font < EINK154_FONTS_N) ? t->font : 0u];
}

/* index of a character in a font, -1 if the font has no glyph; lower case is shown as upper case */
static int font_index(const eink154_font_t *f, char ch)
{
    unsigned char c = (unsigned char)ch;

    if ((c >= 'a') && (c <= 'z')) {
        c = (unsigned char)(c - 'a' + 'A');
    }
    if ((c < f->first) || (c > f->last)) {
        return -1;
    }
    return (int)(c - f->first);
}

static uint8_t plane_color(uint8_t plane)
{
    return (plane == EINK154_PLANE_BLACK) ? (uint8_t)EINK154_BLACK : (uint8_t)EINK154_RED;
}

/* ------------------------------------------------------------------------- object setup */

void eink154_graphic_init(eink154_graphic_t *g, uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                          const uint8_t *black, const uint8_t *red)
{
    g->enable = 1u;
    g->x = x;
    g->y = y;
    g->w = w;
    g->h = h;
    g->black = black;
    g->red = red;
}

void eink154_text_init(eink154_text_t *t, uint8_t x, uint8_t y, uint8_t color, uint8_t scale, uint8_t font)
{
    t->enable = 1u;
    t->x = x;
    t->y = y;
    t->color = color;
    t->scale = scale;
    t->font = font;
    t->text[0] = '\0';
}

void eink154_screen_init(eink154_screen_t *s, uint8_t background)
{
    int i;

    s->background = background;
    for (i = 0; i < EINK154_GRAPHIC_COUNT; i++) {
        s->graphic[i] = 0;
    }
    for (i = 0; i < EINK154_TEXT_COUNT; i++) {
        s->text[i] = 0;
    }
}

eink154_status_t eink154_screen_set_graphic(eink154_screen_t *s, uint8_t index, const eink154_graphic_t *g)
{
    if (s == 0) {
        return EINK154_ERR_NULL;
    }
    if (index >= EINK154_GRAPHIC_COUNT) {
        return EINK154_ERR_INDEX;
    }
    s->graphic[index] = g;
    return EINK154_OK;
}

eink154_status_t eink154_screen_set_text(eink154_screen_t *s, uint8_t index, const eink154_text_t *t)
{
    if (s == 0) {
        return EINK154_ERR_NULL;
    }
    if (index >= EINK154_TEXT_COUNT) {
        return EINK154_ERR_INDEX;
    }
    s->text[index] = t;
    return EINK154_OK;
}

/* ------------------------------------------------------------------------- dynamic text */

/* appends s to the text block at *pos, returns 0 when something did not fit */
static int text_append(eink154_text_t *t, int *pos, const char *s)
{
    int ok = 1;

    while ((s != 0) && (*s != '\0')) {
        if (*pos >= EINK154_TEXT_MAX) {
            ok = 0;
            break;
        }
        t->text[(*pos)++] = *s++;
    }
    return ok;
}

void eink154_text_set(eink154_text_t *t, const char *s)
{
    int pos = 0;

    (void)text_append(t, &pos, s);
    t->text[pos] = '\0';
}

int eink154_text_set_fixed(eink154_text_t *t, const char *prefix, int32_t value, uint8_t decimals, uint8_t width,
                           uint8_t show_sign, const char *suffix)
{
    char num[16];                 /* built backwards: digits, decimal point, sign */
    char out[16];
    uint32_t mag = (value < 0) ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    int n = 0, len, i, pos = 0, ok;

    if (decimals > 9u) {
        decimals = 9u;
    }
    for (i = 0; ; i++) {                        /* at least decimals + 1 digits: 5 with 1 decimal -> 0.5 */
        if ((i == (int)decimals) && (decimals != 0u)) {
            num[n++] = '.';
        }
        num[n++] = (char)('0' + (int)(mag % 10u));
        mag /= 10u;
        if ((mag == 0u) && (i >= (int)decimals)) {
            break;
        }
    }
    if (value < 0) {
        num[n++] = '-';
    } else if (show_sign) {
        num[n++] = '+';
    }
    len = n;
    for (i = 0; i < len; i++) {                 /* reverse into out */
        out[i] = num[len - 1 - i];
    }
    out[len] = '\0';

    ok = text_append(t, &pos, prefix);
    for (i = len; i < (int)width; i++) {        /* right aligned: pad with spaces on the left */
        ok = text_append(t, &pos, " ") && ok;
    }
    ok = text_append(t, &pos, out) && ok;
    ok = text_append(t, &pos, suffix) && ok;
    t->text[pos] = '\0';
    return ok ? 0 : -1;
}

/* number of characters before the terminator, -1 if there is none within EINK154_TEXT_MAX + 1 bytes */
static int text_len(const char *t)
{
    int n = 0;

    while (n <= EINK154_TEXT_MAX) {
        if (t[n] == '\0') {
            return n;
        }
        n++;
    }
    return -1;
}

static uint8_t text_scale(const eink154_text_t *t)
{
    return (t->scale == 0u) ? 1u : t->scale;
}

/* box of a text without its trailing spaces; returns the number of characters in the box or -1 */
static int text_box(const eink154_text_t *t, eink154_rect_t *r)
{
    const eink154_font_t *f = text_font(t);
    int n = text_len(t->text);
    int sc = (int)text_scale(t);

    if (n < 0) {
        return -1;
    }
    while ((n > 0) && (t->text[n - 1] == ' ')) {
        n--;
    }
    r->x0 = (int16_t)t->x;
    r->y0 = (int16_t)t->y;
    r->x1 = (int16_t)((int)t->x + n * (int)f->adv * sc);
    r->y1 = (int16_t)((int)t->y + (int)f->h * sc);
    return n;
}

uint16_t eink154_text_width(const eink154_text_t *t)
{
    eink154_rect_t r;
    int n = text_box(t, &r);

    if (n <= 0) {
        return 0u;
    }
    return (uint16_t)((int)(r.x1 - r.x0) - (int)text_scale(t));   /* the cell includes a gap after the character */
}

void eink154_text_center(eink154_text_t *t)
{
    int w = (int)eink154_text_width(t);

    t->x = (w >= EINK154_WIDTH) ? 0u : (uint8_t)((EINK154_WIDTH - w) / 2);
}

/* ------------------------------------------------------------------------- screen check */

static int rect_overlap(const eink154_rect_t *a, const eink154_rect_t *b)
{
    return (a->x0 < b->x1) && (b->x0 < a->x1) && (a->y0 < b->y1) && (b->y0 < a->y1);
}

static eink154_check_t fail(eink154_status_t st, int a, int b)
{
    eink154_check_t r;

    r.status = (int8_t)st;
    r.obj_a = (int8_t)a;
    r.obj_b = (int8_t)b;
    return r;
}

eink154_check_t eink154_screen_check(const eink154_screen_t *s)
{
    /* boxes: [0 .. G-1] = graphics, [G + i] = text i; object ids: graphic g = EINK154_OBJ_GRAPHIC(g), text i = i */
    eink154_rect_t box[EINK154_GRAPHIC_COUNT + EINK154_TEXT_COUNT];
    uint8_t used[EINK154_GRAPHIC_COUNT + EINK154_TEXT_COUNT];
    int i, j, k, n;

    if (s == 0) {
        return fail(EINK154_ERR_NULL, EINK154_OBJ_SCREEN, EINK154_OBJ_NONE);
    }
    if (s->background > EINK154_RED) {
        return fail(EINK154_ERR_COLOR, EINK154_OBJ_SCREEN, EINK154_OBJ_NONE);
    }
    for (i = 0; i < EINK154_GRAPHIC_COUNT + EINK154_TEXT_COUNT; i++) {
        used[i] = 0;
    }

    for (i = 0; i < EINK154_GRAPHIC_COUNT; i++) {
        const eink154_graphic_t *g = s->graphic[i];

        if ((g == 0) || !g->enable) {
            continue;
        }
        if ((g->black == 0) || (g->red == 0)) {
            return fail(EINK154_ERR_NULL, EINK154_OBJ_GRAPHIC(i), EINK154_OBJ_NONE);
        }
        if ((g->w == 0u) || (g->h == 0u)) {
            return fail(EINK154_ERR_GFX_SIZE, EINK154_OBJ_GRAPHIC(i), EINK154_OBJ_NONE);
        }
        if (((int)g->x + (int)g->w > EINK154_WIDTH) || ((int)g->y + (int)g->h > EINK154_HEIGHT)) {
            return fail(EINK154_ERR_GFX_BOUNDS, EINK154_OBJ_GRAPHIC(i), EINK154_OBJ_NONE);
        }
        box[i].x0 = (int16_t)g->x;
        box[i].y0 = (int16_t)g->y;
        box[i].x1 = (int16_t)((int)g->x + (int)g->w);
        box[i].y1 = (int16_t)((int)g->y + (int)g->h);
        used[i] = 1;
    }

    for (i = 0; i < EINK154_TEXT_COUNT; i++) {
        const eink154_text_t *t = s->text[i];

        if ((t == 0) || !t->enable) {
            continue;
        }
        if (t->color > EINK154_RED) {
            return fail(EINK154_ERR_COLOR, i, EINK154_OBJ_NONE);
        }
        if (t->scale > 2u) {
            return fail(EINK154_ERR_TXT_SCALE, i, EINK154_OBJ_NONE);
        }
        if (t->font >= EINK154_FONTS_N) {
            return fail(EINK154_ERR_TXT_FONT, i, EINK154_OBJ_NONE);
        }
        n = text_box(t, &box[EINK154_GRAPHIC_COUNT + i]);
        if (n < 0) {
            return fail(EINK154_ERR_TXT_LEN, i, EINK154_OBJ_NONE);
        }
        for (k = 0; t->text[k] != '\0'; k++) {
            if (font_index(text_font(t), t->text[k]) < 0) {
                return fail(EINK154_ERR_TXT_CHAR, i, EINK154_OBJ_NONE);
            }
        }
        if (n > 0) {
            if ((box[EINK154_GRAPHIC_COUNT + i].x1 > EINK154_WIDTH) ||
                (box[EINK154_GRAPHIC_COUNT + i].y1 > EINK154_HEIGHT)) {
                return fail(EINK154_ERR_TXT_BOUNDS, i, EINK154_OBJ_NONE);
            }
            used[EINK154_GRAPHIC_COUNT + i] = 1;
        }
    }

    /* collision test of every pair of enabled objects (graphics and texts) */
    for (i = 0; i < EINK154_GRAPHIC_COUNT + EINK154_TEXT_COUNT; i++) {
        for (j = i + 1; j < EINK154_GRAPHIC_COUNT + EINK154_TEXT_COUNT; j++) {
            if (used[i] && used[j] && rect_overlap(&box[i], &box[j])) {
                return fail(EINK154_ERR_OVERLAP,
                            (i < EINK154_GRAPHIC_COUNT) ? EINK154_OBJ_GRAPHIC(i) : (i - EINK154_GRAPHIC_COUNT),
                            (j < EINK154_GRAPHIC_COUNT) ? EINK154_OBJ_GRAPHIC(j) : (j - EINK154_GRAPHIC_COUNT));
            }
        }
    }
    return fail(EINK154_OK, EINK154_OBJ_NONE, EINK154_OBJ_NONE);
}

const char *eink154_status_str(int status)
{
    static const char *const names[] = {
        "ok", "null pointer", "colour", "graphic size", "graphic bounds", "text length", "text character",
        "text scale", "text bounds", "overlap", "io", "no 'b' from the display", "too soon", "text font",
        "index"
    };

    return ((status >= 0) && (status < (int)(sizeof(names) / sizeof(names[0])))) ? names[status] : "?";
}

/* ------------------------------------------------------------------------- rendering */

/* ink pixel of a colour in one plane: ink (0) in the plane of that colour, no ink (1) in the other */
static void put_ink(uint8_t *line, int x, uint8_t plane, uint8_t color)
{
    uint8_t m;

    if ((x < 0) || (x >= EINK154_WIDTH)) {
        return;
    }
    m = (uint8_t)(0x80u >> (x & 7));
    if (color == plane_color(plane)) {
        line[x >> 3] &= (uint8_t)~m;
    } else {
        line[x >> 3] |= m;
    }
}

static void draw_graphic_row(const eink154_graphic_t *g, uint8_t plane, uint8_t y, uint8_t *line)
{
    const uint8_t *src;
    uint32_t stride;
    uint8_t px;

    if ((y < g->y) || ((int)y >= (int)g->y + (int)g->h)) {
        return;
    }
    stride = ((uint32_t)g->w + 7u) >> 3;
    src = ((plane == EINK154_PLANE_BLACK) ? g->black : g->red) + (uint32_t)(y - g->y) * stride;
    for (px = 0; px < g->w; px++) {
        int x = (int)g->x + (int)px;
        uint8_t m;

        if (x >= EINK154_WIDTH) {
            break;
        }
        m = (uint8_t)(0x80u >> (x & 7));
        if (src[px >> 3] & (0x80u >> (px & 7))) {
            line[x >> 3] |= m;                    /* no ink */
        } else {
            line[x >> 3] &= (uint8_t)~m;          /* ink    */
        }
    }
}

static void draw_text_row(const eink154_text_t *t, uint8_t plane, uint8_t y, uint8_t *line)
{
    const eink154_font_t *f = text_font(t);
    int sc = (int)text_scale(t);
    int dy = (int)y - (int)t->y;
    int gr, x, k, gx, rep, idx;
    const char *p;

    if ((dy < 0) || (dy >= (int)f->h * sc)) {
        return;
    }
    gr = dy / sc;
    x = (int)t->x;
    for (p = t->text, k = 0; (*p != '\0') && (k < EINK154_TEXT_MAX); p++, k++) {
        idx = font_index(f, *p);
        if (idx >= 0) {
            uint8_t bits = f->data[idx * (int)f->h + gr];

            for (gx = 0; gx < (int)f->w; gx++) {
                if (bits & (1u << ((int)f->w - 1 - gx))) {
                    for (rep = 0; rep < sc; rep++) {
                        put_ink(line, x + gx * sc + rep, plane, t->color);
                    }
                }
            }
        }
        x += (int)f->adv * sc;
    }
}

void eink154_render_row(const eink154_screen_t *s, uint8_t plane, uint8_t y, uint8_t *line)
{
    uint8_t fill = (s->background == plane_color(plane)) ? 0x00u : 0xFFu;
    int i;

    for (i = 0; i < EINK154_ROW_BYTES; i++) {
        line[i] = fill;
    }
    for (i = 0; i < EINK154_GRAPHIC_COUNT; i++) {
        if ((s->graphic[i] != 0) && s->graphic[i]->enable) {
            draw_graphic_row(s->graphic[i], plane, y, line);
        }
    }
    for (i = 0; i < EINK154_TEXT_COUNT; i++) {
        if ((s->text[i] != 0) && s->text[i]->enable) {
            draw_text_row(s->text[i], plane, y, line);
        }
    }
}

void eink154_render_band(const eink154_screen_t *s, uint8_t plane, uint8_t band, uint8_t *out)
{
    uint8_t r;

    for (r = 0; r < EINK154_ROWS_PER_CHUNK; r++) {
        eink154_render_row(s, plane, (uint8_t)(band * EINK154_ROWS_PER_CHUNK + r),
                           out + (uint32_t)r * EINK154_ROW_BYTES);
    }
}

eink154_status_t eink154_stream_frame(const eink154_screen_t *s, eink154_sink_t sink, void *ctx)
{
    uint8_t chunk[EINK154_CHUNK_BYTES];
    uint8_t plane, band;
    eink154_check_t c = eink154_screen_check(s);

    if (c.status != EINK154_OK) {
        return (eink154_status_t)c.status;
    }
    if (sink == 0) {
        return EINK154_ERR_NULL;
    }
    for (plane = 0; plane < 2u; plane++) {
        for (band = 0; band < EINK154_CHUNKS_PER_PLANE; band++) {
            eink154_render_band(s, plane, band, chunk);
            if (sink(ctx, chunk, EINK154_CHUNK_BYTES, plane, band) != 0) {
                return EINK154_ERR_IO;
            }
        }
    }
    return EINK154_OK;
}

/* ------------------------------------------------------------------------- display protocol */

/* low level: everything goes through the instance (display UART with FIFOs + machine timer). The receive FIFO
 * keeps the oldest characters, so nothing has to be read while transmitting or waiting; the application hook
 * is called in every loop. */

static uint32_t ticks(const struct eink154_instance *e)
{
    unsigned long long t = 0;

    timer_get_mtime(e->timer, &t);
    return (uint32_t)t;
}

static void call_hook(const struct eink154_instance *e)
{
    if (e->hook != 0) {
        e->hook(e->hook_ctx);
    }
}

eink154_status_t eink154_init(struct eink154_instance *e, struct eink_uart_instance *uart,
                              struct timer_instance *timer)
{
    if ((e == 0) || (uart == 0) || (timer == 0)) {
        return EINK154_ERR_NULL;
    }
    e->uart = uart;
    e->timer = timer;
    e->hook = 0;
    e->hook_ctx = 0;
    e->ticks_per_ms = timer->cpu_freq / 1000u;
    e->min_interval_ms = EINK154_MIN_REFRESH_MS;
    e->busy = 0;
    e->has_last = 0;
    e->last_ticks = 0;
    eink_uart_flush_rx(uart);
    eink_uart_clear_flags(uart);
    return EINK154_OK;
}

void eink154_set_hook(struct eink154_instance *e, void (*hook)(void *ctx), void *hook_ctx)
{
    e->hook = hook;
    e->hook_ctx = hook_ctx;
}

uint8_t eink154_is_busy(const struct eink154_instance *e)
{
    return e->busy;
}

void eink154_wait_ms(struct eink154_instance *e, uint32_t ms)
{
    uint32_t t0 = ticks(e), now;
    uint32_t span = ms * e->ticks_per_ms;

    do {
        call_hook(e);
        now = ticks(e);
    } while ((uint32_t)(now - t0) < span);
}

void eink154_send_byte(struct eink154_instance *e, uint8_t b)
{
    eink_uart_putc(e->uart, b);            /* waits while the TX FIFO is full */
    call_hook(e);
}

/* after the last byte: empties the receive FIFO, waits EINK154_END_DELAY_MS and reports what the display sent */
static void monitor_after(struct eink154_instance *e, eink154_stats_t *stats)
{
    eink_uart_flush_rx(e->uart);
    eink_uart_clear_flags(e->uart);
    eink154_wait_ms(e, EINK154_END_DELAY_MS);
    if (stats != 0) {
        stats->rx_after = eink_uart_rx_count(e->uart);
        stats->rx_overflow = (eink_uart_status(e->uart) & EINK_UART_ST_RX_OVERRUN) ? 1u : 0u;
    }
}

eink154_status_t eink154_handshake(struct eink154_instance *e, eink154_stats_t *stats)
{
    uint8_t c;
    uint32_t t0, t_a, idle = 0;
    uint32_t timeout = EINK154_HANDSHAKE_TIMEOUT_MS * e->ticks_per_ms;
    uint32_t retry = EINK154_HANDSHAKE_RETRY_MS * e->ticks_per_ms;

    if (stats != 0) {
        stats->got_ack = 0;
    }
    eink_uart_flush_rx(e->uart);             /* drop stale characters */
    eink_uart_clear_flags(e->uart);
    eink154_send_byte(e, (uint8_t)'a');
    t0 = ticks(e);
    t_a = t0;
    /* Wait for the 'b': the first character the display sends, kept by the FIFO. Look at the clock only every
     * 16th pass without a character. */
    for (;;) {
        if (0u == eink_uart_getc(e->uart, &c)) {
            if (c == (uint8_t)'b') {
                if (stats != 0) {
                    stats->got_ack = 1;
                }
                return EINK154_OK;
            }
        } else if ((++idle & 15u) == 0u) {
            uint32_t now = ticks(e);

            call_hook(e);
            if ((uint32_t)(now - t0) >= timeout) {
                return EINK154_ERR_TIMEOUT;                 /* no 'b' */
            }
            if ((retry != 0u) && ((uint32_t)(now - t_a) >= retry)) {
                eink154_send_byte(e, (uint8_t)'a');
                t_a = now;
            }
        }
    }
}

/* sink of the renderer: one chunk of 76 bytes to the display, then the 80 ms pause */
static int io_sink(void *ctx, const uint8_t *chunk, uint16_t len, uint8_t plane, uint8_t band)
{
    struct eink154_instance *e = (struct eink154_instance *)ctx;
    uint16_t i;

    for (i = 0; i < len; i++) {
        eink154_send_byte(e, chunk[i]);
    }
    eink154_wait_ms(e, EINK154_CHUNK_DELAY_MS);
    if ((EINK154_PLANE_GAP_MS != 0u) && (plane == EINK154_PLANE_BLACK) && (band == EINK154_CHUNKS_PER_PLANE - 1)) {
        eink154_wait_ms(e, EINK154_PLANE_GAP_MS);
    }
    return 0;
}

eink154_status_t eink154_screen_update(struct eink154_instance *e, const eink154_screen_t *s, eink154_stats_t *stats)
{
    eink154_check_t c;
    eink154_status_t st;

    if (stats != 0) {
        stats->got_ack = 0;
        stats->rx_after = 0;
        stats->rx_overflow = 0;
    }
    if ((e == 0) || (e->uart == 0) || (e->timer == 0)) {
        return EINK154_ERR_NULL;
    }
    c = eink154_screen_check(s);                    /* verified up front, before anything is sent */
    if (c.status != EINK154_OK) {
        return (eink154_status_t)c.status;
    }
    if ((e->min_interval_ms != 0u) && e->has_last) {
        unsigned long long now = 0;

        timer_get_mtime(e->timer, &now);
        if ((now - e->last_ticks) < ((unsigned long long)e->min_interval_ms * e->ticks_per_ms)) {
            return EINK154_ERR_TOO_SOON;
        }
    }

    e->busy = 1;
    st = eink154_handshake(e, stats);
    if (st != EINK154_OK) {
        e->busy = 0;
        return st;
    }
    if (EINK154_START_DELAY_MS != 0u) {
        eink154_wait_ms(e, EINK154_START_DELAY_MS);
    }

    st = eink154_stream_frame(s, io_sink, e);
    if (st != EINK154_OK) {
        e->busy = 0;
        return st;
    }
    monitor_after(e, stats);                         /* does the display keep sending? */
    timer_get_mtime(e->timer, &e->last_ticks);
    e->has_last = 1;
    e->busy = 0;
    return EINK154_OK;
}

eink154_status_t eink154_blocktest(struct eink154_instance *e, uint32_t phase, eink154_stats_t *stats)
{
    eink154_status_t st;
    uint32_t plane, piece, i;

    if (stats != 0) {
        stats->got_ack = 0;
        stats->rx_after = 0;
        stats->rx_overflow = 0;
    }
    if ((e == 0) || (e->uart == 0) || (e->timer == 0)) {
        return EINK154_ERR_NULL;
    }
    e->busy = 1;
    st = eink154_handshake(e, stats);
    if (st != EINK154_OK) {
        e->busy = 0;
        return st;
    }
    for (plane = 0; plane < 2u; plane++) {              /* black plane first, then red */
        for (piece = 0; piece < EINK154_CHUNKS_PER_PLANE; piece++) {
            uint8_t col = (uint8_t)(((piece / 2u) + phase) % 3u);      /* 8 rows = 2 chunks per block */
            uint8_t v = (plane == 0u) ? ((col == 1u) ? 0x00u : 0xFFu) : ((col == 2u) ? 0x00u : 0xFFu);

            for (i = 0; i < EINK154_CHUNK_BYTES; i++) {
                eink154_send_byte(e, v);
            }
            eink154_wait_ms(e, EINK154_CHUNK_DELAY_MS);
        }
    }
    monitor_after(e, stats);
    e->busy = 0;
    return EINK154_OK;
}
