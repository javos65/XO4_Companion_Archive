// Self-checking testbench of mkr_rgb_spi.
// PCLK = 20 MHz. Fills the buffer, reads it back, sends two frames and decodes
// SDI on every rising edge of CKI like an APA102 chain would.
`timescale 1ns/1ps
module tb_mkr_rgb_spi;
    localparam integer N = 84;
    localparam integer TOTAL_BITS = 32 + N*32 + 64;

    reg         PCLK = 1'b0;
    reg         PRESETN = 1'b0;
    reg  [9:0]  PADDR = 10'd0;
    reg         PSEL = 1'b0, PENABLE = 1'b0, PWRITE = 1'b0;
    reg  [31:0] PWDATA = 32'd0;
    wire [31:0] PRDATA;
    wire        PREADY, PSLVERR;
    wire        LED_SDI, LED_CKI;

    always #25 PCLK = ~PCLK;     // 20 MHz

    mkr_rgb_spi #(.NUM_LEDS(N)) dut (
        .PCLK(PCLK), .PRESETN(PRESETN), .PADDR(PADDR), .PSEL(PSEL), .PENABLE(PENABLE),
        .PWRITE(PWRITE), .PWDATA(PWDATA), .PRDATA(PRDATA), .PREADY(PREADY), .PSLVERR(PSLVERR),
        .LED_SDI(LED_SDI), .LED_CKI(LED_CKI));

    integer errors = 0;

    task apb_write(input [9:0] a, input [31:0] d);
        begin
            @(posedge PCLK); #1;
            PADDR = a; PWDATA = d; PWRITE = 1'b1; PSEL = 1'b1; PENABLE = 1'b0;
            @(posedge PCLK); #1;
            PENABLE = 1'b1;
            @(posedge PCLK); #1;
            PSEL = 1'b0; PENABLE = 1'b0; PWRITE = 1'b0;
        end
    endtask

    task apb_read(input [9:0] a, output [31:0] d);
        begin
            @(posedge PCLK); #1;
            PADDR = a; PWRITE = 1'b0; PSEL = 1'b1; PENABLE = 1'b0;
            @(posedge PCLK); #1;
            PENABLE = 1'b1;
            @(posedge PCLK);
            d = PRDATA;
            #1 PSEL = 1'b0; PENABLE = 1'b0;
        end
    endtask

    // ---------------------------------------------------------------- chain model
    reg [TOTAL_BITS-1:0] rx;
    integer nbits = 0;
    realtime t_last = 0, t_min = 1.0e9;
    always @(posedge LED_CKI) begin
        if (nbits > 0 && ($realtime - t_last) < t_min) t_min = $realtime - t_last;
        t_last = $realtime;
        if (nbits < TOTAL_BITS) rx[TOTAL_BITS-1-nbits] = LED_SDI;
        nbits = nbits + 1;
    end

    function [31:0] pix(input integer i, input integer seed);
        pix = {3'b111, 5'(i % 32), 8'(i*3 + seed), 8'(i*7 + seed), 8'(255 - i - seed)};
    endfunction

    task check_frame(input integer seed);
        integer i;
        reg [31:0] w;
        begin
            if (nbits != TOTAL_BITS) begin
                $display("ERROR: %0d CKI edges, expected %0d", nbits, TOTAL_BITS); errors = errors + 1;
            end
            if (rx[TOTAL_BITS-1 -: 32] !== 32'd0) begin
                $display("ERROR: start frame %h", rx[TOTAL_BITS-1 -: 32]); errors = errors + 1;
            end
            for (i = 0; i < N; i = i + 1) begin
                w = rx[TOTAL_BITS-33-32*i -: 32];
                if (w !== pix(i, seed)) begin
                    $display("ERROR: LED %0d got %h expected %h", i, w, pix(i, seed)); errors = errors + 1;
                end
            end
            if (rx[63:0] !== {64{1'b1}}) begin
                $display("ERROR: end frame %h", rx[63:0]); errors = errors + 1;
            end
        end
    endtask

    task send_frame(input integer seed);
        reg [31:0] d;
        integer timeout;
        begin
            nbits = 0;
            apb_write(10'h004, 32'h1);
            apb_read(10'h004, d);
            if (d[1] !== 1'b0) begin $display("ERROR: READY not cleared after START"); errors = errors + 1; end
            // a write during the frame must be ignored
            apb_write(10'h00C, 32'hDEAD_BEEF);
            timeout = 0;
            d = 0;
            while (d[1] !== 1'b1 && timeout < 10000) begin apb_read(10'h004, d); timeout = timeout + 1; end
            if (timeout >= 10000) begin $display("ERROR: READY timeout"); errors = errors + 1; end
            repeat (4) @(posedge PCLK);
            if (LED_CKI !== 1'b0) begin $display("ERROR: CKI not idle low"); errors = errors + 1; end
            check_frame(seed);
        end
    endtask

    integer i;
    reg [31:0] d;
    realtime t0, t1;
    initial begin
        repeat (4) @(posedge PCLK);
        PRESETN = 1'b1;

        apb_read(10'h000, d);
        if (d !== 32'h5247_4231) begin $display("ERROR: ID %h", d); errors = errors + 1; end
        apb_read(10'h008, d);
        if (d !== N) begin $display("ERROR: NLEDS %0d", d); errors = errors + 1; end
        apb_read(10'h004, d);
        if (d !== 32'h2) begin $display("ERROR: CTRL after reset %h", d); errors = errors + 1; end

        for (i = 0; i < N; i = i + 1) apb_write(10'h00C + 4*i, pix(i, 0));
        for (i = 0; i < N; i = i + 1) begin
            apb_read(10'h00C + 4*i, d);
            if (d !== pix(i, 0)) begin $display("ERROR: readback PIX[%0d] %h", i, d); errors = errors + 1; end
        end

        t0 = $realtime;
        send_frame(0);
        t1 = $realtime;
        $display("frame 1: %0d bits, min CKI period %0.1f ns (%0.2f MHz), %0.1f us incl. polling",
                 nbits, t_min, 1000.0/t_min, (t1-t0)/1000.0);
        if (t_min < 99.0 || t_min > 101.0) begin $display("ERROR: CKI period"); errors = errors + 1; end

        // second frame with new content: checks the buffer is rewritable and the engine restarts
        for (i = 0; i < N; i = i + 1) apb_write(10'h00C + 4*i, pix(i, 17));
        send_frame(17);

        if (errors == 0) $display("TEST PASSED");
        else             $display("TEST FAILED: %0d errors", errors);
        $finish;
    end
endmodule
