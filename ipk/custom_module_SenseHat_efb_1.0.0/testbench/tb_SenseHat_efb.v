// Testbench for SenseHat_efb: an APB master drives the pass-through, a simple APB slave model
// (stand-in for lscc_efb, 8-bit registers on 32-bit words, one wait state) answers behind it.
// Checks: every APB signal reaches the slave unchanged, data and ready/error return unchanged,
// the I2C register window offsets 0x128..0x14C are reachable, and a PSLVERR is passed back.
`timescale 1ns/1ps
module tb_SenseHat_efb;
    reg         pclk = 1'b0;
    reg         presetn = 1'b0;
    always #41.667 pclk = ~pclk;      // 12 MHz APB clock

    // master side
    reg  [9:0]  paddr;
    reg         psel, penable, pwrite;
    reg  [31:0] pwdata;
    wire [31:0] prdata;
    wire        pready, pslverr;
    // slave side
    wire [9:0]  efb_paddr;
    wire        efb_psel, efb_penable, efb_pwrite;
    wire [31:0] efb_pwdata;
    reg  [31:0] efb_prdata;
    reg         efb_pready;
    reg         efb_pslverr;

    SenseHat_efb dut (
        .apb_paddr_i(paddr), .apb_psel_i(psel), .apb_penable_i(penable), .apb_pwrite_i(pwrite),
        .apb_pwdata_i(pwdata), .apb_prdata_o(prdata), .apb_pready_o(pready), .apb_pslverr_o(pslverr),
        .efb_paddr_o(efb_paddr), .efb_psel_o(efb_psel), .efb_penable_o(efb_penable),
        .efb_pwrite_o(efb_pwrite), .efb_pwdata_o(efb_pwdata), .efb_prdata_i(efb_prdata),
        .efb_pready_i(efb_pready), .efb_pslverr_i(efb_pslverr));

    // slave model: 8-bit registers, index = address[9:2], one wait state, error above 0x1FF
    reg [7:0] regs [0:255];
    reg       phase_seen;
    integer   errors = 0;
    always @(posedge pclk) begin
        efb_pready  <= 1'b0;
        efb_pslverr <= 1'b0;
        if (efb_psel && efb_penable && !efb_pready) begin
            efb_pready <= 1'b1;
            if (efb_paddr > 10'h1FF) begin
                efb_pslverr <= 1'b1;
                efb_prdata  <= 32'hDEAD_BEEF;
            end else if (efb_pwrite) begin
                regs[efb_paddr[9:2]] <= efb_pwdata[7:0];
            end else begin
                efb_prdata <= {24'h0, regs[efb_paddr[9:2]]};
            end
        end
    end

    // pass-through checks, evaluated every clock
    always @(posedge pclk) if (presetn) begin
        if (efb_paddr   !== paddr)   begin errors = errors + 1; $display("ERR paddr");   end
        if (efb_psel    !== psel)    begin errors = errors + 1; $display("ERR psel");    end
        if (efb_penable !== penable) begin errors = errors + 1; $display("ERR penable"); end
        if (efb_pwrite  !== pwrite)  begin errors = errors + 1; $display("ERR pwrite");  end
        if (efb_pwdata  !== pwdata)  begin errors = errors + 1; $display("ERR pwdata");  end
        if (prdata      !== efb_prdata)  begin errors = errors + 1; $display("ERR prdata");  end
        if (pready      !== efb_pready)  begin errors = errors + 1; $display("ERR pready");  end
        if (pslverr     !== efb_pslverr) begin errors = errors + 1; $display("ERR pslverr"); end
    end

    task apb_write(input [9:0] a, input [31:0] d);
        begin
            @(posedge pclk); paddr <= a; pwdata <= d; pwrite <= 1'b1; psel <= 1'b1; penable <= 1'b0;
            @(posedge pclk); penable <= 1'b1;
            @(posedge pclk); while (!pready) @(posedge pclk);
            psel <= 1'b0; penable <= 1'b0; pwrite <= 1'b0;
        end
    endtask

    task apb_read(input [9:0] a, output [31:0] d, output e);
        begin
            @(posedge pclk); paddr <= a; pwrite <= 1'b0; psel <= 1'b1; penable <= 1'b0;
            @(posedge pclk); penable <= 1'b1;
            @(posedge pclk); while (!pready) @(posedge pclk);
            d = prdata; e = pslverr;
            psel <= 1'b0; penable <= 1'b0;
        end
    endtask

    integer i;
    reg [31:0] rd;
    reg        er;
    reg [9:0]  offs [0:9];
    initial begin
        offs[0]=10'h128; offs[1]=10'h12C; offs[2]=10'h130; offs[3]=10'h134; offs[4]=10'h138;
        offs[5]=10'h13C; offs[6]=10'h140; offs[7]=10'h144; offs[8]=10'h148; offs[9]=10'h14C;
        paddr = 0; psel = 0; penable = 0; pwrite = 0; pwdata = 0;
        efb_prdata = 0; efb_pready = 0; efb_pslverr = 0;
        for (i = 0; i < 256; i = i + 1) regs[i] = 8'h00;
        repeat (4) @(posedge pclk);
        presetn = 1'b1;

        // write a pattern to each secondary I2C (I2C2) register and read it back
        for (i = 0; i < 10; i = i + 1) apb_write(offs[i], 32'h0000_0010 + i);
        for (i = 0; i < 10; i = i + 1) begin
            apb_read(offs[i], rd, er);
            if (rd !== (32'h0000_0010 + i) || er) begin
                errors = errors + 1;
                $display("ERR readback offset %h: %h (err %b)", offs[i], rd, er);
            end
        end
        // an access outside the window reaches the slave and its error returns unchanged
        apb_read(10'h3FC, rd, er);
        if (!er || rd !== 32'hDEAD_BEEF) begin errors = errors + 1; $display("ERR pslverr path"); end

        repeat (4) @(posedge pclk);
        if (errors == 0) $display("TB_SENSEHAT_EFB PASS");
        else             $display("TB_SENSEHAT_EFB FAIL errors=%0d", errors);
        $finish;
    end
endmodule
