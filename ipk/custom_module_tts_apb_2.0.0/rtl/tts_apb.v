// TTS_APB 2.0.0: the speech engine of the project MachXO4_TTS behind a small APB register file,
// with two independent clocks:
//   PCLK     APB bus clock, any frequency (register file, character/command FIFO write side)
//   TTS_CLK  synthesizer clock, 24 MHz (line buffer, TTSCPU, sample FIFO, PWM)
// The speech engine (ttscpu, tts_roms, tts_engine, line_buf, sample_fifo, pwm10) is unchanged from 1.5.0.
//
//  Register map (word addresses, PADDR[3:2]) - as 1.5.0, only the ID changes:
//   0x00 ID      read     0x54545302
//   0x04 CTRL    write    bit0 START  speak the oldest complete line (ignored unless LINE_READY and not BUSY)
//                         bit1 CLEAR  discard all queued lines and any partial line (ignored while BUSY)
//   0x08 STATUS  read     bit0  BUSY, bit1 LINE_READY, bit2 DONE (sticky), bit3 PLAY, bit4 DROP (sticky),
//                         bits 15..8 LEN of the oldest complete line (0 when none)
//   0x0C DATA    write    bits 7..0 one character; read: the character written last
//
// Clock crossing:
//   - DATA and CTRL writes go, in order, through an 8-entry asynchronous FIFO (gray pointers) into the TTS
//     domain. A write stalls (PREADY low) only while the FIFO is full.
//   - The TTS domain executes one FIFO entry at a time with three idle clocks after each, so that the
//     effect of an entry (line complete, BUSY, DROP) is visible before the next one is evaluated, exactly
//     as with the bus-paced writes of 1.5.0.
//   - The status is returned with a request/acknowledge snapshot that carries the count of executed
//     entries. A STATUS read stalls (PREADY low) until the snapshot covers every write made before it,
//     so firmware sees the same status sequence as with 1.5.0.
//   - RST_N (active low, PCLK domain) is synchronised into each domain.
//
// The block has one output: PWM (10-bit samples at 16 kHz, 32 kHz carrier from the 24 MHz TTS clock).
module tts_apb_slave #(
    parameter PWM_PERIOD = 750,     // 24 MHz TTS_CLK / 750 = 32 kHz carrier
    parameter PWM_REPEAT = 2        // two PWM periods per 16 kHz audio sample
) (
    input  wire        PCLK,        // APB clock, any frequency
    input  wire        TTS_CLK,     // synthesizer clock, 24 MHz
    input  wire        RST_N,
    input  wire [3:0]  PADDR,
    input  wire        PSEL,
    input  wire        PENABLE,
    input  wire        PWRITE,
    input  wire [31:0] PWDATA,
    output reg  [31:0] PRDATA,
    output wire        PREADY,
    output wire        PSLVERR,
    output wire        PWM_OUT
);
    // ================================================================ resets
    reg [1:0] prst_s = 2'b00;
    always @(posedge PCLK or negedge RST_N)
        if (!RST_N) prst_s <= 2'b00; else prst_s <= {prst_s[0], 1'b1};
    wire prst = ~prst_s[1];

    reg [1:0] trst_s = 2'b00;
    always @(posedge TTS_CLK or negedge RST_N)
        if (!RST_N) trst_s <= 2'b00; else trst_s <= {trst_s[0], 1'b1};
    wire trst = ~trst_s[1];

    function [3:0] bin2gray; input [3:0] b; bin2gray = b ^ (b >> 1); endfunction
    function [3:0] gray2bin;
        input [3:0] g;
        begin
            gray2bin[3] = g[3];
            gray2bin[2] = g[3] ^ g[2];
            gray2bin[1] = g[3] ^ g[2] ^ g[1];
            gray2bin[0] = g[3] ^ g[2] ^ g[1] ^ g[0];
        end
    endfunction

    // ---- signals that cross between the domains (declared first) ----
    reg  [3:0] wgray;                           // PCLK: FIFO write pointer (gray)
    reg  [3:0] tgray_r;                         // TTS:  FIFO read pointer (gray)
    reg        sack;                            // PCLK: snapshot acknowledge
    reg        t_sreq;                          // TTS:  snapshot request (toggle)
    reg [12:0] t_snap;                          // TTS:  status snapshot
    reg [3:0]  t_snap_cnt;                      // TTS:  entries executed at the snapshot

    // ================================================================ PCLK domain
    assign PSLVERR = 1'b0;

    wire access  = PSEL & PENABLE;
    wire wr_ctrl = PWRITE && (PADDR[3:2] == 2'd1) && (PWDATA[1:0] != 2'b00);
    wire wr_data = PWRITE && (PADDR[3:2] == 2'd3);
    wire rd_stat = !PWRITE && (PADDR[3:2] == 2'd2);

    // command FIFO write side: entry = {clear, start, char}; both flags 0 means a character
    reg  [9:0] cmem [0:7];
    reg  [3:0] wbin;
    reg  [3:0] rgray_p1, rgray_p2;              // TTS read pointer, synchronised to PCLK
    wire [3:0] rbin_p = gray2bin(rgray_p2);
    wire       full   = (wbin - rbin_p) == 4'd8;

    // status snapshot receive side
    reg        sreq_p1, sreq_p2;
    reg [12:0] p_stat;                          // {len[7:0], drop, play, done, ready, busy}
    reg [3:0]  p_cnt;                           // entries executed when the snapshot was taken
    wire       fresh = (p_cnt == wbin);

    wire need_push = wr_ctrl || wr_data;
    assign PREADY = !(access && ((need_push && full) || (rd_stat && !fresh)));
    wire push = access && PREADY && need_push;

    reg [7:0] last_data;

    always @(posedge PCLK) begin
        if (push) cmem[wbin[2:0]] <= wr_data ? {2'b00, PWDATA[7:0]} : {PWDATA[1], PWDATA[0], 8'h00};
    end

    always @(posedge PCLK) begin
        if (prst) begin
            wbin      <= 4'd0;
            wgray     <= 4'd0;
            rgray_p1  <= 4'd0;
            rgray_p2  <= 4'd0;
            sreq_p1   <= 1'b0;
            sreq_p2   <= 1'b0;
            sack      <= 1'b0;
            p_stat    <= 13'd0;
            p_cnt     <= 4'd0;
            last_data <= 8'd0;
        end else begin
            rgray_p1 <= tgray_r;
            rgray_p2 <= rgray_p1;
            if (push) begin
                wbin  <= wbin + 4'd1;
                wgray <= bin2gray(wbin + 4'd1);
                if (wr_data) last_data <= PWDATA[7:0];
            end
            sreq_p1 <= t_sreq;
            sreq_p2 <= sreq_p1;
            if (sreq_p2 != sack) begin           // a new snapshot is stable: take it
                p_stat <= t_snap;
                p_cnt  <= t_snap_cnt;
                sack   <= sreq_p2;
            end
        end
    end

    always @(*) begin
        case (PADDR[3:2])
            2'd0:    PRDATA = 32'h54545302;
            2'd1:    PRDATA = 32'h0;
            2'd2:    PRDATA = {16'd0, p_stat[12:5], 3'd0, p_stat[4:0]};
            default: PRDATA = {24'd0, last_data};
        endcase
    end

    // ================================================================ TTS_CLK domain
    reg  [3:0] wgray_t1, wgray_t2;              // write pointer, synchronised to TTS_CLK
    reg  [3:0] trbin;
    wire       t_empty = (wgray_t2 == tgray_r);
    reg  [1:0] gap;                             // idle clocks after an executed entry
    wire       pop = !t_empty && (gap == 2'd0);
    wire [9:0] entry = cmem[trbin[2:0]];

    reg  [7:0] ch_data;
    reg        ch_valid;                        // one-clock pulse: a character to the line buffer
    reg        busy, done_flag, drop_flag;
    reg        tts_start, buf_clear;
    wire       tts_done;
    wire       line_ready;
    wire [7:0] len;
    wire       tts_play;

    wire do_start = pop && entry[8];
    wire do_clear = pop && entry[9];
    wire do_char  = pop && (entry[9:8] == 2'b00);

    // status snapshot send side
    reg        t_ack1, t_ack2;

    always @(posedge TTS_CLK) begin
        ch_valid  <= 1'b0;
        tts_start <= 1'b0;
        buf_clear <= 1'b0;
        if (trst) begin
            wgray_t1   <= 4'd0;
            wgray_t2   <= 4'd0;
            trbin      <= 4'd0;
            tgray_r    <= 4'd0;
            gap        <= 2'd0;
            ch_data    <= 8'd0;
            busy       <= 1'b0;
            done_flag  <= 1'b0;
            drop_flag  <= 1'b0;
            t_sreq     <= 1'b0;
            t_ack1     <= 1'b0;
            t_ack2     <= 1'b0;
            t_snap     <= 13'd0;
            t_snap_cnt <= 4'd0;
        end else begin
            wgray_t1 <= wgray;
            wgray_t2 <= wgray_t1;
            t_ack1   <= sack;
            t_ack2   <= t_ack1;

            if (pop) begin
                trbin   <= trbin + 4'd1;
                tgray_r <= bin2gray(trbin + 4'd1);
                gap     <= 2'd3;
            end else if (gap != 2'd0) begin
                gap <= gap - 2'd1;
            end

            // command execution: same rules as the 1.5.0 register file
            if (do_char) begin
                ch_data  <= entry[7:0];
                ch_valid <= 1'b1;
            end
            if (do_start) begin
                done_flag <= 1'b0;
                drop_flag <= 1'b0;
                if (line_ready && !busy) begin
                    busy      <= 1'b1;
                    tts_start <= 1'b1;
                end else begin
                    drop_flag <= 1'b1;
                end
            end
            if (do_clear && !busy) begin
                buf_clear <= 1'b1;
                done_flag <= 1'b0;
                drop_flag <= 1'b0;
            end
            if (tts_done) begin
                busy      <= 1'b0;
                done_flag <= 1'b1;
            end

            // new snapshot once the previous one is acknowledged and no entry is in flight
            if (t_sreq == t_ack2 && gap == 2'd0 && !pop) begin
                t_snap     <= {(line_ready ? len : 8'd0), drop_flag, tts_play, done_flag, line_ready, busy};
                t_snap_cnt <= trbin;
                t_sreq     <= ~t_sreq;
            end
        end
    end

    // ---- unchanged speech engine ------------------------------------------------------------------
    wire [6:0]  lb_addr;
    wire [7:0]  lb_data;
    wire [19:0] sample;
    wire        sample_tick, pwm_raw;
    wire [7:0]  tts_fade;

    line_buf u_buf (
        .clk(TTS_CLK), .rst(trst), .rx_data(ch_data), .rx_valid(ch_valid),
        .consume(tts_done), .clear(buf_clear), .rd_addr(lb_addr), .rd_data(lb_data),
        .len(len), .line_ready(line_ready));

    tts_engine u_tts (
        .clk(TTS_CLK), .rst(trst), .start(tts_start), .done(tts_done),
        .lb_addr(lb_addr), .lb_data(lb_data), .lb_len(len),
        .tick(sample_tick), .sample(sample), .play(tts_play), .fade(tts_fade));

    pwm10 #(.PERIOD(PWM_PERIOD), .REPEAT(PWM_REPEAT)) u_pwm (
        .clk(TTS_CLK), .rst(trst), .sample_in(sample), .fade(tts_fade),
        .sample_tick(sample_tick), .pwm_out(pwm_raw));

    assign PWM_OUT = pwm_raw & tts_play;     // muted unless speech or its fades are played
endmodule
