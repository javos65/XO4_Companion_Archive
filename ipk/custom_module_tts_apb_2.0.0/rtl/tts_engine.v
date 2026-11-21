// TTS engine: processor + sample FIFO + playback control + fade in / fade out.
// start  : begin speaking the text that is in the line buffer
// done   : one-clock pulse when the speech and its fade-out are finished
// tick   : sample tick from pwm10 (one per 125 us)
// sample : 10-bit value for pwm10 (512 = mid-scale when idle)
// play   : high while speech is played, including the fades (PWM output enabled)
// fade   : gain for the PWM duty, 0..255; ramps up before the first sample and down after the last
//          one, so the output never jumps between "off" and the speech level (no click)
module tts_engine #(
    parameter PREFILL  = 8,
    parameter FADE_DIV = 6        // audio ticks per fade step: 255 steps x 6 / 16 kHz = 96 ms
) (
    input  wire       clk,
    input  wire       rst,
    input  wire       start,
    output reg        done,
    output wire [6:0] lb_addr,
    input  wire [7:0] lb_data,
    input  wire [7:0] lb_len,
    input  wire       tick,
    output wire [19:0] sample,
    output wire       play,
    output wire [7:0] fade
);
    wire        cpu_done, smp_we, full, empty;
    wire [19:0] smp_data, head;
    wire [4:0]  count;
    reg         playing;
    reg  [7:0]  fade_r;
    reg  [1:0]  fdir;             // 0 hold, 1 fading in, 2 fading out
    reg  [3:0]  fdiv;

    ttscpu u_cpu (
        .clk(clk), .rst(rst), .start(start), .done(cpu_done),
        .lb_addr(lb_addr), .lb_data(lb_data), .lb_len(lb_len),
        .smp_we(smp_we), .smp_data(smp_data), .smp_full(full));

    wire pop = tick && playing && !empty;

    sample_fifo u_fifo (
        .clk(clk), .rst(rst), .clear(start), .push(smp_we), .din(smp_data),
        .pop(pop), .head(head), .full(full), .empty(empty), .count(count));

    assign sample = (playing && !empty) ? head : {10'd512, 10'd512};
    assign play   = playing;
    assign fade   = fade_r;

    reg busy;
    always @(posedge clk) begin
        done <= 1'b0;
        if (rst) begin
            playing <= 1'b0;
            busy    <= 1'b0;
            fade_r  <= 8'd0;
            fdir    <= 2'd0;
            fdiv    <= 4'd0;
        end else if (start) begin
            playing <= 1'b0;
            busy    <= 1'b1;
            fade_r  <= 8'd0;
            fdir    <= 2'd0;
            fdiv    <= 4'd0;
        end else begin
            // fade ramp: one step every FADE_DIV audio ticks
            if (tick && fdir != 2'd0) begin
                if (fdiv == FADE_DIV - 1) begin
                    fdiv <= 4'd0;
                    if (fdir == 2'd1) begin
                        fade_r <= fade_r + 8'd1;
                        if (fade_r == 8'd254) fdir <= 2'd0;
                    end else if (fade_r != 8'd0) begin
                        fade_r <= fade_r - 8'd1;
                    end
                end else begin
                    fdiv <= fdiv + 4'd1;
                end
            end
            if (busy) begin
                if (!playing) begin
                    // start playing when enough samples are queued, or when the processor has finished
                    if (count >= PREFILL || (cpu_done && !empty)) begin
                        playing <= 1'b1;
                        fdir    <= 2'd1;
                        fdiv    <= 4'd0;
                    end else if (cpu_done && empty) begin
                        busy <= 1'b0;
                        done <= 1'b1;
                    end
                end else if (fdir != 2'd2 && cpu_done && empty) begin
                    fdir <= 2'd2;                  // all samples played: fade out
                    fdiv <= 4'd0;
                end else if (fdir == 2'd2 && fade_r == 8'd0) begin
                    playing <= 1'b0;
                    busy    <= 1'b0;
                    done    <= 1'b1;
                    fdir    <= 2'd0;
                end
            end
        end
    end
endmodule
