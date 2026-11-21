// MKR_RGB_SPI 1.0.0: APB controller for a daisy chain of APA102 / APA102-2020 RGB LEDs
// (Arduino MKR RGB Shield: 84 LEDs, 7 rows x 12 columns).
//
// One clock (PCLK). The serial clock CKI runs at f_PCLK / 2, so PCLK = 20 MHz gives
// CKI = 10 MHz. SDI changes together with the falling edge of CKI and is sampled
// by the LED on the rising edge (half a CKI period of setup and hold).
//
// Software fills the pixel buffer, then writes CTRL.START. The block sends one frame:
//     start frame : 32 x '0'
//     LED frames  : NUM_LEDS x 32 bits, PIX[0] first (first LED in the chain)
//     end frame   : 64 x '1'   (>= NUM_LEDS/2 extra clocks, valid up to 127 LEDs)
// A frame of 84 LEDs is 2784 CKI clocks = 278.4 us at 10 MHz.
//
// Each PIX word is sent MSB first, exactly as written. APA102 LED frame layout:
//     [31:29] 3'b111   [28:24] global brightness 0..31   [23:16] blue   [15:8] green   [7:0] red
//
// Register map (byte offsets, 1 KB window, PADDR[9:2] = word index):
//   0x000      ID     R  0x52474231 ("RGB1")
//   0x004      CTRL   bit0 START  W: 1 starts a frame (ignored while busy); reads 0
//                     bit1 READY  R: 1 = idle, buffer may be written / read
//   0x008      NLEDS  R  NUM_LEDS
//   0x00C..    PIX[0..NUM_LEDS-1]  RW 32-bit LED frame words (0x00C..0x158 for 84 LEDs)
//
// The pixel buffer is one block RAM (pseudo dual port). While a frame is being sent
// (READY = 0) the RAM read port belongs to the serial engine: PIX writes are ignored
// and PIX reads return undefined data. Wait for READY before touching the buffer.
module mkr_rgb_spi #(
    parameter integer NUM_LEDS = 84          // 1..125
) (
    input  wire        PCLK,
    input  wire        PRESETN,
    input  wire [9:0]  PADDR,
    input  wire        PSEL,
    input  wire        PENABLE,
    input  wire        PWRITE,
    input  wire [31:0] PWDATA,
    output reg  [31:0] PRDATA,
    output wire        PREADY,
    output wire        PSLVERR,
    output reg         LED_SDI,
    output reg         LED_CKI
);
    localparam [31:0] ID_VALUE  = 32'h5247_4231;
    localparam [7:0]  W_ID      = 8'h00;      // byte 0x000
    localparam [7:0]  W_CTRL    = 8'h01;      // byte 0x004
    localparam [7:0]  W_NLEDS   = 8'h02;      // byte 0x008
    localparam [7:0]  W_PIX0    = 8'h03;      // byte 0x00C: PIX[0]
    localparam [6:0]  LAST_WORD = NUM_LEDS + 2;   // words: 0 start, 1..N LEDs, N+1..N+2 end

    assign PREADY  = 1'b1;
    assign PSLVERR = 1'b0;

    // ------------------------------------------------------------ APB decode
    wire [7:0] widx    = PADDR[9:2];
    wire       is_pix  = (widx >= W_PIX0) && (widx < W_PIX0 + NUM_LEDS);
    wire [7:0] pix_off = widx - W_PIX0;
    wire [6:0] pix_idx = pix_off[6:0];
    wire       wr      = PSEL & PENABLE & PWRITE;

    // ------------------------------------------------------------ engine state
    reg        busy;
    reg [6:0]  wcnt;        // word being shifted out (0..LAST_WORD)
    reg [4:0]  bcnt;        // bits left in the word - 1
    reg        ph;          // 0: drive SDI with CKI low, 1: CKI high
    reg [31:0] sh;          // shift register, MSB goes out
    reg [6:0]  eng_addr;    // RAM address of the next LED word

    wire go = wr && (widx == W_CTRL) && PWDATA[0] && !busy;

    // ------------------------------------------------------------ pixel RAM
    reg [31:0] mem [0:127];
    reg [31:0] mem_q;
    wire [6:0] raddr = busy ? eng_addr : pix_idx;

    always @(posedge PCLK) begin
        if (wr && is_pix && !busy)
            mem[pix_idx] <= PWDATA;
        mem_q <= mem[raddr];    // APB: address captured in the setup phase, data valid in the access phase
    end

    // ------------------------------------------------------------ read data
    always @(*) begin
        if (is_pix)
            PRDATA = mem_q;
        else case (widx)
            W_CTRL:  PRDATA = {30'd0, ~busy, 1'b0};
            W_ID:    PRDATA = ID_VALUE;
            W_NLEDS: PRDATA = NUM_LEDS;
            default: PRDATA = 32'd0;
        endcase
    end

    // ------------------------------------------------------------ serial engine
    wire [6:0]  next_w    = wcnt + 7'd1;
    wire [31:0] next_word = (next_w <= NUM_LEDS) ? mem_q : 32'hFFFF_FFFF;

    always @(posedge PCLK or negedge PRESETN) begin
        if (!PRESETN) begin
            busy     <= 1'b0;
            wcnt     <= 7'd0;
            bcnt     <= 5'd0;
            ph       <= 1'b0;
            sh       <= 32'd0;
            eng_addr <= 7'd0;
            LED_SDI  <= 1'b0;
            LED_CKI  <= 1'b0;
        end else if (!busy) begin
            LED_SDI <= 1'b0;
            LED_CKI <= 1'b0;
            if (go) begin
                busy     <= 1'b1;
                wcnt     <= 7'd0;
                bcnt     <= 5'd31;
                ph       <= 1'b0;
                sh       <= 32'd0;          // start frame
                eng_addr <= 7'd0;           // RAM read of PIX[0] starts now
            end
        end else if (!ph) begin
            LED_SDI <= sh[31];
            LED_CKI <= 1'b0;
            ph      <= 1'b1;
        end else begin
            LED_CKI <= 1'b1;                // rising edge: the LED samples SDI
            ph      <= 1'b0;
            if (bcnt != 5'd0) begin
                bcnt <= bcnt - 5'd1;
                sh   <= {sh[30:0], 1'b0};
            end else if (wcnt == LAST_WORD) begin
                busy <= 1'b0;               // CKI returns low in the next (idle) cycle
            end else begin
                wcnt     <= next_w;
                bcnt     <= 5'd31;
                sh       <= next_word;      // mem_q = PIX[eng_addr], read during the last word
                eng_addr <= eng_addr + 7'd1;
            end
        end
    end
endmodule
