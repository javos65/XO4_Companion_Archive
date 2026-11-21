// IS25LP064A_efb 1.0.0
//
// APB-mapped access block for an ISSI IS25LP064A serial flash on the MachXO4 EFB SPI controller.
// The block sits between the APB interconnect and the lscc_efb IP (APB interface, 32-bit data,
// 10-bit address) and passes every APB signal through 1:1, without registers or clocks:
//
//   APB interconnect --APB_S0--> IS25LP064A_efb --APB_M0--> lscc_efb (APB_S0)
//
// The block gives the flash its own address window and its own BSP driver (IS25LP064A_efb.c/.h):
// JEDEC/unique ID, status/function registers, read, fast read, sector/block read, program,
// 4/32/64 KB erase, chip erase, block protection, software reset and deep power-down.
//
// Address window (1 KB, same as lscc_efb with APB_ADDR_WIDTH = 10): each 8-bit EFB register
// is on its own 32-bit word, CPU offset = EFB register index x 4; the SPI controller registers
// are at 0x150 (SPICR0) to 0x174 (SPIIRQEN).
module IS25LP064A_efb (
    // APB_S0: from the APB interconnect
    input  wire [9:0]  apb_paddr_i,
    input  wire        apb_psel_i,
    input  wire        apb_penable_i,
    input  wire        apb_pwrite_i,
    input  wire [31:0] apb_pwdata_i,
    output wire [31:0] apb_prdata_o,
    output wire        apb_pready_o,
    output wire        apb_pslverr_o,
    // APB_M0: to the lscc_efb APB_S0
    output wire [9:0]  efb_paddr_o,
    output wire        efb_psel_o,
    output wire        efb_penable_o,
    output wire        efb_pwrite_o,
    output wire [31:0] efb_pwdata_o,
    input  wire [31:0] efb_prdata_i,
    input  wire        efb_pready_i,
    input  wire        efb_pslverr_i
);
    assign efb_paddr_o   = apb_paddr_i;
    assign efb_psel_o    = apb_psel_i;
    assign efb_penable_o = apb_penable_i;
    assign efb_pwrite_o  = apb_pwrite_i;
    assign efb_pwdata_o  = apb_pwdata_i;
    assign apb_prdata_o  = efb_prdata_i;
    assign apb_pready_o  = efb_pready_i;
    assign apb_pslverr_o = efb_pslverr_i;
endmodule
