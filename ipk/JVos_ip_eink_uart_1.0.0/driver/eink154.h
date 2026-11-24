/* eink154 - BSP driver for the Seeed Grove triple colour e-ink display, 1.54 inch, 152 x 152 pixels,
 *           black / red / white, UART interface.
 *
 * Project and documentation: https://github.com/javos65/XO4_Companion_Archive
 *
 * This driver is part of the BSP package "eink_uart" (src/bsp/driver/eink_uart), which collects all driver files
 * of the display link: eink_uart.h/.c (UART driver of the IP eink_uart), eink154.h/.c (this driver) and the font
 * files eink_font3x5.* and font_1bpp_5x7.*.
 *
 * ---------------------------------------------------------------------------------------------------------
 * WHAT THIS DRIVER DOES
 * ---------------------------------------------------------------------------------------------------------
 * The display takes a full frame over a UART (230400 baud, 8N1) as two 1-bit planes: first the black plane,
 * then the red plane, each EINK154_PLANE_BYTES (2888) bytes, row-major, 19 bytes per row, most significant
 * bit = leftmost pixel, 0 = ink, 1 = no ink. Red overrides black. There are no partial updates and a frame
 * takes about 25 s to send and 15 s to show; do not refresh more often than every EINK154_MIN_REFRESH_MS.
 *
 * Instead of two 2888 byte frame buffers the driver keeps a small SCREEN DESCRIPTION and renders the frame
 * while it is streamed, one chunk of 4 pixel rows (76 bytes) at a time. RAM needed for rendering: 76 bytes.
 *
 * A screen (eink154_screen_t) is a background colour plus two tables of POINTERS:
 *   - EINK154_GRAPHIC_COUNT graphics (eink154_graphic_t): bitmaps in the panel encoding (black + red bitmap)
 *   - EINK154_TEXT_COUNT text blocks (eink154_text_t): up to EINK154_TEXT_MAX characters, a colour, a font
 *     and a scale of 1 or 2
 * The application owns these objects (graphics as constant data, text blocks as variables) and the screen only
 * points to them, so a text block can be rewritten before every update. A null pointer, or an object with
 * enable = 0, is not drawn.
 *
 * Drawing order: background, graphics 0, 1 (opaque), then texts 0, 1, 2 ... (transparent).
 * eink154_screen_check() verifies the whole screen BEFORE anything is sent to the display: pointers, colours,
 * sizes, bounds, characters, fonts and a collision test of every pair of enabled objects. A screen with
 * objects that leave the 152 x 152 area or overlap is refused. eink154_screen_update() checks again.
 *
 * ---------------------------------------------------------------------------------------------------------
 * FONTS (included by this header)
 * ---------------------------------------------------------------------------------------------------------
 *   eink_font3x5.h    EINK154_FONT_3X5_ID (0)   3 x 5 pixels, cell 4 pixels wide. Small, default.
 *   font_1bpp_5x7.h   EINK154_FONT_5X7_ID (1)   5 x 7 pixels, cell 6 pixels wide. Readable.
 * Both cover ASCII 0x20 .. 0x5F (space, digits, upper case letters, punctuation; lower case is shown as upper
 * case). A text can be shown with scale 2 (every font pixel becomes 2 x 2 pixels). A new font is added by a
 * glyph array (one byte per pixel row, leftmost pixel in bit w-1) and one line in the font table in eink154.c.
 *
 * ---------------------------------------------------------------------------------------------------------
 * DISPLAY PROTOCOL (measured on the hardware)
 * ---------------------------------------------------------------------------------------------------------
 * At power-up the display prints "firmware version:V1.1" and runs its self-test. After an 'a' (0x61) it
 * answers with a 'b' (followed by the text "UART_recv_func()", repeated while it waits for data). The frame
 * is streamed right after the 'b': 2 x 2888 bytes in chunks of 76 bytes, followed by a pause of 80 ms after
 * every chunk. When all bytes have been received the display stops sending text and refreshes the panel.
 *
 * The display sends text at 230400 baud while it waits for data. The display UART is the APB UART with 16 word
 * FIFOs (driver eink_uart): a full receive FIFO keeps the OLDEST characters, so the 'b' that comes first is
 * never lost and the driver does not have to read the receiver while it transmits or waits.
 *
 * Like the other BSP drivers (fxls8971 on the I2C master, ...) the driver works on an instance that holds the
 * pointers to the blocks it uses: the display UART (struct eink_uart_instance, 230400 8N1, initialised by the
 * application with eink_uart_init() and eink_uart_set_baud()) and the machine timer (struct timer_instance,
 * time base of all waits).
 *   struct eink154_instance eink;
 *   eink154_init(&eink, &eink_uart, &mtimer_inst);
 * An optional hook function (eink154_set_hook()) is called in every loop of the driver while the display is
 * being served, for example to blink LEDs; eink154_is_busy() tells whether a frame is being sent.
 *
 * ---------------------------------------------------------------------------------------------------------
 * HOW TO USE IT
 * ---------------------------------------------------------------------------------------------------------
 *   // 1. objects, defined by the application
 *   static const eink154_graphic_t logo = { 1, 4, 6, LOGO_W, LOGO_H, logo_black, logo_red };
 *   static eink154_text_t title, tilt_x;
 *   static eink154_screen_t screen;
 *
 *   // 2. set up once
 *   eink154_screen_init(&screen, EINK154_WHITE);
 *   eink154_screen_set_graphic(&screen, 0, &logo);
 *   eink154_text_init(&title, 0, 30, EINK154_BLACK, 1, EINK154_FONT_5X7_ID);   // x, y, colour, scale, font
 *   eink154_text_set(&title, "HELLO");
 *   eink154_text_center(&title);                                               // optional: centre in x
 *   eink154_screen_set_text(&screen, 0, &title);
 *   eink154_text_init(&tilt_x, 0, 52, EINK154_RED, 2, EINK154_FONT_5X7_ID);
 *   eink154_screen_set_text(&screen, 1, &tilt_x);
 *
 *   // 3. the driver instance: display UART and machine timer (both initialised by the application)
 *   static struct eink154_instance eink;
 *   static struct eink_uart_instance eink_uart;
 *   eink_uart_init(&eink_uart, UART1_INST_BASE_ADDR);
 *   eink_uart_set_baud(&eink_uart, apb_clk_hz, 230400);
 *   eink154_init(&eink, &eink_uart, &mtimer_inst);
 *
 *   // 4. for every refresh: write the live values into the text blocks, check, send
 *   eink154_text_set_fixed(&tilt_x, "X:", tenths_of_degree, 1, 5, 1, " DEG");  // "X:+12.3 DEG", "X: -4.5 DEG"
 *   eink154_text_center(&tilt_x);
 *   eink154_check_t chk = eink154_screen_check(&screen);                        // up front: no collisions?
 *   if (chk.status == EINK154_OK) {
 *       eink154_stats_t stats;
 *       eink154_status_t rc = eink154_screen_update(&eink, &screen, &stats);
 *   }
 *
 * eink154_stats_t tells whether the 'b' arrived and how many characters the display sent in the
 * EINK154_END_DELAY_MS after the last byte (at most 16, the size of the receive FIFO, and an overflow flag): a
 * display that keeps sending text after the last byte points to a miscount of the bytes sent.
 * eink154_blocktest() sends a test pattern of black, red and white blocks (no screen structure needed).
 */
#ifndef EINK154_H
#define EINK154_H

#include <stdint.h>
#include "eink_uart.h"         /* struct eink_uart_instance: the display UART (APB UART with 16 word FIFOs) */
#include "timer.h"             /* struct timer_instance: machine timer, time base */
#include "eink_font3x5.h"      /* font 3 x 5 */
#include "font_1bpp_5x7.h"     /* font 5 x 7 */

/* ---- panel geometry */
#define EINK154_WIDTH             152
#define EINK154_HEIGHT            152
#define EINK154_ROW_BYTES         19          /* 152 / 8                                   */
#define EINK154_PLANE_BYTES       2888        /* 19 * 152                                  */
#define EINK154_CHUNK_BYTES       76          /* 4 rows, the chunk size of the display     */
#define EINK154_ROWS_PER_CHUNK    4
#define EINK154_CHUNKS_PER_PLANE  38

/* ---- configuration: can be defined before including this header */
#ifndef EINK154_TEXT_MAX
#define EINK154_TEXT_MAX          20          /* characters per text block                 */
#endif
#ifndef EINK154_TEXT_COUNT
#define EINK154_TEXT_COUNT        6           /* number of text blocks in a screen         */
#endif
#ifndef EINK154_GRAPHIC_COUNT
#define EINK154_GRAPHIC_COUNT     2           /* number of graphics in a screen            */
#endif

/* ---- protocol timing, measured on the hardware */
#define EINK154_HANDSHAKE_TIMEOUT_MS  30000u  /* give up waiting for the 'b' after this long */
#define EINK154_HANDSHAKE_RETRY_MS    0u      /* resend 'a' at this interval, 0 = send it once */
#define EINK154_START_DELAY_MS        0u      /* after the 'b'                             */
#define EINK154_CHUNK_DELAY_MS        80u     /* after every 76 byte chunk                 */
#define EINK154_PLANE_GAP_MS          0u      /* extra, between the black and red plane    */
#define EINK154_END_DELAY_MS          2000u   /* monitoring time after the last chunk      */
#define EINK154_MIN_REFRESH_MS        180000u /* minimum interval between two frames       */

typedef enum { EINK154_WHITE = 0, EINK154_BLACK = 1, EINK154_RED = 2 } eink154_color_t;

#define EINK154_PLANE_BLACK  0
#define EINK154_PLANE_RED    1

/* ---- fonts */
#define EINK154_FONT_3X5_ID  0        /* 3 x 5 pixels, cell 4 wide (default, value 0) */
#define EINK154_FONT_5X7_ID  1        /* 5 x 7 pixels, cell 6 wide                    */

/* One graphic: two bitmaps in the panel encoding (row-major, MSB first, 0 = ink, a row takes
 * (w + 7) / 8 bytes, padding bits are 1). The graphic is opaque: white pixels paint white. A red pixel
 * has a 0 in the red bitmap (and a 1 in the black bitmap). */
typedef struct {
    uint8_t        enable;      /* 1 = used, 0 = skipped                               */
    uint8_t        x, y;        /* top-left corner on the screen                       */
    uint8_t        w, h;        /* size in pixels                                      */
    const uint8_t *black;       /* ((w + 7) / 8) * h bytes                             */
    const uint8_t *red;         /* same layout                                         */
} eink154_graphic_t;

/* One text block of up to EINK154_TEXT_MAX characters, transparent. */
typedef struct {
    uint8_t enable;             /* 1 = used, 0 = skipped                               */
    uint8_t x, y;               /* top-left of the first character                     */
    uint8_t color;              /* eink154_color_t (white text is possible)            */
    uint8_t scale;              /* 1 or 2 (0 counts as 1): pixel size of the font      */
    uint8_t font;               /* EINK154_FONT_3X5_ID (0) or EINK154_FONT_5X7_ID (1)  */
    char    text[EINK154_TEXT_MAX + 1];
} eink154_text_t;

/* The screen: a background colour and two tables of pointers (null = not used). */
typedef struct {
    uint8_t                  background;                          /* eink154_color_t                 */
    const eink154_graphic_t *graphic[EINK154_GRAPHIC_COUNT];      /* drawn first, in this order      */
    const eink154_text_t    *text[EINK154_TEXT_COUNT];            /* drawn after the graphics        */
} eink154_screen_t;

/* ---- status codes */
typedef enum {
    EINK154_OK              = 0,
    EINK154_ERR_NULL        = 1,   /* null pointer (screen, bitmap, callback)             */
    EINK154_ERR_COLOR       = 2,   /* colour value is not white / black / red             */
    EINK154_ERR_GFX_SIZE    = 3,   /* graphic width or height is 0                        */
    EINK154_ERR_GFX_BOUNDS  = 4,   /* graphic leaves the 152 x 152 area                   */
    EINK154_ERR_TXT_LEN     = 5,   /* text is not terminated within EINK154_TEXT_MAX      */
    EINK154_ERR_TXT_CHAR    = 6,   /* a character is not in the font                      */
    EINK154_ERR_TXT_SCALE   = 7,   /* scale is larger than 2                              */
    EINK154_ERR_TXT_BOUNDS  = 8,   /* text leaves the 152 x 152 area                      */
    EINK154_ERR_OVERLAP     = 9,   /* two enabled objects overlap                         */
    EINK154_ERR_IO          = 10,  /* sending a byte failed                               */
    EINK154_ERR_TIMEOUT     = 11,  /* no 'b' from the display                             */
    EINK154_ERR_TOO_SOON    = 12,  /* minimum interval between two frames not reached     */
    EINK154_ERR_TXT_FONT    = 13,  /* unknown font id                                     */
    EINK154_ERR_INDEX       = 14   /* table index out of range                            */
} eink154_status_t;

/* object ids in eink154_check_t */
#define EINK154_OBJ_NONE     (-3)  /* no object                                           */
#define EINK154_OBJ_SCREEN   (-2)  /* the screen itself (background colour)               */
#define EINK154_OBJ_GRAPHIC(g) (-10 - (g))  /* graphic g: -10, -11, ...; texts are 0 .. EINK154_TEXT_COUNT - 1 */

typedef struct {
    int8_t status;              /* eink154_status_t                                    */
    int8_t obj_a;               /* offending object                                    */
    int8_t obj_b;               /* second object of an overlap, otherwise EINK154_OBJ_NONE */
} eink154_check_t;

/* ---- object setup */

/* Fills a graphic: enable = 1, position, size and the two bitmaps. */
void eink154_graphic_init(eink154_graphic_t *g, uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                          const uint8_t *black, const uint8_t *red);

/* Fills a text block: enable = 1, empty text, position (top-left), colour, scale (1 or 2) and font id. */
void eink154_text_init(eink154_text_t *t, uint8_t x, uint8_t y, uint8_t color, uint8_t scale, uint8_t font);

/* Clears the screen description (no graphics, no texts) and sets the background colour. */
void eink154_screen_init(eink154_screen_t *s, uint8_t background);

/* Puts a graphic / a text block into slot 'index' of the screen (null removes it). Returns EINK154_OK or
 * EINK154_ERR_INDEX / EINK154_ERR_NULL. */
eink154_status_t eink154_screen_set_graphic(eink154_screen_t *s, uint8_t index, const eink154_graphic_t *g);
eink154_status_t eink154_screen_set_text(eink154_screen_t *s, uint8_t index, const eink154_text_t *t);

/* ---- dynamic text: fill the text blocks with live values, without printf. A new text can change the box of
 * the block: eink154_screen_check() / eink154_screen_update() test the whole screen again. */

/* Copies s into the block, truncated to EINK154_TEXT_MAX characters. */
void eink154_text_set(eink154_text_t *t, const char *s);

/* Writes prefix + number + suffix into the block. The number is a fixed point value: value / 10^decimals, for
 * example value = -45, decimals = 1 gives "-4.5". It is right aligned in 'width' characters (sign and decimal
 * point included, padded with spaces on the left) so the text keeps its width when the value changes;
 * show_sign = 1 prints '+' for positive values. Returns 0, or -1 when the result had to be truncated.
 *   eink154_text_set_fixed(&t, "X:", tenths_of_degree, 1, 5, 1, " DEG")  -> "X:+12.0 DEG", "X: -4.5 DEG"
 *   eink154_text_set_fixed(&t, "X:", milli_g, 0, 5, 1, " MG")            -> "X: +207 MG" */
int eink154_text_set_fixed(eink154_text_t *t, const char *prefix, int32_t value, uint8_t decimals, uint8_t width,
                           uint8_t show_sign, const char *suffix);

/* Visible width in pixels of the current text (without trailing spaces and the gap after the last character). */
uint16_t eink154_text_width(const eink154_text_t *t);

/* Sets x so that the current text is centred horizontally on the panel. Call it after the text was set. */
void eink154_text_center(eink154_text_t *t);

/* ---- verification, rendering and streaming */

/* Checks the screen: pointers, colours, sizes, bounds, characters, fonts and collisions of every pair of
 * enabled objects. An object is the rectangle of the graphic or the cell box of a text (characters without
 * trailing spaces times the cell width of its font * scale pixels wide, font height * scale high). Touching
 * edges are not an overlap. */
eink154_check_t eink154_screen_check(const eink154_screen_t *s);

/* Short name of a status code (for messages). */
const char *eink154_status_str(int status);

/* Renders one pixel row (19 bytes) of a plane (EINK154_PLANE_BLACK or EINK154_PLANE_RED). */
void eink154_render_row(const eink154_screen_t *s, uint8_t plane, uint8_t y, uint8_t *line);

/* Renders one chunk of 4 rows (76 bytes) of a plane, band 0 .. 37. */
void eink154_render_band(const eink154_screen_t *s, uint8_t plane, uint8_t band, uint8_t *out);

/* Called for every chunk (38 per plane, black plane first). Return 0 to continue. */
typedef int (*eink154_sink_t)(void *ctx, const uint8_t *chunk, uint16_t len, uint8_t plane, uint8_t band);

/* Checks the screen, then renders the whole frame (2 x 38 chunks of 76 bytes) into the sink. */
eink154_status_t eink154_stream_frame(const eink154_screen_t *s, eink154_sink_t sink, void *ctx);

/* ---- the driver instance and the display protocol */

/* The instance of one display. The application defines it, the driver fills it in eink154_init(). */
struct eink154_instance {
    struct eink_uart_instance *uart;       /* display UART with FIFOs, 230400 8N1 (initialised by the application) */
    struct timer_instance *timer;          /* machine timer: time base                                    */
    void                 (*hook)(void *ctx);   /* optional: called in every loop while the display is served */
    void                  *hook_ctx;
    uint32_t               ticks_per_ms;   /* timer ticks per millisecond                                  */
    uint32_t               min_interval_ms;/* minimum time between two frames, default EINK154_MIN_REFRESH_MS, 0 = off */
    uint8_t                busy;           /* 1 while a frame is being sent                                */
    uint8_t                has_last;       /* a frame has been sent before                                 */
    unsigned long long     last_ticks;     /* machine timer value at the end of the last frame            */
};

/* Result of one update (optional). rx_after counts the characters the display sent during
 * EINK154_END_DELAY_MS after the last byte (at most 16: the size of the receive FIFO); rx_overflow = 1 when
 * there were more than the FIFO could hold. */
typedef struct {
    uint8_t  got_ack;           /* 1 = the 'b' arrived                                 */
    uint32_t rx_after;
    uint8_t  rx_overflow;
} eink154_stats_t;

/* Links the driver to the display UART (initialised with eink_uart_init() and eink_uart_set_baud(), 230400
 * 8N1) and to the machine timer (timer_init()). Returns EINK154_OK or EINK154_ERR_NULL. */
eink154_status_t eink154_init(struct eink154_instance *e, struct eink_uart_instance *uart,
                              struct timer_instance *timer);

/* Optional hook, called in every loop of the driver (transmit, waits, handshake) while the display is served. */
void eink154_set_hook(struct eink154_instance *e, void (*hook)(void *ctx), void *hook_ctx);

/* 1 while a frame is being sent (for a status LED). */
uint8_t eink154_is_busy(const struct eink154_instance *e);

/* Waits ms milliseconds (up to about 170000); the hook is called meanwhile. */
void eink154_wait_ms(struct eink154_instance *e, uint32_t ms);

/* Sends one byte (waits while the TX FIFO is full). */
void eink154_send_byte(struct eink154_instance *e, uint8_t b);

/* Handshake: flushes the receive FIFO, sends 'a', waits up to EINK154_HANDSHAKE_TIMEOUT_MS for the 'b'.
 * Returns EINK154_OK, EINK154_ERR_TIMEOUT or EINK154_ERR_NULL; stats->got_ack is set (stats may be NULL). */
eink154_status_t eink154_handshake(struct eink154_instance *e, eink154_stats_t *stats);

/* Sends the screen to the display: check, minimum interval, 'a' -> 'b' handshake, black plane, red plane
 * (76 byte chunks, 80 ms after each), then monitors the display for EINK154_END_DELAY_MS. stats may be NULL. */
eink154_status_t eink154_screen_update(struct eink154_instance *e, const eink154_screen_t *s, eink154_stats_t *stats);

/* Test pattern instead of a screen: handshake, then horizontal blocks of 8 rows, block k has the colour
 * (k + phase) % 3 with 0 white, 1 black, 2 red (phase 1 = black-red-white, 0 = white-black-red). */
eink154_status_t eink154_blocktest(struct eink154_instance *e, uint32_t phase, eink154_stats_t *stats);

#endif /* EINK154_H */
