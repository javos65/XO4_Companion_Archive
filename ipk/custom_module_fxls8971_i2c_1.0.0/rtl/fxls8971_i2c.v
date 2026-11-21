// FXLS8971_I2C 1.0.0: APB I2C controller for the NXP FXLS8971CF accelerometer (single master).
//
// One clock (PCLK). The bus speed is set by PRESCALE: every SCL period is four
// quarter periods of (PRESCALE + 1) PCLK cycles, so
//     f_SCL = f_PCLK / (4 * (PRESCALE + 1))
// e.g. PCLK 17.73 MHz: PRESCALE 10 -> 403 kHz, PRESCALE 43 -> 100.7 kHz.
//
// The controller executes one byte-level command at a time. A write to CMD
// starts it; STATUS.BUSY stays high until the whole sequence is done:
//     [START or repeated START] -> [WRITE TXDATA | READ to RXDATA] -> [STOP]
// SCL clock stretching by a target is supported (the controller waits for SCL
// to really go high before it continues).
//
// Register map (word offsets, PADDR[4:2]):
//   0x00 ID        R   0x49324331 ("I2C1")
//   0x04 PRESCALE  RW  bits 15..0 quarter-period divider (reset 43 = ~100 kHz at 17.73 MHz)
//   0x08 CMD       RW  write starts a command (ignored while BUSY); read returns the last command
//                      bit0 START  generate a START (repeated START when the bus is already owned)
//                      bit1 STOP   generate a STOP after the byte
//                      bit2 WRITE  send TXDATA, receive the ACK bit into STATUS.RXNACK
//                      bit3 READ   receive one byte into RXDATA
//                      bit4 NACK   with READ: answer NACK (1) instead of ACK (0), for the last byte
//   0x0C TXDATA    RW  bits 7..0 byte to send
//   0x10 RXDATA    R   bits 7..0 last byte received
//   0x14 STATUS    R   bit0 BUSY   a command is running
//                      bit1 RXNACK the target answered NACK on the last WRITE
//                      bit2 DONE   a command finished (cleared by the next CMD write)
//                      bit3 OWNED  this master owns the bus (between START and STOP)
//                      bit4 SCL    level on the SCL pin
//                      bit5 SDA    level on the SDA pin
//
// The pins are open drain: the controller only ever drives them low; the pull-up
// resistors on the board pull them high.
module fxls8971_i2c #(
    parameter [15:0] PRESCALE_RESET = 16'd43
) (
    input  wire        PCLK,
    input  wire        PRESETN,
    input  wire [4:0]  PADDR,
    input  wire        PSEL,
    input  wire        PENABLE,
    input  wire        PWRITE,
    input  wire [31:0] PWDATA,
    output reg  [31:0] PRDATA,
    output wire        PREADY,
    output wire        PSLVERR,
    inout  wire        I2C_SCL,
    inout  wire        I2C_SDA
);
    localparam [31:0] ID_VALUE = 32'h4932_4331;

    localparam [2:0] A_ID = 3'd0, A_PRESCALE = 3'd1, A_CMD = 3'd2,
                     A_TXDATA = 3'd3, A_RXDATA = 3'd4, A_STATUS = 3'd5;

    localparam [1:0] S_IDLE = 2'd0, S_START = 2'd1, S_BITS = 2'd2, S_STOP = 2'd3;

    // ------------------------------------------------------------ pins
    reg  scl_oe;                    // 1: pull SCL low
    reg  sda_oe;                    // 1: pull SDA low
    assign I2C_SCL = scl_oe ? 1'b0 : 1'bz;
    assign I2C_SDA = sda_oe ? 1'b0 : 1'bz;

    reg [1:0] scl_sync, sda_sync;
    always @(posedge PCLK or negedge PRESETN)
        if (!PRESETN) begin
            scl_sync <= 2'b11;
            sda_sync <= 2'b11;
        end else begin
            scl_sync <= {scl_sync[0], I2C_SCL};
            sda_sync <= {sda_sync[0], I2C_SDA};
        end
    wire scl_in = scl_sync[1];
    wire sda_in = sda_sync[1];

    // ------------------------------------------------------------ registers
    reg [15:0] prescale;
    reg [4:0]  cmd;                 // {NACK, READ, WRITE, STOP, START}
    reg [7:0]  txdata;
    reg [7:0]  rxdata;
    reg        busy, rxnack, done, owned;

    assign PREADY  = 1'b1;
    assign PSLVERR = 1'b0;

    wire       wr       = PSEL & PENABLE & PWRITE;
    wire [2:0] waddr    = PADDR[4:2];
    wire       cmd_go   = wr && (waddr == A_CMD) && !busy;

    always @(*) begin
        case (PADDR[4:2])
            A_ID:       PRDATA = ID_VALUE;
            A_PRESCALE: PRDATA = {16'd0, prescale};
            A_CMD:      PRDATA = {27'd0, cmd};
            A_TXDATA:   PRDATA = {24'd0, txdata};
            A_RXDATA:   PRDATA = {24'd0, rxdata};
            A_STATUS:   PRDATA = {26'd0, sda_in, scl_in, owned, done, rxnack, busy};
            default:    PRDATA = 32'd0;
        endcase
    end

    // ------------------------------------------------------------ engine
    reg [1:0]  st;                  // sequence state
    reg [1:0]  ph;                  // quarter-period phase inside a START / bit / STOP
    reg [3:0]  bitcnt;              // 0..7 data bits, 8 = ACK bit
    reg [7:0]  sr;                  // shift register
    reg [15:0] divcnt;
    reg        smp;                 // SDA sampled in the high part of SCL

    wire c_start = cmd[0];
    wire c_stop  = cmd[1];
    wire c_write = cmd[2];
    wire c_read  = cmd[3];
    wire c_nack  = cmd[4];

    wire tick = (divcnt == 16'd0);

    // SCL is released at the end of phase 1; in phase 2 it must be seen high before going on
    // (clock stretching by the target, and the rise time of the bus)
    wire scl_wait = (st != S_IDLE) && (ph == 2'd2);
    wire step = tick && !(scl_wait && !scl_in);

    // value placed on SDA for the current bit (1 = released)
    wire bit_out = (bitcnt == 4'd8) ? (c_write ? 1'b1 : c_nack)   // ACK slot
                                    : (c_write ? sr[7] : 1'b1);   // data slot

    always @(posedge PCLK or negedge PRESETN) begin
        if (!PRESETN) begin
            prescale <= PRESCALE_RESET;
            cmd      <= 5'd0;
            txdata   <= 8'd0;
            rxdata   <= 8'd0;
            busy     <= 1'b0;
            rxnack   <= 1'b0;
            done     <= 1'b0;
            owned    <= 1'b0;
            st       <= S_IDLE;
            ph       <= 2'd0;
            bitcnt   <= 4'd0;
            sr       <= 8'd0;
            divcnt   <= 16'd0;
            smp      <= 1'b1;
            scl_oe   <= 1'b0;
            sda_oe   <= 1'b0;
        end else begin
            // ---------------- register writes
            if (wr && (waddr == A_PRESCALE)) prescale <= PWDATA[15:0];
            if (wr && (waddr == A_TXDATA))   txdata   <= PWDATA[7:0];

            // ---------------- divider (runs only while a command is active)
            if (st == S_IDLE || tick) divcnt <= prescale;
            else                      divcnt <= divcnt - 16'd1;

            // ---------------- command start
            if (cmd_go) begin
                cmd    <= PWDATA[4:0];
                busy   <= 1'b1;
                done   <= 1'b0;
                ph     <= 2'd0;
                bitcnt <= 4'd0;
                sr     <= PWDATA[2] ? txdata : 8'hFF;
                if      (PWDATA[0])             st <= S_START;
                else if (PWDATA[2] | PWDATA[3]) st <= S_BITS;
                else if (PWDATA[1])             st <= S_STOP;
                else begin
                    busy <= 1'b0;
                    done <= 1'b1;
                end
            end else if (st != S_IDLE && step) begin
                case (st)
                // ---------- START / repeated START
                S_START: case (ph)
                    2'd0: begin sda_oe <= 1'b0; ph <= 2'd1; end          // release SDA (SCL as it is)
                    2'd1: begin scl_oe <= 1'b0; ph <= 2'd2; end          // release SCL, wait until high
                    2'd2: begin sda_oe <= 1'b1; ph <= 2'd3; owned <= 1'b1; end  // SDA low while SCL high
                    2'd3: begin
                        scl_oe <= 1'b1;                                   // SCL low
                        ph     <= 2'd0;
                        if (c_write | c_read) st <= S_BITS;
                        else if (c_stop)      st <= S_STOP;
                        else begin st <= S_IDLE; busy <= 1'b0; done <= 1'b1; end
                    end
                endcase
                // ---------- 8 data bits + ACK bit
                S_BITS: case (ph)
                    2'd0: begin scl_oe <= 1'b1; sda_oe <= ~bit_out; ph <= 2'd1; end
                    2'd1: begin scl_oe <= 1'b0; ph <= 2'd2; end           // SCL high (stretch aware)
                    2'd2: begin smp <= sda_in; ph <= 2'd3; end             // sample in the middle
                    2'd3: begin
                        scl_oe <= 1'b1;                                    // SCL low
                        ph     <= 2'd0;
                        if (bitcnt == 4'd8) begin
                            bitcnt <= 4'd0;
                            if (c_write) rxnack <= smp;
                            if (c_read)  rxdata <= sr;
                            // SDA is changed only in phase 0 (a quarter period after SCL fell),
                            // never on the SCL falling edge itself
                            if (c_stop) st <= S_STOP;
                            else begin st <= S_IDLE; busy <= 1'b0; done <= 1'b1; end
                        end else begin
                            bitcnt <= bitcnt + 4'd1;
                            sr     <= {sr[6:0], smp};
                        end
                    end
                endcase
                // ---------- STOP
                S_STOP: case (ph)
                    2'd0: begin scl_oe <= 1'b1; sda_oe <= 1'b1; ph <= 2'd1; end
                    2'd1: begin scl_oe <= 1'b0; ph <= 2'd2; end            // SCL high
                    2'd2: begin sda_oe <= 1'b0; ph <= 2'd3; owned <= 1'b0; end  // SDA high while SCL high
                    2'd3: begin st <= S_IDLE; ph <= 2'd0; busy <= 1'b0; done <= 1'b1; end
                endcase
                default: st <= S_IDLE;
                endcase
            end
        end
    end
endmodule
