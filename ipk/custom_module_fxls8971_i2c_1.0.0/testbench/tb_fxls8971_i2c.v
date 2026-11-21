`timescale 1ns/1ps
// Self-checking testbench for fxls8971_i2c.
// The target model behaves like the FXLS8971CF register interface: 7-bit address 0x18,
// a register pointer set by the first written byte, auto-increment on read and write,
// WHO_AM_I (0x13) = 0x83. It also stretches SCL on the first ACK to test the wait logic.
module tb_fxls8971_i2c;
    reg         pclk = 0, presetn = 0;
    reg  [4:0]  paddr = 0;
    reg         psel = 0, penable = 0, pwrite = 0;
    reg  [31:0] pwdata = 0;
    wire [31:0] prdata;
    wire        pready, pslverr;
    tri1        scl, sda;               // pull-ups

    always #28.2 pclk = ~pclk;          // ~17.73 MHz

    fxls8971_i2c dut (
        .PCLK(pclk), .PRESETN(presetn), .PADDR(paddr), .PSEL(psel), .PENABLE(penable),
        .PWRITE(pwrite), .PWDATA(pwdata), .PRDATA(prdata), .PREADY(pready), .PSLVERR(pslverr),
        .I2C_SCL(scl), .I2C_SDA(sda));

    // ------------------------------------------------------------ target model
    reg [7:0] mem [0:255];
    reg       t_sda_low = 0, t_scl_low = 0;
    assign sda = t_sda_low ? 1'b0 : 1'bz;
    assign scl = t_scl_low ? 1'b0 : 1'bz;

    integer i;
    initial begin
        for (i = 0; i < 256; i = i + 1) mem[i] = i[7:0] ^ 8'h5A;
        mem[8'h13] = 8'h83;
    end

    reg [7:0] t_ptr = 0;
    reg       stretched = 0;
    reg       started = 0;
    event     ev_start, ev_stop;
    integer   errors = 0;
    integer   starts = 0, stops = 0;

    // bus conditions, counted after reset (the X->1 edge of the pull-up at time 0 is not a STOP)
    always @(negedge sda) if (presetn && scl === 1'b1) begin starts = starts + 1; -> ev_start; end
    always @(posedge sda) if (presetn && scl === 1'b1) begin stops  = stops + 1;  -> ev_stop;  end

    task automatic t_get_byte(output [7:0] b);
        integer k;
        begin
            for (k = 7; k >= 0; k = k - 1) begin
                @(posedge scl); b[k] = sda;
            end
        end
    endtask

    task automatic t_ack(input ack, input do_stretch);
        begin
            @(negedge scl); #100 t_sda_low = ack;
            if (do_stretch) begin t_scl_low = 1; #20000 t_scl_low = 0; end
            @(negedge scl); #100 t_sda_low = 0;
        end
    endtask

    task automatic t_send_byte(input [7:0] b, output nack);
        integer k;
        begin
            for (k = 7; k >= 0; k = k - 1) begin
                t_sda_low = ~b[k];
                @(negedge scl); #100;
            end
            t_sda_low = 0;
            @(posedge scl); nack = sda;
            @(negedge scl); #100;
        end
    endtask

    // transaction engine: restarted on every START
    reg [7:0] t_addr, t_byte;
    reg       t_nack, t_first;
    event ev_go;
    always @(ev_start) begin
        disable target_body;
        t_sda_low = 0;
        #1 -> ev_go;
    end
    always @(ev_go) begin : target_body
        @(negedge scl); #100;
        t_get_byte(t_addr);
        if (t_addr[7:1] != 7'h18) begin
            @(negedge scl);                     // not us: no ACK
        end else begin
            t_ack(1'b1, !stretched); stretched = 1;
            if (t_addr[0] == 1'b0) begin        // write: pointer, then data
                t_first = 1;
                forever begin
                    t_get_byte(t_byte);
                    if (t_first) t_ptr = t_byte; else begin mem[t_ptr] = t_byte; t_ptr = t_ptr + 1; end
                    t_first = 0;
                    t_ack(1'b1, 1'b0);
                end
            end else begin                      // read with auto-increment
                forever begin
                    t_send_byte(mem[t_ptr], t_nack);
                    t_ptr = t_ptr + 1;
                    if (t_nack) disable target_body;
                end
            end
        end
    end
    always @(ev_stop) disable target_body;

    // ------------------------------------------------------------ APB master
    task apb_write(input [4:0] a, input [31:0] d);
        begin
            @(posedge pclk); paddr <= a; pwdata <= d; pwrite <= 1; psel <= 1; penable <= 0;
            @(posedge pclk); penable <= 1;
            @(posedge pclk); psel <= 0; penable <= 0; pwrite <= 0;
        end
    endtask
    task apb_read(input [4:0] a, output [31:0] d);
        begin
            @(posedge pclk); paddr <= a; pwrite <= 0; psel <= 1; penable <= 0;
            @(posedge pclk); penable <= 1;
            @(negedge pclk); d = prdata;
            @(posedge pclk); psel <= 0; penable <= 0;
        end
    endtask

    localparam CMD_START = 1, CMD_STOP = 2, CMD_WRITE = 4, CMD_READ = 8, CMD_NACK = 16;
    localparam R_ID = 5'h00, R_PRE = 5'h04, R_CMD = 5'h08, R_TX = 5'h0C, R_RX = 5'h10, R_ST = 5'h14;

    task cmd(input [4:0] c, input [7:0] tx, output [31:0] st);
        begin
            apb_write(R_TX, tx);
            apb_write(R_CMD, c);
            st = 1;
            while (st[0]) apb_read(R_ST, st);
        end
    endtask

    task check(input [255:0] what, input [31:0] got, input [31:0] exp);
        begin
            if (got !== exp) begin
                errors = errors + 1;
                $display("FAIL %0s: got %h expected %h", what, got, exp);
            end else
                $display("ok   %0s = %h", what, got);
        end
    endtask

    reg [31:0] st, d;
    initial begin
        #500 presetn = 1;
        apb_read(R_ID, d);     check("ID", d, 32'h49324331);
        apb_write(R_PRE, 10);  // ~400 kHz
        apb_read(R_PRE, d);    check("PRESCALE", d, 10);

        // ---- read WHO_AM_I: S 0x30 A 0x13 A Sr 0x31 A [data] N P
        cmd(CMD_START | CMD_WRITE, 8'h30, st); check("addr W ack", st[1], 0);
        cmd(CMD_WRITE, 8'h13, st);             check("reg ack", st[1], 0);
        cmd(CMD_START | CMD_WRITE, 8'h31, st); check("addr R ack", st[1], 0);
        cmd(CMD_READ | CMD_NACK | CMD_STOP, 8'h00, st);
        apb_read(R_RX, d);                     check("WHO_AM_I", d, 8'h83);
        check("bus released", st[3], 0);

        // ---- write two registers 0x15/0x16 = 0xA5, 0x3C
        cmd(CMD_START | CMD_WRITE, 8'h30, st);
        cmd(CMD_WRITE, 8'h15, st);
        cmd(CMD_WRITE, 8'hA5, st);
        cmd(CMD_WRITE | CMD_STOP, 8'h3C, st);
        check("mem[15]", mem[8'h15], 8'hA5);
        check("mem[16]", mem[8'h16], 8'h3C);

        // ---- burst read 6 bytes from 0x04
        cmd(CMD_START | CMD_WRITE, 8'h30, st);
        cmd(CMD_WRITE, 8'h04, st);
        cmd(CMD_START | CMD_WRITE, 8'h31, st);
        for (i = 0; i < 6; i = i + 1) begin
            cmd(CMD_READ | ((i == 5) ? (CMD_NACK | CMD_STOP) : 0), 8'h00, st);
            apb_read(R_RX, d);
            check("burst", d, (8'h04 + i) ^ 8'h5A);
        end

        // ---- wrong address must NACK
        cmd(CMD_START | CMD_WRITE, 8'h40, st); check("NACK on 0x20", st[1], 1);
        cmd(CMD_STOP, 8'h00, st);
        check("SCL idle high", st[4], 1);
        check("SDA idle high", st[5], 1);
        check("START count", starts, 6);
        check("STOP count", stops, 4);

        if (errors == 0) $display("TEST PASSED");
        else             $display("TEST FAILED: %0d errors", errors);
        $finish;
    end

    initial begin #20_000_000 $display("TEST FAILED: timeout"); $finish; end
endmodule
