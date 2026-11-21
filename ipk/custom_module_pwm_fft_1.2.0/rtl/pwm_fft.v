// PWM_FFT 1.2.0: audio spectrum analyser on a PWM audio signal, APB register read-out.
// 1.2.0: band 1 = 50-500 Hz, bands 2..12 logarithmic from 500 Hz to 8 kHz.
// 1.1.0: 256-point FFT (62.5 Hz bins, 16 ms frames).
//
// Two clocks:
//   CORE_CLK  PWM sampling and FFT processing (24 MHz for the TTS_APB PWM: the same clock
//             that generates the PWM, so the duty cycle is counted exactly).
//   PCLK      APB registers (12 MHz in the TTS SoC).
// CORE_CLK and PCLK may be related (one PLL) or unrelated. Data crosses between them only
// through toggle handshakes: configuration (CTRL, SCALE) is copied into the core after each
// write, and each new set of results is copied into the APB domain once per frame.
// PRESETN resets both domains (asserted asynchronously, released synchronously to CORE_CLK).
//
//   1. PWM to sample: PWM_IN is synchronised (2 flip-flops) and its high cycles are counted over
//      WIN_CLKS CORE_CLK cycles (1500 at 24 MHz = 2 carrier periods = one 16 kHz audio sample).
//      The window is an exact number of carrier periods, so the count is the average duty
//      cycle independent of phase. sample = (count - WIN_CLKS/2) << SMP_SHIFT (about +-12000).
//   2. A frame is 256 samples (16 ms at 16 kHz). The frame mean (DC) is removed and an
//      optional periodic Hann window is applied while the frame is loaded, bit reversed,
//      into the FFT work memory.
//   3. 256-point radix-2 decimation-in-time FFT, in place, scaled by 1/2 per stage, using one
//      sequential shift-and-add multiplier (no DSP blocks needed) and a quarter-wave sine table.
//   4. Power re^2 + im^2 of bins 1..127 (62.5 Hz per bin) is summed into 12 bands: band 1 holds
//      50-500 Hz, bands 2..12 split 500 Hz .. 8 kHz into 11 equal log steps (factor 1.29 each),
//      rounded to whole bins:
//        band  1: bins   1-7   (31-469 Hz)      band  7: bins  29-36  (1781-2281 Hz)
//        band  2: bins   8-10  (469-656 Hz)     band  8: bins  37-46  (2281-2906 Hz)
//        band  3: bins  11-13  (656-844 Hz)     band  9: bins  47-60  (2906-3781 Hz)
//        band  4: bins  14-17  (844-1094 Hz)    band 10: bins  61-77  (3781-4844 Hz)
//        band  5: bins  18-21  (1094-1344 Hz)   band 11: bins  78-99  (4844-6219 Hz)
//        band  6: bins  22-28  (1344-1781 Hz)   band 12: bins 100-127 (6219-8000 Hz)
//   5. Per band: L = 4 * log2(power) (quarter steps of about 0.75 dB);
//        level = 0 if L < FLOOR, else min(7, 1 + (L - FLOOR) / STEP)
//      followed by a peak display: a band jumps up at once and falls one level every DECAY
//      frames (DECAY = 0: levels follow every frame).
// Processing a frame takes about 92,000 CORE_CLK cycles (3.8 ms at 24 MHz), less than one frame.
//
// Register map (PCLK domain, byte offsets, 1 KB window, PADDR[9:2] = word index):
//   0x00  ID         R   0x50464654 ("PFFT")
//   0x04  CTRL       W   [0] EN (reset 1)  [1] WINDOW Hann on (reset 1)  [11:8] DECAY (reset 2)
//         STATUS     R   [0] EN  [1] WINDOW  [11:8] DECAY (as written)
//                        [12] READY: new levels since the last LEVELS_LO read  [13] BUSY
//                        [31:16] frame counter
//   0x08  SCALE      RW  [7:0] FLOOR (reset 36)  [12:8] STEP (reset 8 = 2 x log2 = about 6 dB)
//   0x0C  LEVELS_LO  R   bands 1..8 as 4-bit levels 0..7: [3:0] band 1 ... [31:28] band 8
//                        reading it clears READY and captures LEVELS_HI of the same frame
//   0x10  LEVELS_HI  R   bands 9..12: [3:0] band 9 ... [15:12] band 12, [31:16] = 0
//   0x40 + 4*b  RAW[b] R [7:0] L = 4 * log2(band power) of band b+1 (b = 0..11)
//
// Verilog-2001.
module pwm_fft #(
    parameter integer WIN_CLKS = 1500          // CORE_CLK cycles per audio sample, 64..4095
) (
    input  wire        CORE_CLK,
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
    input  wire        PWM_IN
);
    localparam [31:0] ID_VALUE  = 32'h5046_4654;
    localparam [11:0] HALF_WIN  = WIN_CLKS / 2;
    // sample scaling: (count - WIN_CLKS/2) << SMP_SHIFT stays within +-16384
    localparam integer SMP_SHIFT = (WIN_CLKS > 2047) ? 3 : (WIN_CLKS > 1023) ? 4 : 5;

    assign PREADY  = 1'b1;
    assign PSLVERR = 1'b0;

    // sin(2*pi*m/256) in Q15, m = 0..64
    function [15:0] sinq;
        input [6:0] m;
        begin
            case (m)
                7'd0: sinq = 16'd0;
                7'd1: sinq = 16'd804;
                7'd2: sinq = 16'd1608;
                7'd3: sinq = 16'd2410;
                7'd4: sinq = 16'd3212;
                7'd5: sinq = 16'd4011;
                7'd6: sinq = 16'd4808;
                7'd7: sinq = 16'd5602;
                7'd8: sinq = 16'd6393;
                7'd9: sinq = 16'd7179;
                7'd10: sinq = 16'd7962;
                7'd11: sinq = 16'd8739;
                7'd12: sinq = 16'd9512;
                7'd13: sinq = 16'd10278;
                7'd14: sinq = 16'd11039;
                7'd15: sinq = 16'd11793;
                7'd16: sinq = 16'd12539;
                7'd17: sinq = 16'd13279;
                7'd18: sinq = 16'd14010;
                7'd19: sinq = 16'd14732;
                7'd20: sinq = 16'd15446;
                7'd21: sinq = 16'd16151;
                7'd22: sinq = 16'd16846;
                7'd23: sinq = 16'd17530;
                7'd24: sinq = 16'd18204;
                7'd25: sinq = 16'd18868;
                7'd26: sinq = 16'd19519;
                7'd27: sinq = 16'd20159;
                7'd28: sinq = 16'd20787;
                7'd29: sinq = 16'd21403;
                7'd30: sinq = 16'd22005;
                7'd31: sinq = 16'd22594;
                7'd32: sinq = 16'd23170;
                7'd33: sinq = 16'd23731;
                7'd34: sinq = 16'd24279;
                7'd35: sinq = 16'd24811;
                7'd36: sinq = 16'd25329;
                7'd37: sinq = 16'd25832;
                7'd38: sinq = 16'd26319;
                7'd39: sinq = 16'd26790;
                7'd40: sinq = 16'd27245;
                7'd41: sinq = 16'd27683;
                7'd42: sinq = 16'd28105;
                7'd43: sinq = 16'd28510;
                7'd44: sinq = 16'd28898;
                7'd45: sinq = 16'd29268;
                7'd46: sinq = 16'd29621;
                7'd47: sinq = 16'd29956;
                7'd48: sinq = 16'd30273;
                7'd49: sinq = 16'd30571;
                7'd50: sinq = 16'd30852;
                7'd51: sinq = 16'd31113;
                7'd52: sinq = 16'd31356;
                7'd53: sinq = 16'd31580;
                7'd54: sinq = 16'd31785;
                7'd55: sinq = 16'd31971;
                7'd56: sinq = 16'd32137;
                7'd57: sinq = 16'd32285;
                7'd58: sinq = 16'd32412;
                7'd59: sinq = 16'd32521;
                7'd60: sinq = 16'd32609;
                7'd61: sinq = 16'd32678;
                7'd62: sinq = 16'd32728;
                7'd63: sinq = 16'd32757;
                7'd64: sinq = 16'd32767;
                default: sinq = 16'd0;
            endcase
        end
    endfunction

    // periodic Hann window sin^2(pi*i/256) in Q15, i = 0..128 (w[n] = w[256-n])
    function [15:0] hannq;
        input [7:0] i;
        begin
            case (i)
                8'd0: hannq = 16'd0;
                8'd1: hannq = 16'd5;
                8'd2: hannq = 16'd20;
                8'd3: hannq = 16'd44;
                8'd4: hannq = 16'd79;
                8'd5: hannq = 16'd123;
                8'd6: hannq = 16'd177;
                8'd7: hannq = 16'd241;
                8'd8: hannq = 16'd315;
                8'd9: hannq = 16'd398;
                8'd10: hannq = 16'd491;
                8'd11: hannq = 16'd593;
                8'd12: hannq = 16'd705;
                8'd13: hannq = 16'd827;
                8'd14: hannq = 16'd958;
                8'd15: hannq = 16'd1098;
                8'd16: hannq = 16'd1247;
                8'd17: hannq = 16'd1406;
                8'd18: hannq = 16'd1573;
                8'd19: hannq = 16'd1749;
                8'd20: hannq = 16'd1935;
                8'd21: hannq = 16'd2128;
                8'd22: hannq = 16'd2331;
                8'd23: hannq = 16'd2542;
                8'd24: hannq = 16'd2761;
                8'd25: hannq = 16'd2989;
                8'd26: hannq = 16'd3224;
                8'd27: hannq = 16'd3468;
                8'd28: hannq = 16'd3719;
                8'd29: hannq = 16'd3978;
                8'd30: hannq = 16'd4244;
                8'd31: hannq = 16'd4518;
                8'd32: hannq = 16'd4799;
                8'd33: hannq = 16'd5086;
                8'd34: hannq = 16'd5381;
                8'd35: hannq = 16'd5682;
                8'd36: hannq = 16'd5990;
                8'd37: hannq = 16'd6304;
                8'd38: hannq = 16'd6624;
                8'd39: hannq = 16'd6950;
                8'd40: hannq = 16'd7281;
                8'd41: hannq = 16'd7618;
                8'd42: hannq = 16'd7961;
                8'd43: hannq = 16'd8308;
                8'd44: hannq = 16'd8660;
                8'd45: hannq = 16'd9017;
                8'd46: hannq = 16'd9379;
                8'd47: hannq = 16'd9744;
                8'd48: hannq = 16'd10114;
                8'd49: hannq = 16'd10487;
                8'd50: hannq = 16'd10864;
                8'd51: hannq = 16'd11244;
                8'd52: hannq = 16'd11628;
                8'd53: hannq = 16'd12014;
                8'd54: hannq = 16'd12403;
                8'd55: hannq = 16'd12794;
                8'd56: hannq = 16'd13187;
                8'd57: hannq = 16'd13583;
                8'd58: hannq = 16'd13980;
                8'd59: hannq = 16'd14378;
                8'd60: hannq = 16'd14778;
                8'd61: hannq = 16'd15178;
                8'd62: hannq = 16'd15580;
                8'd63: hannq = 16'd15981;
                8'd64: hannq = 16'd16384;
                8'd65: hannq = 16'd16786;
                8'd66: hannq = 16'd17187;
                8'd67: hannq = 16'd17589;
                8'd68: hannq = 16'd17989;
                8'd69: hannq = 16'd18389;
                8'd70: hannq = 16'd18787;
                8'd71: hannq = 16'd19184;
                8'd72: hannq = 16'd19580;
                8'd73: hannq = 16'd19973;
                8'd74: hannq = 16'd20364;
                8'd75: hannq = 16'd20753;
                8'd76: hannq = 16'd21139;
                8'd77: hannq = 16'd21523;
                8'd78: hannq = 16'd21903;
                8'd79: hannq = 16'd22280;
                8'd80: hannq = 16'd22653;
                8'd81: hannq = 16'd23023;
                8'd82: hannq = 16'd23388;
                8'd83: hannq = 16'd23750;
                8'd84: hannq = 16'd24107;
                8'd85: hannq = 16'd24459;
                8'd86: hannq = 16'd24806;
                8'd87: hannq = 16'd25149;
                8'd88: hannq = 16'd25486;
                8'd89: hannq = 16'd25817;
                8'd90: hannq = 16'd26143;
                8'd91: hannq = 16'd26463;
                8'd92: hannq = 16'd26777;
                8'd93: hannq = 16'd27085;
                8'd94: hannq = 16'd27386;
                8'd95: hannq = 16'd27681;
                8'd96: hannq = 16'd27968;
                8'd97: hannq = 16'd28249;
                8'd98: hannq = 16'd28523;
                8'd99: hannq = 16'd28789;
                8'd100: hannq = 16'd29048;
                8'd101: hannq = 16'd29299;
                8'd102: hannq = 16'd29543;
                8'd103: hannq = 16'd29778;
                8'd104: hannq = 16'd30006;
                8'd105: hannq = 16'd30225;
                8'd106: hannq = 16'd30436;
                8'd107: hannq = 16'd30639;
                8'd108: hannq = 16'd30832;
                8'd109: hannq = 16'd31018;
                8'd110: hannq = 16'd31194;
                8'd111: hannq = 16'd31361;
                8'd112: hannq = 16'd31520;
                8'd113: hannq = 16'd31669;
                8'd114: hannq = 16'd31809;
                8'd115: hannq = 16'd31940;
                8'd116: hannq = 16'd32062;
                8'd117: hannq = 16'd32174;
                8'd118: hannq = 16'd32276;
                8'd119: hannq = 16'd32369;
                8'd120: hannq = 16'd32452;
                8'd121: hannq = 16'd32526;
                8'd122: hannq = 16'd32590;
                8'd123: hannq = 16'd32644;
                8'd124: hannq = 16'd32688;
                8'd125: hannq = 16'd32723;
                8'd126: hannq = 16'd32747;
                8'd127: hannq = 16'd32762;
                8'd128: hannq = 16'd32767;
                default: hannq = 16'd0;
            endcase
        end
    endfunction

    // last FFT bin of each band (band 0..11)
    function [6:0] band_last;
        input [3:0] bnd;
        begin
            case (bnd)
                4'd0:  band_last = 7'd7;
                4'd1:  band_last = 7'd10;
                4'd2:  band_last = 7'd13;
                4'd3:  band_last = 7'd17;
                4'd4:  band_last = 7'd21;
                4'd5:  band_last = 7'd28;
                4'd6:  band_last = 7'd36;
                4'd7:  band_last = 7'd46;
                4'd8:  band_last = 7'd60;
                4'd9:  band_last = 7'd77;
                4'd10: band_last = 7'd99;
                default: band_last = 7'd127;
            endcase
        end
    endfunction

    // 8-bit bit reversal
    function [7:0] bitrev8;
        input [7:0] v;
        begin
            bitrev8 = {v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]};
        end
    endfunction

    // saturate an 18-bit signed value to 16 bits
    function [15:0] sat16;
        input [17:0] v;
        begin
            if ((v[17] == v[16]) && (v[16] == v[15])) sat16 = v[15:0];
            else                                      sat16 = v[17] ? 16'h8000 : 16'h7FFF;
        end
    endfunction

    // ================================================================ APB domain (PCLK)
    reg        ctrl_en;
    reg        ctrl_win;
    reg [3:0]  ctrl_decay;
    reg [7:0]  scale_floor;
    reg [4:0]  scale_step;
    reg        cfg_tog;          // toggles on every CTRL / SCALE write
    reg        st_ready;
    reg [15:0] frame_cnt;
    reg [47:0] a_lv;             // levels in the APB domain, 12 x 4 bit
    reg [95:0] a_raw;            // L values in the APB domain, 12 x 8 bit
    reg [15:0] hi_snap;          // LEVELS_HI captured on a LEVELS_LO read
    reg [2:0]  pub_s;            // synchroniser of the core publish toggle
    reg [1:0]  busy_s;           // synchroniser of the core busy flag

    wire [7:0] widx = PADDR[9:2];
    wire       wr   = PSEL & PENABLE &  PWRITE;
    wire       rd   = PSEL & PENABLE & ~PWRITE;

    // ================================================================ core domain (CORE_CLK)
    // reset synchroniser: asynchronous assertion, release on CORE_CLK
    reg [1:0] crst;
    always @(posedge CORE_CLK or negedge PRESETN) begin
        if (!PRESETN) crst <= 2'b00;
        else          crst <= {crst[0], 1'b1};
    end
    wire crst_n = crst[1];

    // configuration copied from the APB domain after each write
    reg [2:0]  cfg_s;
    reg        c_en, c_win;
    reg [3:0]  c_decay;
    reg [7:0]  c_floor;
    reg [4:0]  c_step;

    always @(posedge CORE_CLK or negedge crst_n) begin
        if (!crst_n) begin
            cfg_s   <= 3'b000;
            c_en    <= 1'b1;
            c_win   <= 1'b1;
            c_decay <= 4'd2;
            c_floor <= 8'd36;
            c_step  <= 5'd8;
        end else begin
            cfg_s <= {cfg_s[1:0], cfg_tog};
            if (cfg_s[2] != cfg_s[1]) begin
                c_en    <= ctrl_en;
                c_win   <= ctrl_win;
                c_decay <= ctrl_decay;
                c_floor <= scale_floor;
                c_step  <= scale_step;
            end
        end
    end

    // ---------------------------------------------------------------- 1. PWM to sample
    reg  [1:0]  pwm_s;
    reg  [11:0] win_cnt;
    reg  [11:0] high_cnt;
    reg         smp_stb;
    reg  [15:0] smp;
    wire [11:0] high_tot = high_cnt + {11'd0, pwm_s[1]};
    wire [12:0] smp_c    = {1'b0, high_tot} - {1'b0, HALF_WIN};      // signed, +-WIN_CLKS/2
    wire [15:0] smp_x    = {{3{smp_c[12]}}, smp_c};

    always @(posedge CORE_CLK or negedge crst_n) begin
        if (!crst_n) begin
            pwm_s    <= 2'b00;
            win_cnt  <= 12'd0;
            high_cnt <= 12'd0;
            smp_stb  <= 1'b0;
            smp      <= 16'd0;
        end else begin
            pwm_s   <= {pwm_s[0], PWM_IN};
            smp_stb <= 1'b0;
            if (win_cnt == WIN_CLKS - 1) begin
                win_cnt  <= 12'd0;
                high_cnt <= 12'd0;
                smp      <= smp_x << SMP_SHIFT;
                smp_stb  <= 1'b1;
            end else begin
                win_cnt  <= win_cnt + 12'd1;
                high_cnt <= high_tot;
            end
        end
    end

    // ---------------------------------------------------------------- 2. frame buffer
    reg [15:0] in_mem [0:255];
    reg [7:0]  in_wa;
    reg [15:0] in_q;
    reg [7:0]  ld_n;                 // load index, also the in_mem read address
    reg [23:0] fsum;                 // running sum of the frame being collected
    reg [15:0] mean_l;               // mean of the last complete frame
    reg        frame_go;             // pulse: 256 new samples collected

    always @(posedge CORE_CLK) begin
        if (smp_stb) in_mem[in_wa] <= smp;
        in_q <= in_mem[ld_n];
    end

    wire [23:0] fsum_n = fsum + {{8{smp[15]}}, smp};

    always @(posedge CORE_CLK or negedge crst_n) begin
        if (!crst_n) begin
            in_wa    <= 8'd0;
            fsum     <= 24'd0;
            mean_l   <= 16'd0;
            frame_go <= 1'b0;
        end else begin
            frame_go <= 1'b0;
            if (smp_stb) begin
                in_wa <= in_wa + 8'd1;
                if (in_wa == 8'd255) begin
                    fsum     <= 24'd0;
                    mean_l   <= fsum_n[23:8];          // arithmetic: sum / 256
                    frame_go <= 1'b1;
                end else begin
                    fsum <= fsum_n;
                end
            end
        end
    end

    // ---------------------------------------------------------------- multiplier
    reg         mul_start;
    reg  [15:0] mul_a, mul_b;
    wire        mul_done;
    wire [31:0] mul_p;

    pwm_fft_mul u_mul (
        .clk(CORE_CLK), .rst_n(crst_n), .start(mul_start),
        .a(mul_a), .b(mul_b), .done(mul_done), .p(mul_p)
    );

    // ---------------------------------------------------------------- FFT work memory
    reg [15:0] re_mem [0:255];
    reg [15:0] im_mem [0:255];
    reg [7:0]  ra, wa;
    reg [15:0] wre, wim;
    reg        we;
    reg [15:0] re_q, im_q;

    always @(posedge CORE_CLK) begin
        if (we) begin
            re_mem[wa] <= wre;
            im_mem[wa] <= wim;
        end
        re_q <= re_mem[ra];
        im_q <= im_mem[ra];
    end

    // ---------------------------------------------------------------- control
    localparam [4:0] S_IDLE = 5'd0,  S_LD0  = 5'd1,  S_LD1  = 5'd2,  S_LDM  = 5'd3,  S_LDW  = 5'd4,
                     S_BF0  = 5'd5,  S_BF1  = 5'd6,  S_BF2  = 5'd7,  S_BF3  = 5'd8,  S_M1   = 5'd9,
                     S_M1W  = 5'd10, S_M2W  = 5'd11, S_M3W  = 5'd12, S_M4W  = 5'd13, S_BW1  = 5'd14,
                     S_BW2  = 5'd15, S_MG0  = 5'd16, S_MG1  = 5'd17, S_MG2  = 5'd18, S_MG3W = 5'd19,
                     S_MG4W = 5'd20, S_MG5  = 5'd21, S_LV0  = 5'd22, S_LV1  = 5'd23, S_LV2  = 5'd24,
                     S_LV3  = 5'd25, S_PUB  = 5'd26;

    reg [4:0]  state;
    reg [3:0]  stg;                  // FFT stage 1..8
    reg [6:0]  bfi;                  // butterfly index 0..127
    reg [15:0] ar, ai, br, bi;
    reg [32:0] accr, acci;
    reg [15:0] xd;                   // loaded sample minus mean
    reg [6:0]  bin;                  // power phase: bin 1..127
    reg [3:0]  band;
    reg [37:0] bacc;                 // band power accumulator
    reg [31:0] sq;
    reg [5:0]  msb;
    reg [7:0]  lval;
    reg [7:0]  dval;
    reg [2:0]  lvl;
    reg [47:0] lv_w;                 // display levels being built
    reg [95:0] raw_w;
    reg [47:0] lv_pub;               // published results (core domain, stable for one frame)
    reg [95:0] raw_pub;
    reg        pub_tog;              // toggles when a new result is published
    reg [3:0]  dcnt;
    reg        dtick;
    reg        c_busy;               // registered busy flag for the APB domain

    always @(posedge CORE_CLK or negedge crst_n) begin
        if (!crst_n) c_busy <= 1'b0;
        else         c_busy <= (state != S_IDLE);
    end

    // butterfly addressing
    wire [7:0] half  = 8'd1 << (stg - 4'd1);
    wire [7:0] jmask = half - 8'd1;
    wire [7:0] bfi8  = {1'b0, bfi};
    wire [7:0] jj    = bfi8 & jmask;
    wire [7:0] top   = ((bfi8 & ~jmask) << 1) | jj;
    wire [7:0] bot   = top | half;
    wire [7:0] twk   = jj << (4'd8 - stg);     // twiddle index 0..127

    // twiddle W = cos(2 pi k/256) - j sin(2 pi k/256), registered
    reg [15:0] tw_r, tw_i;
    always @(posedge CORE_CLK) begin
        if (twk <= 8'd64) begin
            tw_r <= sinq(7'd64 - twk[6:0]);
            tw_i <= 16'd0 - sinq(twk[6:0]);
        end else begin
            tw_r <= 16'd0 - sinq(twk[6:0] - 7'd64);
            tw_i <= 16'd0 - sinq(7'd0 - twk[6:0]);         // 128 - k (mod 128)
        end
    end

    // Hann window, registered (w[n] = w[256-n])
    reg [15:0] win_q;
    always @(posedge CORE_CLK)
        win_q <= hannq(ld_n[7] ? (8'd0 - ld_n) : ld_n);

    // butterfly results: t = b * W (Q15, rounded), a' = (a + t) / 2, b' = (a - t) / 2
    wire [32:0] accr_rnd = accr + 33'd16384;
    wire [32:0] acci_rnd = acci + 33'd16384;
    wire [17:0] tr = accr_rnd[32:15];
    wire [17:0] ti = acci_rnd[32:15];
    wire [18:0] sum_r = {{3{ar[15]}}, ar} + {tr[17], tr};
    wire [18:0] sum_i = {{3{ai[15]}}, ai} + {ti[17], ti};
    wire [18:0] dif_r = {{3{ar[15]}}, ar} - {tr[17], tr};
    wire [18:0] dif_i = {{3{ai[15]}}, ai} - {ti[17], ti};

    // load: sample minus frame mean
    wire [16:0] xd_c    = {in_q[15], in_q} - {mean_l[15], mean_l};
    wire [31:0] win_rnd = mul_p + 32'd16384;

    // level update with peak hold
    wire [3:0] old_lv  = lv_w[band*4 +: 4];
    wire [3:0] dec_lv  = (old_lv != 4'd0) ? old_lv - 4'd1 : 4'd0;
    wire [3:0] hold_lv = dtick ? dec_lv : old_lv;
    wire [3:0] new_lv  = (c_decay == 4'd0) ? {1'b0, lvl} :
                         (({1'b0, lvl} > hold_lv) ? {1'b0, lvl} : hold_lv);
    wire [4:0] step_e  = (c_step == 5'd0) ? 5'd1 : c_step;

    always @(posedge CORE_CLK or negedge crst_n) begin
        if (!crst_n) begin
            state     <= S_IDLE;
            stg       <= 4'd1;
            bfi       <= 7'd0;
            ld_n      <= 8'd0;
            ra        <= 8'd0;
            wa        <= 8'd0;
            wre       <= 16'd0;
            wim       <= 16'd0;
            we        <= 1'b0;
            mul_start <= 1'b0;
            mul_a     <= 16'd0;
            mul_b     <= 16'd0;
            ar <= 16'd0; ai <= 16'd0; br <= 16'd0; bi <= 16'd0;
            accr      <= 33'd0;
            acci      <= 33'd0;
            xd        <= 16'd0;
            bin       <= 7'd1;
            band      <= 4'd0;
            bacc      <= 38'd0;
            sq        <= 32'd0;
            msb       <= 6'd0;
            lval      <= 8'd0;
            dval      <= 8'd0;
            lvl       <= 3'd0;
            lv_w      <= 48'd0;
            raw_w     <= 96'd0;
            lv_pub    <= 48'd0;
            raw_pub   <= 96'd0;
            pub_tog   <= 1'b0;
            dcnt      <= 4'd0;
            dtick     <= 1'b0;
        end else begin
            we        <= 1'b0;
            mul_start <= 1'b0;
            case (state)
            // ---------------------------------------------------------- wait for a frame
            S_IDLE: begin
                ld_n <= 8'd0;
                if (frame_go && c_en) begin
                    if (c_decay != 4'd0 && dcnt + 4'd1 >= c_decay) begin
                        dtick <= 1'b1;
                        dcnt  <= 4'd0;
                    end else begin
                        dtick <= 1'b0;
                        dcnt  <= dcnt + 4'd1;
                    end
                    state <= S_LD0;
                end
            end
            // ---------------------------------------------------------- load + window
            S_LD0: state <= S_LD1;                      // in_mem / window ROM read of ld_n
            S_LD1: begin
                xd <= sat16({xd_c[16], xd_c});
                if (c_win) begin
                    mul_a     <= sat16({xd_c[16], xd_c});
                    mul_b     <= win_q;
                    mul_start <= 1'b1;
                    state     <= S_LDM;
                end else begin
                    state <= S_LDW;
                end
            end
            S_LDM: if (mul_done) begin
                xd    <= win_rnd[30:15];
                state <= S_LDW;
            end
            S_LDW: begin
                wa   <= bitrev8(ld_n);
                wre  <= xd;
                wim  <= 16'd0;
                we   <= 1'b1;
                ld_n <= ld_n + 8'd1;
                if (ld_n == 8'd255) begin
                    stg   <= 4'd1;
                    bfi   <= 7'd0;
                    state <= S_BF0;
                end else begin
                    state <= S_LD0;
                end
            end
            // ---------------------------------------------------------- butterflies
            S_BF0: begin ra <= top; state <= S_BF1; end
            S_BF1: begin ra <= bot; state <= S_BF2; end
            S_BF2: begin ar <= re_q; ai <= im_q; state <= S_BF3; end
            S_BF3: begin br <= re_q; bi <= im_q; state <= S_M1;  end
            S_M1: begin
                mul_a <= br; mul_b <= tw_r; mul_start <= 1'b1;
                state <= S_M1W;
            end
            S_M1W: if (mul_done) begin
                accr  <= {mul_p[31], mul_p};
                mul_a <= bi; mul_b <= tw_i; mul_start <= 1'b1;
                state <= S_M2W;
            end
            S_M2W: if (mul_done) begin
                accr  <= accr - {mul_p[31], mul_p};
                mul_a <= br; mul_b <= tw_i; mul_start <= 1'b1;
                state <= S_M3W;
            end
            S_M3W: if (mul_done) begin
                acci  <= {mul_p[31], mul_p};
                mul_a <= bi; mul_b <= tw_r; mul_start <= 1'b1;
                state <= S_M4W;
            end
            S_M4W: if (mul_done) begin
                acci  <= acci + {mul_p[31], mul_p};
                state <= S_BW1;
            end
            S_BW1: begin
                wa  <= top;
                wre <= sat16(sum_r[18:1]);
                wim <= sat16(sum_i[18:1]);
                we  <= 1'b1;
                state <= S_BW2;
            end
            S_BW2: begin
                wa  <= bot;
                wre <= sat16(dif_r[18:1]);
                wim <= sat16(dif_i[18:1]);
                we  <= 1'b1;
                if (bfi == 7'd127) begin
                    bfi <= 7'd0;
                    if (stg == 4'd8) state <= S_MG0;
                    else begin stg <= stg + 4'd1; state <= S_BF0; end
                end else begin
                    bfi   <= bfi + 7'd1;
                    state <= S_BF0;
                end
            end
            // ---------------------------------------------------------- band power
            S_MG0: begin
                bin   <= 7'd1;
                band  <= 4'd0;
                bacc  <= 38'd0;
                ra    <= 8'd1;
                state <= S_MG1;
            end
            S_MG1: state <= S_MG2;
            S_MG2: begin
                br    <= re_q;
                bi    <= im_q;
                mul_a <= re_q; mul_b <= re_q; mul_start <= 1'b1;
                state <= S_MG3W;
            end
            S_MG3W: if (mul_done) begin
                sq    <= mul_p;
                mul_a <= bi; mul_b <= bi; mul_start <= 1'b1;
                state <= S_MG4W;
            end
            S_MG4W: if (mul_done) begin
                bacc  <= bacc + {6'd0, sq} + {6'd0, mul_p};
                state <= S_MG5;
            end
            S_MG5: begin
                if (bin == band_last(band)) begin
                    msb   <= 6'd37;
                    state <= S_LV0;
                end else begin
                    bin   <= bin + 7'd1;
                    ra    <= {1'b0, bin + 7'd1};
                    state <= S_MG1;
                end
            end
            // ---------------------------------------------------------- level
            S_LV0: begin                                 // normalise: find the leading one
                if (bacc == 38'd0) begin
                    lval  <= 8'd0;
                    state <= S_LV1;
                end else if (bacc[37]) begin
                    lval  <= {msb, bacc[36:35]};
                    state <= S_LV1;
                end else begin
                    bacc <= bacc << 1;
                    msb  <= msb - 6'd1;
                end
            end
            S_LV1: begin
                if (lval < c_floor) begin
                    lvl   <= 3'd0;
                    state <= S_LV3;
                end else begin
                    dval  <= lval - c_floor;
                    lvl   <= 3'd1;
                    state <= S_LV2;
                end
            end
            S_LV2: begin
                if (dval >= {3'd0, step_e} && lvl != 3'd7) begin
                    dval <= dval - {3'd0, step_e};
                    lvl  <= lvl + 3'd1;
                end else begin
                    state <= S_LV3;
                end
            end
            S_LV3: begin
                lv_w[band*4 +: 4]  <= new_lv;
                raw_w[band*8 +: 8] <= lval;
                bacc <= 38'd0;
                if (band == 4'd11) begin
                    state <= S_PUB;
                end else begin
                    band  <= band + 4'd1;
                    bin   <= bin + 7'd1;
                    ra    <= {1'b0, bin + 7'd1};
                    state <= S_MG1;
                end
            end
            S_PUB: begin
                lv_pub  <= lv_w;
                raw_pub <= raw_w;
                pub_tog <= ~pub_tog;
                state   <= S_IDLE;
            end
            default: state <= S_IDLE;
            endcase
        end
    end

    // ================================================================ APB domain logic
    always @(posedge PCLK or negedge PRESETN) begin
        if (!PRESETN) begin
            ctrl_en     <= 1'b1;
            ctrl_win    <= 1'b1;
            ctrl_decay  <= 4'd2;
            scale_floor <= 8'd36;
            scale_step  <= 5'd8;
            cfg_tog     <= 1'b0;
            st_ready    <= 1'b0;
            frame_cnt   <= 16'd0;
            a_lv        <= 48'd0;
            a_raw       <= 96'd0;
            hi_snap     <= 16'd0;
            pub_s       <= 3'b000;
            busy_s      <= 2'b00;
        end else begin
            pub_s  <= {pub_s[1:0], pub_tog};
            busy_s <= {busy_s[0], c_busy};
            if (wr && widx == 8'd1) begin
                ctrl_en    <= PWDATA[0];
                ctrl_win   <= PWDATA[1];
                ctrl_decay <= PWDATA[11:8];
                cfg_tog    <= ~cfg_tog;
            end
            if (wr && widx == 8'd2) begin
                scale_floor <= PWDATA[7:0];
                scale_step  <= PWDATA[12:8];
                cfg_tog     <= ~cfg_tog;
            end
            if (rd && widx == 8'd3)
                hi_snap <= a_lv[47:32];
            if (pub_s[2] != pub_s[1]) begin             // new results: stable in the core for a frame
                a_lv      <= lv_pub;
                a_raw     <= raw_pub;
                frame_cnt <= frame_cnt + 16'd1;
                st_ready  <= 1'b1;
            end else if (rd && widx == 8'd3) begin
                st_ready <= 1'b0;
            end
        end
    end

    always @(*) begin
        case (widx)
            8'd0:    PRDATA = ID_VALUE;
            8'd1:    PRDATA = {frame_cnt, 2'd0, busy_s[1], st_ready, ctrl_decay, 6'd0, ctrl_win, ctrl_en};
            8'd2:    PRDATA = {19'd0, scale_step, scale_floor};
            8'd3:    PRDATA = a_lv[31:0];
            8'd4:    PRDATA = {16'd0, hi_snap};
            8'd16:   PRDATA = {24'd0, a_raw[7:0]};
            8'd17:   PRDATA = {24'd0, a_raw[15:8]};
            8'd18:   PRDATA = {24'd0, a_raw[23:16]};
            8'd19:   PRDATA = {24'd0, a_raw[31:24]};
            8'd20:   PRDATA = {24'd0, a_raw[39:32]};
            8'd21:   PRDATA = {24'd0, a_raw[47:40]};
            8'd22:   PRDATA = {24'd0, a_raw[55:48]};
            8'd23:   PRDATA = {24'd0, a_raw[63:56]};
            8'd24:   PRDATA = {24'd0, a_raw[71:64]};
            8'd25:   PRDATA = {24'd0, a_raw[79:72]};
            8'd26:   PRDATA = {24'd0, a_raw[87:80]};
            8'd27:   PRDATA = {24'd0, a_raw[95:88]};
            default: PRDATA = 32'd0;
        endcase
    end
endmodule

// ====================================================================================
// Sequential signed 16 x 16 multiplier (shift and add, 16 cycles). start loads the
// operands; done pulses for one cycle when p is valid. p stays valid until the next start.
module pwm_fft_mul (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        start,
    input  wire [15:0] a,
    input  wire [15:0] b,
    output reg         done,
    output wire [31:0] p
);
    reg [15:0] mb;
    reg [31:0] mc;
    reg [31:0] acc;
    reg [4:0]  cnt;
    reg        neg;
    reg        busy;
    wire [15:0] abs_a = a[15] ? (16'd0 - a) : a;
    wire [15:0] abs_b = b[15] ? (16'd0 - b) : b;

    assign p = neg ? (32'd0 - acc) : acc;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mb   <= 16'd0;
            mc   <= 32'd0;
            acc  <= 32'd0;
            cnt  <= 5'd0;
            neg  <= 1'b0;
            busy <= 1'b0;
            done <= 1'b0;
        end else begin
            done <= 1'b0;
            if (start) begin
                mb   <= abs_b;
                mc   <= {16'd0, abs_a};
                acc  <= 32'd0;
                cnt  <= 5'd16;
                neg  <= a[15] ^ b[15];
                busy <= 1'b1;
            end else if (busy) begin
                if (mb[0]) acc <= acc + mc;
                mb  <= mb >> 1;
                mc  <= mc << 1;
                cnt <= cnt - 5'd1;
                if (cnt == 5'd1) begin
                    busy <= 1'b0;
                    done <= 1'b1;
                end
            end
        end
    end
endmodule
