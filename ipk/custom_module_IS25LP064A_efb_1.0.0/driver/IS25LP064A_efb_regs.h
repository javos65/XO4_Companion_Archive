/* IS25LP064A_efb 1.0.0: register and command definitions.
 *
 * EFB SPI controller registers as seen through the IS25LP064A_efb APB window
 * (lscc_efb with APB_DATA_WIDTH = 32: each 8-bit EFB register is on its own
 * 32-bit word, offset = EFB register index x 4).
 *
 * Flash opcodes and register bits: ISSI IS25LP064A data sheet Rev. A8,
 * Table 8.1 (instruction set), Tables 6.1/6.2 (status register),
 * Table 6.4 (block protection), Tables 6.5/6.6 (function register).
 */
#ifndef IS25LP064A_EFB_REGS_H
#define IS25LP064A_EFB_REGS_H

/* ---- EFB SPI controller registers (byte offsets from the block base) ---- */
#define IS25LP064A_EFB_SPICR0    0x150u
#define IS25LP064A_EFB_SPICR1    0x154u
#define IS25LP064A_EFB_SPICR2    0x158u
#define IS25LP064A_EFB_SPIBR     0x15Cu
#define IS25LP064A_EFB_SPICSR    0x160u
#define IS25LP064A_EFB_SPITXDR   0x164u
#define IS25LP064A_EFB_SPISR     0x168u
#define IS25LP064A_EFB_SPIRXDR   0x16Cu
#define IS25LP064A_EFB_SPIIRQ    0x170u
#define IS25LP064A_EFB_SPIIRQEN  0x174u

/* SPICR1 */
#define IS25LP064A_EFB_SPICR1_SPE   (1u << 7)
/* SPICR2 */
#define IS25LP064A_EFB_SPICR2_MSTR  (1u << 7)
#define IS25LP064A_EFB_SPICR2_MCSH  (1u << 6)
#define IS25LP064A_EFB_SPICR2_CPOL  (1u << 2)
#define IS25LP064A_EFB_SPICR2_CPHA  (1u << 1)
#define IS25LP064A_EFB_SPICR2_LSBF  (1u << 0)
/* SPISR */
#define IS25LP064A_EFB_SPISR_TIP    (1u << 7)
#define IS25LP064A_EFB_SPISR_TRDY   (1u << 4)
#define IS25LP064A_EFB_SPISR_RRDY   (1u << 3)
#define IS25LP064A_EFB_SPISR_TOE    (1u << 2)
#define IS25LP064A_EFB_SPISR_ROE    (1u << 1)
#define IS25LP064A_EFB_SPISR_MDF    (1u << 0)
/* SPICSR: CS0 (CSSPIN) is the flash, active low */
#define IS25LP064A_EFB_SPICSR_CS0   0xFEu
#define IS25LP064A_EFB_SPICSR_NONE  0xFFu

/* SPIBR divider: SCK = PCLK / (divider + 1). SPICR2[MCSH] may only be used with a
   divider above 4 (FPGA-IPUG-02287 Table 5.15), so the fastest SCK is PCLK / 6. */
#define IS25LP064A_EFB_SPIBR_MIN      5u
#define IS25LP064A_EFB_SPIBR_DEFAULT  7u     /* 12 MHz PCLK -> 1.5 MHz SCK */

/* ---- IS25LP064A instruction set (SPI mode) ---- */
#define IS25LP064A_CMD_NORD      0x03u   /* normal read, max 50 MHz */
#define IS25LP064A_CMD_FRD       0x0Bu   /* fast read, 8 dummy cycles by default */
#define IS25LP064A_CMD_PP        0x02u   /* page program, 1-256 bytes */
#define IS25LP064A_CMD_SER       0x20u   /* sector erase, 4 KB */
#define IS25LP064A_CMD_BER32     0x52u   /* block erase, 32 KB */
#define IS25LP064A_CMD_BER64     0xD8u   /* block erase, 64 KB */
#define IS25LP064A_CMD_CER       0xC7u   /* chip erase (ignored unless BP3-0 = 0) */
#define IS25LP064A_CMD_WREN      0x06u
#define IS25LP064A_CMD_WRDI      0x04u
#define IS25LP064A_CMD_RDSR      0x05u
#define IS25LP064A_CMD_WRSR      0x01u
#define IS25LP064A_CMD_RDFR      0x48u
#define IS25LP064A_CMD_WRFR      0x42u
#define IS25LP064A_CMD_RDJDID    0x9Fu
#define IS25LP064A_CMD_RDMDID    0x90u
#define IS25LP064A_CMD_RDUID     0x4Bu
#define IS25LP064A_CMD_RDSFDP    0x5Au
#define IS25LP064A_CMD_DP        0xB9u   /* deep power-down */
#define IS25LP064A_CMD_RDPD      0xABu   /* release from deep power-down / read ID */
#define IS25LP064A_CMD_RSTEN     0x66u
#define IS25LP064A_CMD_RST       0x99u

/* ---- Status register (RDSR / WRSR) ---- */
#define IS25LP064A_SR_WIP        (1u << 0)    /* write in progress (volatile) */
#define IS25LP064A_SR_WEL        (1u << 1)    /* write enable latch (volatile) */
#define IS25LP064A_SR_BP_SHIFT   2u
#define IS25LP064A_SR_BP_MASK    (0x0Fu << 2) /* BP3..BP0 (non-volatile) */
#define IS25LP064A_SR_QE         (1u << 6)    /* quad enable (non-volatile) */
#define IS25LP064A_SR_SRWD       (1u << 7)    /* status register write disable */

/* ---- Function register (RDFR) ---- */
#define IS25LP064A_FR_TBS        (1u << 1)    /* 0: BP area at the top (OTP) */
#define IS25LP064A_FR_PSUS       (1u << 2)
#define IS25LP064A_FR_ESUS       (1u << 3)

#endif /* IS25LP064A_EFB_REGS_H */
