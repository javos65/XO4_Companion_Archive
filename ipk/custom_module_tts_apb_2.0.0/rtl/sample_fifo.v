// 16 x 20-bit sample FIFO (two 10-bit PWM values per audio sample) between the processor and the PWM block.
module sample_fifo (
    input  wire       clk,
    input  wire       rst,
    input  wire       clear,
    input  wire       push,
    input  wire [19:0] din,
    input  wire       pop,
    output wire [19:0] head,
    output wire       full,
    output wire       empty,
    output wire [4:0] count
);
    reg [19:0] mem [0:15];
    reg [3:0] wp, rp;
    reg [4:0] cnt;

    assign head  = mem[rp];
    assign full  = (cnt >= 5'd15);        // one slot of margin: the processor may push once more
    assign empty = (cnt == 5'd0);
    assign count = cnt;

    wire do_push = push && (cnt != 5'd16);
    wire do_pop  = pop && (cnt != 5'd0);

    always @(posedge clk) begin
        if (rst || clear) begin
            wp  <= 4'd0;
            rp  <= 4'd0;
            cnt <= 5'd0;
        end else begin
            if (do_push) begin
                mem[wp] <= din;
                wp <= wp + 4'd1;
            end
            if (do_pop) rp <= rp + 4'd1;
            cnt <= cnt + {4'd0, do_push} - {4'd0, do_pop};
        end
    end
endmodule
