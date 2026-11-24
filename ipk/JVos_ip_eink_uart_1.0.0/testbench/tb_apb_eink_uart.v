`timescale 1ns/1ps
// Testbench of apb_eink_uart: APB master tasks, a serial monitor on txd_o and a serial transmitter on rxd_i.
//   1. ID and BAUD register
//   2. internal loopback: 4 bytes written, read back in order, flags
//   3. TX FIFO depth: 16 bytes written back to back, all 16 seen by the monitor on txd_o
//   4. RX FIFO overrun: 20 bytes in loopback without reading: the 16 oldest are kept, OVERRUN flag, flush
//   5. external receive on rxd_i (230400 baud) incl. a frame error
// Prints PASS / FAIL and the number of errors.
module tb_apb_eink_uart;

    localparam real CLK_PERIOD = 83.3333;      // 12 MHz
    localparam real BIT_NS     = 4340.28;      // 1 / 230400 baud

    reg         pclk = 0, presetn = 0;
    reg  [31:0] paddr = 0, pwdata = 0;
    reg         psel = 0, penable = 0, pwrite = 0;
    wire [31:0] prdata;
    wire        pready, pslverr, txd_o;
    reg         rxd_i = 1;

    apb_eink_uart dut (
        .pclk(pclk), .presetn(presetn), .paddr(paddr), .psel(psel), .penable(penable), .pwrite(pwrite),
        .pwdata(pwdata), .prdata(prdata), .pready(pready), .pslverr(pslverr), .txd_o(txd_o), .rxd_i(rxd_i)
    );

    always #(CLK_PERIOD / 2.0) pclk = ~pclk;

    integer errors = 0;

    // ---- APB master
    task apb_write(input [4:0] a, input [31:0] d);
        begin
            @(posedge pclk); #1;
            paddr = {25'd0, a, 2'b00}; pwdata = d; pwrite = 1; psel = 1; penable = 0;
            @(posedge pclk); #1; penable = 1;
            @(posedge pclk); #1; psel = 0; penable = 0; pwrite = 0;
        end
    endtask

    task apb_read(input [4:0] a, output [31:0] d);
        begin
            @(posedge pclk); #1;
            paddr = {25'd0, a, 2'b00}; pwrite = 0; psel = 1; penable = 0;
            @(posedge pclk); #1; penable = 1;
            @(posedge pclk); d = prdata; #1; psel = 0; penable = 0;
        end
    endtask

    localparam A_ID = 0, A_DATA = 1, A_STATUS = 2, A_CONTROL = 3, A_BAUD = 4;

    task check(input [255:0] name, input [31:0] got, input [31:0] expected);
        begin
            if (got !== expected) begin
                errors = errors + 1;
                $display("FAIL %0s: got 0x%08h expected 0x%08h", name, got, expected);
            end else begin
                $display("ok   %0s: 0x%08h", name, got);
            end
        end
    endtask

    // ---- serial transmitter on rxd_i (stop_ok = 0 gives a frame error)
    task serial_send(input [7:0] b, input stop_ok);
        integer i;
        begin
            rxd_i = 0; #(BIT_NS);
            for (i = 0; i < 8; i = i + 1) begin rxd_i = b[i]; #(BIT_NS); end
            rxd_i = stop_ok ? 1'b1 : 1'b0; #(BIT_NS);
            rxd_i = 1; #(BIT_NS * 2);
        end
    endtask

    // ---- serial monitor on txd_o: collects the bytes
    reg [7:0] mon [0:63];
    integer   mon_n = 0;
    reg [7:0] mon_b;
    integer   mi;
    initial begin
        forever begin
            @(negedge txd_o);
            #(BIT_NS * 1.5);
            for (mi = 0; mi < 8; mi = mi + 1) begin mon_b[mi] = txd_o; #(BIT_NS); end
            mon[mon_n] = mon_b;
            mon_n = mon_n + 1;
        end
    end

    reg [31:0] r;
    integer k;

    initial begin
        repeat (10) @(posedge pclk);
        presetn = 1;
        repeat (5) @(posedge pclk);

        // 1. ID and baud register
        apb_read(A_ID, r);              check("ID", r, 32'h55415446);
        apb_read(A_BAUD, r);            check("BAUD reset value", r, 32'd20133);

        // 2. loopback
        apb_write(A_CONTROL, 32'h8);    // loopback on
        apb_write(A_DATA, 32'h55); apb_write(A_DATA, 32'hA3); apb_write(A_DATA, 32'h00); apb_write(A_DATA, 32'hFF);
        #(BIT_NS * 10 * 5);
        apb_read(A_STATUS, r);          check("loopback RX_COUNT", (r >> 8) & 32'h1F, 32'd4);
        apb_read(A_DATA, r);            check("loopback byte 0", r, 32'h155);
        apb_read(A_DATA, r);            check("loopback byte 1", r, 32'h1A3);
        apb_read(A_DATA, r);            check("loopback byte 2", r, 32'h100);
        apb_read(A_DATA, r);            check("loopback byte 3", r, 32'h1FF);
        apb_read(A_DATA, r);            check("empty read (valid = 0)", r, 32'h000);
        apb_read(A_STATUS, r);          check("status: TX_IDLE only", r & 32'h7F, 32'h08);
        apb_write(A_CONTROL, 32'h0);    // loopback off

        // 3. TX FIFO depth: 16 bytes back to back on txd_o
        mon_n = 0;
        for (k = 0; k < 16; k = k + 1) apb_write(A_DATA, 32'h30 + k);
        apb_read(A_STATUS, r);
        $display("TX_COUNT after 16 writes: %0d (one byte may already be in the shifter)", (r >> 16) & 32'h1F);
        #(BIT_NS * 10 * 18);
        check("monitor byte count", mon_n, 32'd16);
        for (k = 0; k < 16; k = k + 1) check("monitor byte", mon[k], 32'h30 + k);
        apb_read(A_STATUS, r);          check("TX_IDLE after 16 bytes", (r >> 3) & 1, 32'd1);

        // 4. RX FIFO overrun: 20 bytes in loopback, nothing read
        apb_write(A_CONTROL, 32'h8);
        for (k = 0; k < 20; k = k + 1) begin
            apb_read(A_STATUS, r);
            while (r[4]) begin apb_read(A_STATUS, r); end            // wait while the TX FIFO is full
            apb_write(A_DATA, 32'h40 + k);
        end
        #(BIT_NS * 10 * 22);
        apb_read(A_STATUS, r);
        check("overrun: RX_COUNT", (r >> 8) & 32'h1F, 32'd16);
        check("overrun: RX_FULL", (r >> 1) & 1, 32'd1);
        check("overrun: OVERRUN flag", (r >> 2) & 1, 32'd1);
        for (k = 0; k < 16; k = k + 1) begin
            apb_read(A_DATA, r);
            check("overrun: oldest bytes kept", r, 32'h100 + 32'h40 + k);
        end
        apb_read(A_DATA, r);            check("overrun: FIFO empty", r, 32'h000);
        apb_write(A_CONTROL, 32'hC);    // clear flags, loopback stays on
        apb_read(A_STATUS, r);          check("overrun flag cleared", (r >> 2) & 1, 32'd0);
        apb_write(A_CONTROL, 32'h0);    // loopback off

        // 5. external receive on rxd_i
        serial_send(8'h5A, 1);
        serial_send(8'hA5, 1);
        serial_send(8'h00, 0);          // frame error
        apb_read(A_STATUS, r);          check("external RX_COUNT", (r >> 8) & 32'h1F, 32'd3);
        check("FRAME_ERR flag", (r >> 6) & 1, 32'd1);
        apb_read(A_DATA, r);            check("external byte 0", r, 32'h15A);
        apb_read(A_DATA, r);            check("external byte 1", r, 32'h1A5);
        apb_read(A_DATA, r);            check("external byte 2 (frame error)", r, 32'h300);

        // 6. flush
        apb_write(A_CONTROL, 32'h8);
        apb_write(A_DATA, 32'h11); apb_write(A_DATA, 32'h22);
        #(BIT_NS * 10 * 3);
        apb_write(A_CONTROL, 32'h8 | 32'h1);   // clear RX FIFO
        apb_read(A_STATUS, r);          check("flush RX_COUNT", (r >> 8) & 32'h1F, 32'd0);

        if (errors == 0) $display("PASS: all checks ok");
        else             $display("FAIL: %0d error(s)", errors);
        $finish;
    end

    // watchdog
    initial begin
        #(50_000_000);
        $display("FAIL: timeout");
        $finish;
    end

endmodule
