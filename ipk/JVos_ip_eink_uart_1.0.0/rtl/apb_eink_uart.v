// apb_eink_uart - APB UART with 16-word transmit and receive FIFOs, 8N1, polled (no interrupt).
//
// Project and documentation: https://github.com/javos65/XO4_Companion_Archive
//
// A small UART for fixed, fast links (made for the 230400 baud link to the Seeed Grove e-ink display):
//   - 8 data bits, no parity, 1 stop bit
//   - baud rate from a fractional (NCO) 16x tick generator: tick16 = pclk * BAUD_INC / 65536, so
//     BAUD_INC = round(baud * 16 * 65536 / pclk). Default 20133 = 230400 baud at 12 MHz (error 0.002 %).
//     The value can be changed at run time (BAUD register).
//   - 16 word TX FIFO and 16 word RX FIFO (distributed RAM). A full RX FIFO drops the NEW character (the
//     oldest characters are kept) and sets the sticky OVERRUN flag.
//   - APB slave without wait states (PREADY = 1), no PSLVERR
//   - internal loopback (TX to RX) for self test
//
// Register map (32 bit registers, word addresses, paddr[4:2]):
//   0x00 ID       RO  0x55415446 ("UATF")
//   0x04 DATA     WO  write: push bits [7:0] into the TX FIFO (ignored when the FIFO is full)
//                 RO  read : pop the RX FIFO. [7:0] data, [8] VALID (0 = FIFO was empty), [9] FRAME error of this byte
//   0x08 STATUS   RO  [0] RX_NOT_EMPTY  [1] RX_FULL  [2] RX_OVERRUN (sticky)  [3] TX_IDLE (FIFO empty and shifter idle)
//                     [4] TX_FULL  [5] TX_BUSY (shifter active)  [6] FRAME_ERR (sticky)
//                     [12:8] RX_COUNT (0..16)   [20:16] TX_COUNT (0..16)
//   0x0C CONTROL  RW  [0] CLR_RX (write 1, self clearing)  [1] CLR_TX (write 1, self clearing)
//                     [2] CLR_FLAGS (write 1: clears OVERRUN and FRAME_ERR)  [3] LOOPBACK (1 = TX connected to RX)
//   0x10 BAUD     RW  [15:0] BAUD_INC
module apb_eink_uart #(
    parameter [15:0] BAUD_INC_RESET = 16'd20133      // 230400 baud at pclk = 12 MHz
)(
    // APB slave
    input  wire        pclk,
    input  wire        presetn,
    input  wire [31:0] paddr,
    input  wire        psel,
    input  wire        penable,
    input  wire        pwrite,
    input  wire [31:0] pwdata,
    output reg  [31:0] prdata,
    output wire        pready,
    output wire        pslverr,

    // serial lines
    output wire        txd_o,
    input  wire        rxd_i
);

    localparam [31:0] ID_CODE = 32'h55415446;

    localparam [2:0] A_ID = 3'd0, A_DATA = 3'd1, A_STATUS = 3'd2, A_CONTROL = 3'd3, A_BAUD = 3'd4;

    assign pready  = 1'b1;
    assign pslverr = 1'b0;

    wire       apb_wr = psel & penable & pwrite;
    wire       apb_rd = psel & penable & ~pwrite;
    wire [2:0] addr   = paddr[4:2];

    // ------------------------------------------------------------------ control and baud registers
    reg [15:0] baud_inc;
    reg        loopback;
    reg        clr_rx, clr_tx, clr_flags;

    always @(posedge pclk or negedge presetn) begin
        if (!presetn) begin
            baud_inc  <= BAUD_INC_RESET;
            loopback  <= 1'b0;
            clr_rx    <= 1'b0;
            clr_tx    <= 1'b0;
            clr_flags <= 1'b0;
        end else begin
            clr_rx    <= 1'b0;
            clr_tx    <= 1'b0;
            clr_flags <= 1'b0;
            if (apb_wr && (addr == A_CONTROL)) begin
                clr_rx    <= pwdata[0];
                clr_tx    <= pwdata[1];
                clr_flags <= pwdata[2];
                loopback  <= pwdata[3];
            end
            if (apb_wr && (addr == A_BAUD)) begin
                baud_inc <= pwdata[15:0];
            end
        end
    end

    // ------------------------------------------------------------------ 16x tick generator (NCO)
    reg  [15:0] nco;
    wire [16:0] nco_sum = {1'b0, nco} + {1'b0, baud_inc};
    reg         tick16;

    always @(posedge pclk or negedge presetn) begin
        if (!presetn) begin
            nco    <= 16'd0;
            tick16 <= 1'b0;
        end else begin
            nco    <= nco_sum[15:0];
            tick16 <= nco_sum[16];
        end
    end

    // ------------------------------------------------------------------ TX FIFO (16 x 8)
    (* syn_ramstyle = "distributed" *) reg [7:0] txmem [0:15];
    reg [3:0] tx_wp, tx_rp;
    reg [4:0] tx_cnt;
    wire      tx_full  = tx_cnt[4];
    wire      tx_empty = (tx_cnt == 5'd0);
    wire      tx_push  = apb_wr && (addr == A_DATA) && !tx_full;
    reg       tx_busy;
    wire      tx_load  = !tx_busy && !tx_empty;

    always @(posedge pclk) begin
        if (tx_push) txmem[tx_wp] <= pwdata[7:0];
    end

    always @(posedge pclk or negedge presetn) begin
        if (!presetn) begin
            tx_wp  <= 4'd0;
            tx_cnt <= 5'd0;
        end else if (clr_tx) begin
            tx_wp  <= 4'd0;
            tx_cnt <= 5'd0;
        end else begin
            if (tx_push) tx_wp <= tx_wp + 4'd1;
            case ({tx_push, tx_load})
                2'b10:   tx_cnt <= tx_cnt + 5'd1;
                2'b01:   tx_cnt <= tx_cnt - 5'd1;
                default: tx_cnt <= tx_cnt;
            endcase
        end
    end

    // ------------------------------------------------------------------ transmitter (start, 8 data bits LSB first, stop)
    reg [3:0] tx_div;
    reg [3:0] tx_bit;
    reg [9:0] tx_sh;
    reg       txd_r;

    always @(posedge pclk or negedge presetn) begin
        if (!presetn) begin
            tx_busy <= 1'b0;
            tx_div  <= 4'd0;
            tx_bit  <= 4'd0;
            tx_sh   <= 10'h3FF;
            tx_rp   <= 4'd0;
            txd_r   <= 1'b1;
        end else begin
            if (clr_tx) tx_rp <= 4'd0;
            if (!tx_busy) begin
                txd_r <= 1'b1;
                if (tx_load && !clr_tx) begin
                    tx_sh   <= {1'b1, txmem[tx_rp], 1'b0};     // stop, data, start
                    tx_rp   <= tx_rp + 4'd1;
                    tx_busy <= 1'b1;
                    tx_bit  <= 4'd0;
                    tx_div  <= 4'd0;
                    txd_r   <= 1'b0;                           // start bit
                end
            end else if (tick16) begin
                if (tx_div == 4'd15) begin
                    tx_div <= 4'd0;
                    if (tx_bit == 4'd9) begin
                        tx_busy <= 1'b0;
                        txd_r   <= 1'b1;
                    end else begin
                        tx_bit <= tx_bit + 4'd1;
                        txd_r  <= tx_sh[tx_bit + 4'd1];
                    end
                end else begin
                    tx_div <= tx_div + 4'd1;
                end
            end
        end
    end

    assign txd_o = txd_r;

    // ------------------------------------------------------------------ receiver (samples in the middle of every bit)
    reg  rxd_s1, rxd_s2, rx_prev;
    wire rx_in = loopback ? txd_r : rxd_s2;

    reg        rx_busy;
    reg  [3:0] rx_div;
    reg  [3:0] rx_bit;
    reg  [7:0] rx_sh;
    reg        rx_push;
    reg  [7:0] rx_word;
    reg        rx_ferr;

    always @(posedge pclk or negedge presetn) begin
        if (!presetn) begin
            rxd_s1  <= 1'b1;
            rxd_s2  <= 1'b1;
            rx_prev <= 1'b1;
            rx_busy <= 1'b0;
            rx_div  <= 4'd0;
            rx_bit  <= 4'd0;
            rx_sh   <= 8'd0;
            rx_push <= 1'b0;
            rx_word <= 8'd0;
            rx_ferr <= 1'b0;
        end else begin
            rxd_s1  <= rxd_i;
            rxd_s2  <= rxd_s1;
            rx_prev <= rx_in;
            rx_push <= 1'b0;
            if (!rx_busy) begin
                if (rx_prev && !rx_in) begin                   // falling edge: start bit
                    rx_busy <= 1'b1;
                    rx_div  <= 4'd0;
                    rx_bit  <= 4'd0;
                end
            end else if (tick16) begin
                rx_div <= rx_div + 4'd1;
                if (rx_bit == 4'd0) begin                      // wait to the middle of the start bit
                    if (rx_div == 4'd7) begin
                        if (rx_in) begin
                            rx_busy <= 1'b0;                   // false start
                        end else begin
                            rx_bit <= 4'd1;
                            rx_div <= 4'd0;
                        end
                    end
                end else if (rx_div == 4'd15) begin            // one bit time later: sample
                    rx_div <= 4'd0;
                    if (rx_bit <= 4'd8) begin
                        rx_sh  <= {rx_in, rx_sh[7:1]};
                        rx_bit <= rx_bit + 4'd1;
                    end else begin                             // stop bit
                        rx_word <= rx_sh;
                        rx_ferr <= !rx_in;
                        rx_push <= 1'b1;
                        rx_busy <= 1'b0;
                    end
                end
            end
        end
    end

    // ------------------------------------------------------------------ RX FIFO (16 x 8 + frame error flag)
    (* syn_ramstyle = "distributed" *) reg [7:0] rxmem [0:15];
    reg [15:0] rxerr;
    reg [3:0]  rx_wp, rx_rp;
    reg [4:0]  rx_cnt;
    wire       rx_full  = rx_cnt[4];
    wire       rx_empty = (rx_cnt == 5'd0);
    wire       rx_wr    = rx_push && !rx_full;
    wire       rx_pop   = apb_rd && (addr == A_DATA) && !rx_empty;
    reg        overrun, ferr_any;

    always @(posedge pclk) begin
        if (rx_wr) rxmem[rx_wp] <= rx_word;
    end

    always @(posedge pclk or negedge presetn) begin
        if (!presetn) begin
            rx_wp    <= 4'd0;
            rx_rp    <= 4'd0;
            rx_cnt   <= 5'd0;
            rxerr    <= 16'd0;
            overrun  <= 1'b0;
            ferr_any <= 1'b0;
        end else begin
            if (clr_flags) begin
                overrun  <= 1'b0;
                ferr_any <= 1'b0;
            end
            if (clr_rx) begin
                rx_wp  <= 4'd0;
                rx_rp  <= 4'd0;
                rx_cnt <= 5'd0;
            end else begin
                if (rx_wr) begin
                    rx_wp        <= rx_wp + 4'd1;
                    rxerr[rx_wp] <= rx_ferr;
                    if (rx_ferr) ferr_any <= 1'b1;
                end else if (rx_push) begin
                    overrun <= 1'b1;                           // FIFO full: the new character is dropped
                end
                if (rx_pop) rx_rp <= rx_rp + 4'd1;
                case ({rx_wr, rx_pop})
                    2'b10:   rx_cnt <= rx_cnt + 5'd1;
                    2'b01:   rx_cnt <= rx_cnt - 5'd1;
                    default: rx_cnt <= rx_cnt;
                endcase
            end
        end
    end

    // ------------------------------------------------------------------ read data
    wire [31:0] status_w = {11'd0, tx_cnt, 3'd0, rx_cnt, 1'b0, ferr_any, tx_busy, tx_full,
                            (tx_empty && !tx_busy), overrun, rx_full, !rx_empty};

    always @(*) begin
        case (addr)
            A_ID:      prdata = ID_CODE;
            A_DATA:    prdata = rx_empty ? 32'd0 : {22'd0, rxerr[rx_rp], 1'b1, rxmem[rx_rp]};   // empty: all zero
            A_STATUS:  prdata = status_w;
            A_CONTROL: prdata = {28'd0, loopback, 3'd0};
            A_BAUD:    prdata = {16'd0, baud_inc};
            default:   prdata = 32'd0;
        endcase
    end

endmodule
