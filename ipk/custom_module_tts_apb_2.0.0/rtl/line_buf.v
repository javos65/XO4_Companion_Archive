// 2 x 128-byte text line buffer (ping-pong).
// A line is complete when CR or LF arrives (with at least one stored byte) or when 128 bytes are stored.
// Only printable ASCII (0x20..0x7E) is stored. Received lines are queued: one bank is filled by the UART
// while the other one is read by the TTS engine. A completed line stays in its bank until the engine
// pulses "consume" (end of speech); the next queued line is then available immediately (line_ready).
// If both banks are occupied, a further line is dropped as a whole (until its line end).
// A '#' starts and ends a tone command (#Hz,ms#); a line that ends (CR / LF, or 128 characters) while a command is
// still open is ignored as a whole.
module line_buf (
    input  wire       clk,
    input  wire       rst,
    input  wire [7:0] rx_data,
    input  wire       rx_valid,
    input  wire       consume,      // one-clock pulse: the oldest line has been spoken, free its bank
    input  wire       clear,        // one-clock pulse: discard everything received so far
    input  wire [6:0] rd_addr,      // read address inside the oldest line (0..127)
    output wire [7:0] rd_data,
    output wire [7:0] len,          // length of the oldest line, 1..128
    output wire       line_ready    // a complete line is waiting
);
    localparam [7:0] LINE_MAX = 8'd128;
    reg [7:0] mem0 [0:127];
    reg [7:0] mem1 [0:127];
    reg       wsel, rsel;           // bank being filled, bank to be read next
    reg [1:0] full;                 // bank holds a complete line
    reg [7:0] wlen;                 // characters stored in the bank being filled
    reg [7:0] len0, len1;
    reg       drop;                 // dropping a line because both banks are occupied
    reg       in_cmd;               // inside a #...# tone command

    assign rd_data    = rsel ? mem1[rd_addr] : mem0[rd_addr];
    assign len        = rsel ? len1 : len0;
    assign line_ready = full[rsel];

    wire is_eol       = (rx_data == 8'h0D) || (rx_data == 8'h0A);
    wire is_printable = (rx_data >= 8'h20) && (rx_data <= 8'h7E);
    wire accept       = rx_valid && !full[wsel] && !drop;
    wire store        = accept && !is_eol && is_printable;
    wire is_hash      = (rx_data == 8'h23);
    wire next_in_cmd  = in_cmd ^ (store && is_hash);
    wire at_limit     = store && wlen == (LINE_MAX - 8'd1);
    wire eol_line     = accept && is_eol && wlen != 8'd0;
    wire finish       = (eol_line && !in_cmd) || (accept && at_limit && !next_in_cmd);
    wire discard      = (eol_line && in_cmd) || (accept && at_limit && next_in_cmd);   // open command: ignore the line
    wire [7:0] newlen = store ? LINE_MAX : wlen;

    always @(posedge clk) begin
        if (store && !wsel) mem0[wlen[6:0]] <= rx_data;
        if (store &&  wsel) mem1[wlen[6:0]] <= rx_data;
    end

    always @(posedge clk) begin
        if (rst || clear) begin
            wsel <= 1'b0;
            rsel <= 1'b0;
            full <= 2'b00;
            wlen <= 8'd0;
            len0 <= 8'd0;
            len1 <= 8'd0;
            drop <= 1'b0;
            in_cmd <= 1'b0;
        end else begin
            if (consume) begin
                full[rsel] <= 1'b0;
                rsel       <= ~rsel;
            end
            if (rx_valid && (full[wsel] || drop)) begin
                // no free bank: ignore the rest of this line
                if (is_eol) drop <= 1'b0;
                else        drop <= 1'b1;
            end
            if (store) begin
                wlen   <= wlen + 8'd1;
                in_cmd <= next_in_cmd;
            end
            if (discard) begin
                wlen   <= 8'd0;
                in_cmd <= 1'b0;
                if (at_limit) drop <= 1'b1;          // the rest of this long line is ignored too
            end
            if (finish) begin
                if (wsel) len1 <= newlen; else len0 <= newlen;
                full[wsel] <= 1'b1;
                wsel       <= ~wsel;
                wlen       <= 8'd0;
            end
        end
    end
endmodule
