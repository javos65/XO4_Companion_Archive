
/*
 *
 * SSD1306 Include file  (oled_4wire IP, BSP driver)
 *
 * J.Vos 2025/2026
 *
 *  Define Control set
 *  Define Memory mapped registers to SPI
 *
 *  APB memory mapped, see sys_platform.h for the base address OLED_SPI0_INST_BASE_ADDR
 *	Offset	Register
 *	0x00	ID       RO  0x4F4C4401
 *	0x04	STATUS   RO  [0] BUSY  [1] FIFO EMPTY  [2] FIFO FULL  [3] IDLE (empty and not busy)
 *	                     [4] OVERFLOW (sticky, write 1 to clear)
 *	0x08	CONTROL  RW  [7:0] CLKDIV (SCLK = PCLK / (2*(CLKDIV+1)))
 *	                     [8] RES_N  [9] VBATC_N (active low)  [10] VDDC_N (active low)
 *	0x0C	DATA     WO  [8] D/C~ (1 = data)  [7:0] byte  -> 16 deep FIFO
 *
 *  Pmod OLED power sequence (Digilent): VDD on, RES pulse, init commands,
 *  VBAT on, 100 ms, display on  -> see oled_reset_ssd1306(), oled_vbat_ssd1306()
 *
 */
#define SSD1306_DRIVER // UNMARK THIS TO USE THIS DRIVER INCL 1BPP FONT !!!
#ifdef SSD1306_DRIVER

#include <stdio.h> // for printf funtion
#include "utils.h" // for delay function

#ifndef SSD1306_H_
#define SSD1306_H_

#define ID_REGISTER1306 0x00			// offset ID 		read only
#define STATUS_REGISTER1306 0x04		// offset Status 	read only
#define CONTROL_REGISTER1306 0x08		// offset Control 	read / write
#define DATA_REGISTER1306 0x0c			// offset Data 		read / write
#define REGMAX1306 0x0c

#define STATUS_SPI_BUSY    0	// shifter busy
#define STATUS_FIFO_EMPTY  1
#define STATUS_FIFO_FULL   2
#define STATUS_SPI_IDLE    3	// FIFO empty and shifter idle
#define STATUS_SPI_ERROR   4	// overflow, write 1 to clear

#define CONTROL_CLKDIV_MASK 0x000000FF
#define CONTROL_RES_N       (1u<<8)
#define CONTROL_VBATC_N     (1u<<9)	// 0 = display supply ON
#define CONTROL_VDDC_N      (1u<<10)	// 0 = logic supply ON
#define CONTROL_RESET_VALUE 0x00000601	// VDD off, VBAT off, RES asserted, CLKDIV 1

#define SSD1306_H 32 // height
#define SSD1306_W 128 // width
#define SSD1306_D 512 // full screen data = 128*32 / 8 = 512 bytes
#define SSD1306_BLACK 0   //< Draw 'off' pixels
#define SSD1306_WHITE 1   //< Draw 'on' pixels
#define SSD1306_INVERSE 2 //< Invert pixels

#define SSD1306_MEMORYMODE 0x20          //< See datasheet
#define SSD1306_COLUMNADDR 0x21          //< See datasheet
#define SSD1306_PAGEADDR 0x22            //< See datasheet
#define SSD1306_SETCONTRAST 0x81         //< See datasheet
#define SSD1306_CHARGEPUMP 0x8D          //< See datasheet
#define SSD1306_SEGREMAP 0xA0            //< See datasheet
#define SSD1306_DISPLAYALLON_RESUME 0xA4 //< See datasheet
#define SSD1306_DISPLAYALLON 0xA5        //< Not currently used
#define SSD1306_NORMALDISPLAY 0xA6       //< See datasheet
#define SSD1306_INVERTDISPLAY 0xA7       //< See datasheet
#define SSD1306_SETMULTIPLEX 0xA8        //< See datasheet
#define SSD1306_DISPLAYOFF 0xAE          //< See datasheet
#define SSD1306_DISPLAYON 0xAF           //< See datasheet
#define SSD1306_COMSCANINC 0xC0          //< Not currently used
#define SSD1306_COMSCANDEC 0xC8          //< See datasheet
#define SSD1306_PAGESTART 0xB0          //< See datasheet
#define SSD1306_SETDISPLAYCLKDIV 0xD5  //< See datasheet
#define SSD1306_SETDISPLAYOFFSET 0xD3    //< See datasheet

#define SSD1306_SETPRECHARGE 0xD9        //< See datasheet
#define SSD1306_SETCOMPINS 0xDA          //< See datasheet
#define SSD1306_SETVCOMDETECT 0xDB       //< See datasheet

#define SSD1306_SETLOWCOLUMN 0x00  //< Not currently used
#define SSD1306_SETHIGHCOLUMN 0x10 //< Not currently used
#define SSD1306_SETSTARTLINE 0x40  //< See datasheet

#define SSD1306_EXTERNALVCC 0x01  //< External display voltage source
#define SSD1306_SWITCHCAPVCC 0x02 //< Gen. display voltage from 3.3V

#define SSD1306_RIGHT_HORIZONTAL_SCROLL 0x26              //< Init rt scroll
#define SSD1306_LEFT_HORIZONTAL_SCROLL 0x27               //< Init left scroll
#define SSD1306_VERTICAL_AND_RIGHT_HORIZONTAL_SCROLL 0x29 //< Init diag scroll
#define SSD1306_VERTICAL_AND_LEFT_HORIZONTAL_SCROLL 0x2A  //< Init diag scroll
#define SSD1306_DEACTIVATE_SCROLL 0x2E                    //< Stop scroll
#define SSD1306_ACTIVATE_SCROLL 0x2F                      //< Start scroll
#define SSD1306_SET_VERTICAL_SCROLL_AREA 0xA3             //< Set scroll range

#define OLED_GRAPH_SIZE 6
#define OLED_ON_SIZE 2
#define OLED_OFF_SIZE 1
#define OLED_NORMAL_SIZE 1
#define OLED_INVERSE_SIZE 1
#define OLED_ORIENT_SIZE 2

// SPI memory mapped routines
uint32_t readSPIMemory_ssd1306(uint8_t offset);
void writeSPIMemory_ssd1306(uint8_t offset,uint32_t data);

// SPI ssd1351 routines
void base_ssd1306(uint32_t address);
uint32_t getbase_ssd1306();
void commandlist_ssd1306(const uint8_t* list,uint8_t size);
uint8_t command_ssd1306(uint8_t command);
uint8_t data_ssd1306(uint8_t data);
void oled_clear_ssd1306(uint8_t pattern);
// display orientation, parameter of oled_start_ssd1306() and oled_orientation_ssd1306()
#define OLED_ORIENT_NORMAL       0	// segment re-map A1 + COM scan C8: the orientation used so far (upright on this board)
#define OLED_ORIENT_UPSIDE_DOWN  1	// segment re-map A0 + COM scan C0 (controller default): picture rotated by 180 degrees
// logo orientation, 4th parameter of oled_lattice_ssd1306()
#define OLED_LOGO_NORMAL       0	// bitmap as stored in lattice_logo[]
#define OLED_LOGO_UPSIDE_DOWN  1	// the same bitmap rotated by 180 degrees
void oled_lattice_ssd1306(uint8_t column,uint8_t page,uint8_t type,uint8_t orientation); // custom routine
void oled_setblock_ssd1306(uint8_t column,uint8_t page,uint8_t width, uint8_t height);
void oled_clearblock_ssd1306(uint8_t column,uint8_t page,uint8_t width, uint8_t height,uint8_t pattern);

// power / reset sequence of the Pmod OLED (new with the oled_4wire IP)
void oled_reset_ssd1306(void);               // VDD on, pulse RES
void oled_vbat_ssd1306(uint8_t on);          // display supply on / off (100 ms wait after on)
void oled_clkdiv_ssd1306(uint8_t clkdiv);    // SCLK = PCLK / (2*(clkdiv+1))
uint8_t oled_flush_ssd1306(void);            // wait until everything is sent, 0 = timeout
void oled_orientation_ssd1306(uint8_t orientation);	// picture orientation, OLED_ORIENT_NORMAL or OLED_ORIENT_UPSIDE_DOWN
void oled_start_ssd1306(uint8_t orientation);	// full start-up: reset, init list, orientation, VBAT, display on
void oled_stop_ssd1306(void);               // display off, VBAT off, VDD off

// Graphics Array defs
#define LATTICE_HEIGHT 24 // 24 pixels = 3 pages
#define LATTICE_WIDTH 120 // 120 pixels = 120 columns
#define LATTICE_SIZE 360  // 120x24 / 8 = 360

// define  command arrays
extern  uint8_t oled_graph[6];
extern const uint8_t oled_init[23];
extern const uint8_t oled_on[2];
extern const uint8_t oled_off[1];
extern const uint8_t oled_normal[1];
extern const uint8_t oled_inverse[1];
extern const uint8_t oled_orient_normal[2];
extern const uint8_t oled_orient_upsidedown[2];
extern const uint8_t lattice_logo[ LATTICE_SIZE];


#endif /* SSD1306_DRIVER */

#endif				/* SSD1351_H_ */
