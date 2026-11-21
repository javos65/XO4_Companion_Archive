// 10-bit audio sample to PWM.
//   PERIOD clocks per PWM period (carrier = 12 MHz / PERIOD), REPEAT periods per audio sample.
//   PERIOD x REPEAT clocks make one audio sample: at 24 MHz 750 x 4 = 3000 clocks = 8 kHz with a 32 kHz carrier.
//   The first half of the periods of a sample use sample_in[9:0], the second half sample_in[19:10].
// The 10-bit sample (0..1023) is scaled to the 0..PERIOD range: duty = (sample * PERIOD) >> 10
// (512 -> half of the period), and then multiplied by fade / 256 (fade = 255 means full level,
// 0 means no output). The duty is latched at every period start, and sample_tick pulses for one
// clock once per audio sample (every REPEAT periods), after the latch.
module pwm10 #(
    parameter PERIOD = 750,
    parameter REPEAT = 2
) (
    input  wire        clk,
    input  wire        rst,
    input  wire [19:0] sample_in,       // two 10-bit values: [9:0] for the first, [19:10] for the second PWM period
    input  wire [7:0]  fade,
    output reg         sample_tick,
    output reg         pwm_out
);
    localparam [10:0] P = PERIOD;

    reg [10:0] cnt;
    reg [10:0] duty;
    reg [2:0]  rep;

    wire [9:0]  smp    = (rep < (REPEAT / 2)) ? sample_in[9:0] : sample_in[19:10];
    wire [20:0] prod   = smp * P;
    wire [10:0] scaled = prod[20:10];
    wire [18:0] prod2  = scaled * fade;
    wire [10:0] faded  = (fade == 8'd255) ? scaled : prod2[18:8];

    always @(posedge clk) begin
        sample_tick <= 1'b0;
        if (rst) begin
            cnt     <= 11'd0;
            duty    <= 11'd0;
            rep     <= 3'd0;
            pwm_out <= 1'b0;
        end else begin
            if (cnt == PERIOD - 1) begin
                cnt  <= 11'd0;
                duty <= faded;
                if (rep == REPEAT - 1) begin
                    rep         <= 3'd0;
                    sample_tick <= 1'b1;
                end else begin
                    rep <= rep + 3'd1;
                end
            end else begin
                cnt <= cnt + 11'd1;
            end
            pwm_out <= (cnt < duty);
        end
    end
endmodule
