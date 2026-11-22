
/*
 *
 * SSD1306 setup and Routines (oled_4wire IP)
 *
 * J.Vos 2025
 *
 * Setup display in Serial 4-wire interface (CS,DC,SDIN,SCLK)
 * Drive SSD1306
 * Page-based display, pixel-byte is vertical (8 pixels per address)
 *
 */
#include "ssd1306.h"

#ifdef SSD1306_DRIVER


const uint8_t oled_init[23] = {	// orientation and display on are sent separately (oled_orientation_ssd1306, oled_on)
		SSD1306_DISPLAYOFF,
		SSD1306_SETDISPLAYCLKDIV,0x80,
		SSD1306_SETMULTIPLEX,31, 				// HEIGHT -1
		SSD1306_SETDISPLAYOFFSET,0x00,
		SSD1306_SETSTARTLINE | 0x00, 			// line #0
		SSD1306_CHARGEPUMP,0x14, 				// charge pump 0x10 - intern or 0x14 - extern
		SSD1306_MEMORYMODE, 0x00,
		SSD1306_SETCOMPINS,0x02,  				// 128x32 specific
		SSD1306_SETCONTRAST,0x4F,  				// 128x32 specific
		SSD1306_SETPRECHARGE,0xF1, 				// pre-charge 0x22 - intern or 0xF1 - extern
		SSD1306_SETVCOMDETECT,0x40,
		SSD1306_DISPLAYALLON_RESUME,
		SSD1306_NORMALDISPLAY,
		SSD1306_DEACTIVATE_SCROLL}; // END OF COMMAND LIST (no DISPLAYON: VBAT is switched on first)

uint8_t oled_graph[OLED_GRAPH_SIZE]={SSD1306_COLUMNADDR,0x00,0x7F, SSD1306_PAGEADDR,0x00,0x7d};
const uint8_t oled_on[OLED_ON_SIZE]={SSD1306_DISPLAYALLON_RESUME,SSD1306_DISPLAYON};
const uint8_t oled_off[OLED_OFF_SIZE]={SSD1306_DISPLAYOFF};
const uint8_t oled_normal[OLED_NORMAL_SIZE]={SSD1306_NORMALDISPLAY};
const uint8_t oled_inverse[OLED_INVERSE_SIZE]={SSD1306_INVERTDISPLAY};
// picture orientation on the glass = segment re-map (column mirror) + COM scan direction (row flip)
const uint8_t oled_orient_normal[OLED_ORIENT_SIZE]     ={SSD1306_SEGREMAP | 0x1, SSD1306_COMSCANDEC};	// A1, C8: the orientation used so far (upright on this board)
const uint8_t oled_orient_upsidedown[OLED_ORIENT_SIZE] ={SSD1306_SEGREMAP | 0x0, SSD1306_COMSCANINC};	// A0, C0: controller default, rotated by 180 degrees compared to normal


uint32_t SPI_BASE; 		// Base Memory Mapped address
static uint32_t oled_ctrl = CONTROL_RESET_VALUE;	// shadow of the CONTROL register

/*
 * Optimised (v3.0.4): all public function names and signatures are unchanged.
 *  - FIFO-full wait is a tight status poll instead of delay(1) (1 ms) per byte.
 *    A byte leaves the IP FIFO in ~2 us, the old wait cost 1 ms every time the
 *    8 word FIFO was full (a 512 byte screen clear took about half a second).
 *  - Registers are accessed through volatile pointers.
 *  - The extra CONTROL register write after every byte is dropped (no function
 *    in the IP, it only cost one more APB write per byte).
 *  - Bulk routines stop at the first failed write instead of printing a
 *    timeout message for every remaining byte.
 */
#define SPI_FULL_MASK	(1u << STATUS_FIFO_FULL)	// STATUS[2] = FIFO FULL
#define SPI_DATA_FLAG	0x100u						// DATA[8] = D/C~ (1 = data)
#define SPI_WAIT_LOOPS	20000u						// ~8 ms worst case, FIFO frees a slot in ~2 us

static inline volatile uint32_t* spi_reg(uint8_t offset)
{
	return (volatile uint32_t*) (SPI_BASE + offset);
}

// set SPI base address - check sys_platform.h
void base_ssd1306(uint32_t address)
{
	SPI_BASE = address;
	oled_ctrl = *spi_reg(CONTROL_REGISTER1306);			// pick up the current CONTROL value
}

uint32_t getbase_ssd1306()
{
	return(SPI_BASE);
}


uint8_t wait_spi_ssd1306()
 {
	volatile uint32_t* status = spi_reg(STATUS_REGISTER1306);
	for(uint32_t t=SPI_WAIT_LOOPS;t;--t){
		if ( !(*status & SPI_FULL_MASK) ) return(1); // check not FULL : bit[16]
	}
	printf("* spi fifo full : timeout\n");
	return(0);
 }

// push one 9 bit word (D/C~ and byte) into the SPI fifo
static inline uint8_t spi_put(uint32_t word)
{
	if ( !wait_spi_ssd1306() ) return(0); // failure
	*spi_reg(DATA_REGISTER1306) = word;
	return(1);
}

uint8_t command_ssd1306(uint8_t command)
{
	return spi_put(command);
}


uint8_t data_ssd1306(uint8_t data)
{
	return spi_put(SPI_DATA_FLAG | data);
}


void writeSPIMemory_ssd1306(uint8_t offset,uint32_t data)
{
	*spi_reg(offset) = data;
}


uint32_t readSPIMemory_ssd1306(uint8_t offset)
{
	return *spi_reg(offset);
}


void  commandlist_ssd1306(const uint8_t* list,uint8_t size) {
  for (uint8_t t=0;t<size;++t)
	  {command_ssd1306(list[t]);}
 }


// send 'count' data bytes with the same pattern, stop at first failure
static void spi_fill(uint16_t count,uint8_t pattern)
{
	const uint32_t word = SPI_DATA_FLAG | pattern;
	while (count--) { if ( !spi_put(word) ) return; }
}

// set the write window: ncol columns from col, npage pages from page
static void spi_window(uint8_t col,uint8_t ncol,uint8_t page,uint8_t npage)
{
	oled_graph[1]=col;oled_graph[2]=col+ncol-1;
	oled_graph[4]=page;oled_graph[5]=page+npage-1;			// put data into indexed setup array
	commandlist_ssd1306(oled_graph,OLED_GRAPH_SIZE); 		// send window
}


// Fill the oled with a pattern : 128x32 bits ,in 512 bytes/
void oled_clear_ssd1306(uint8_t pattern)
{
	spi_window(0,SSD1306_W,0,SSD1306_H/8); 								// full screen: columns 0-127, pages 0-3
	command_ssd1306(SSD1306_SETSTARTLINE |0x00);	 					// set start to 0x00
	spi_fill(SSD1306_D,pattern);  										// send data pattern
}


// reverse the bit order of a byte (bit 0 <-> bit 7). One page byte holds 8 vertical pixels,
// so turning the picture upside down also means mirroring the bits inside every byte.
static inline uint8_t bitrev8(uint8_t b)
{
	b = (uint8_t)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
	b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
	b = (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
	return b;
}

// Send the lattice logo (120 x 24 pixels = 120 columns x 3 pages) to the window at column/page.
//   type        : 1 = normal pixels, any other value = inverted pixels
//   orientation : OLED_LOGO_NORMAL      = bitmap as stored in lattice_logo[]
//                 OLED_LOGO_UPSIDE_DOWN = same bitmap rotated by 180 degrees
// Both orientations fill the same window, so the logo starts at the same column and page.
// Nothing is drawn if the logo would not fit on the display at that position.
void oled_lattice_ssd1306(uint8_t column,uint8_t page,uint8_t type,uint8_t orientation)
{
	const uint8_t invert = (type == 1) ? 0x00 : 0xFF;

	if ( ((uint16_t)column + LATTICE_WIDTH > SSD1306_W) ||
	     ((uint16_t)page + LATTICE_HEIGHT/8 > SSD1306_H/8) ) return;

	spi_window(column,LATTICE_WIDTH,page,LATTICE_HEIGHT/8);				// window exactly the size of the logo
	for(uint16_t i=0;i<LATTICE_SIZE;++i) {
		uint8_t b;
		if (orientation == OLED_LOGO_UPSIDE_DOWN)
			b = bitrev8( lattice_logo[LATTICE_SIZE-1-i] );				// last byte first, bits mirrored = rotation by 180 degrees
		else
			b = lattice_logo[i];
		if ( !data_ssd1306( b ^ invert ) ) return;
	}
}

void oled_setblock_ssd1306(uint8_t column,uint8_t page,uint8_t width, uint8_t height)
{
	spi_window(column,width,page,height);					// set block
	command_ssd1306(SSD1306_SETSTARTLINE |0x00);			// set startline to zero
}


void oled_clearblock_ssd1306(uint8_t column,uint8_t page,uint8_t width, uint8_t height,uint8_t pattern)
{
	oled_setblock_ssd1306(column,page,width,height);		// set block, startline to zero
	spi_fill((uint16_t)width*height,pattern);  				// send data pattern
}

// ---------------------------------------------------------------------------
// Power / reset sequence of the Pmod OLED (oled_4wire IP)
// ---------------------------------------------------------------------------
static void oled_ctrl_write(uint32_t value)
{
	oled_ctrl = value;
	*spi_reg(CONTROL_REGISTER1306) = value;
}

// SCLK = PCLK / (2*(clkdiv+1))
void oled_clkdiv_ssd1306(uint8_t clkdiv)
{
	oled_ctrl_write( (oled_ctrl & ~CONTROL_CLKDIV_MASK) | clkdiv );
}

// wait until the FIFO is empty and the last byte is shifted out
uint8_t oled_flush_ssd1306(void)
{
	volatile uint32_t* status = spi_reg(STATUS_REGISTER1306);
	for(uint32_t t=SPI_WAIT_LOOPS*16;t;--t){
		if ( *status & (1u << STATUS_SPI_IDLE) ) return(1);
	}
	printf("* spi flush : timeout\n");
	return(0);
}

// logic supply on (VDDC low), then pulse the display reset
void oled_reset_ssd1306(void)
{
	oled_ctrl_write( CONTROL_RESET_VALUE & ~CONTROL_VDDC_N );				// VDD on, RES asserted, VBAT off
	delay(5);																// supply settles
	oled_ctrl_write( (CONTROL_RESET_VALUE & ~CONTROL_VDDC_N) | CONTROL_RES_N );	// release RES
	delay(5);
}

// display supply (VBAT) on / off, datasheet asks for 100 ms before DISPLAYON
void oled_vbat_ssd1306(uint8_t on)
{
	if (on) { oled_ctrl_write( oled_ctrl & ~CONTROL_VBATC_N ); delay(100); }
	else    { oled_ctrl_write( oled_ctrl |  CONTROL_VBATC_N ); delay(100); }
}

// Picture orientation: OLED_ORIENT_NORMAL = A1 + C8, the orientation this display has always had (upright on the board),
// OLED_ORIENT_UPSIDE_DOWN = A0 + C0 (controller default), the picture rotated by 180 degrees.
// The COM scan (C0/C8) acts on the picture immediately, the segment re-map (A0/A1) only acts on data
// written afterwards: call this before drawing, or redraw the screen after changing it.
void oled_orientation_ssd1306(uint8_t orientation)
{
	if (orientation == OLED_ORIENT_UPSIDE_DOWN) commandlist_ssd1306(oled_orient_upsidedown,OLED_ORIENT_SIZE);
	else                                        commandlist_ssd1306(oled_orient_normal,OLED_ORIENT_SIZE);
}

// complete start-up sequence, orientation = OLED_ORIENT_NORMAL or OLED_ORIENT_UPSIDE_DOWN
void oled_start_ssd1306(uint8_t orientation)
{
	oled_reset_ssd1306();
	commandlist_ssd1306(oled_init,sizeof(oled_init));		// init list, display still off
	oled_orientation_ssd1306(orientation);				// segment re-map and COM scan direction
	oled_flush_ssd1306();
	oled_vbat_ssd1306(1);									// charge pump supply on, wait 100 ms
	commandlist_ssd1306(oled_on,OLED_ON_SIZE);				// display on
	oled_flush_ssd1306();
}

// switch the display off in the order the datasheet asks for
void oled_stop_ssd1306(void)
{
	commandlist_ssd1306(oled_off,OLED_OFF_SIZE);
	oled_flush_ssd1306();
	oled_vbat_ssd1306(0);
	oled_ctrl_write( CONTROL_RESET_VALUE );					// VDD off, RES asserted
}

// Data arrays


// remark: logo is upside down due to physical oled mounting
const uint8_t lattice_logo[ LATTICE_SIZE]  = {
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x54, 0xa8, 0x54, 0xa8, 0x54, 0x00, 0x80, 0xfc,
		  0xfc, 0xfc, 0xfc, 0xfc, 0x04, 0x80, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0x0c, 0x00, 0x00, 0xc0,
		  0xfc, 0xfc, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0xc0, 0xe0, 0xf0, 0x78, 0x1c, 0xfc, 0xfc, 0xf0, 0x00, 0x00, 0x00, 0x0c, 0x0c, 0x0c,
		  0x0c, 0xec, 0xfc, 0xfc, 0x1c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x0c, 0x0c,
		  0xcc, 0xfc, 0xfc, 0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x00, 0xc0, 0xfc, 0xfc, 0x7c, 0x00,
		  0xc0, 0xe0, 0xf0, 0x78, 0x38, 0x1c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x00,
		  0xe0, 0xfc, 0xfc, 0xbc, 0x8c, 0x8c, 0x8c, 0x8c, 0x8c, 0x8c, 0x8c, 0x0c, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x40, 0x2a, 0x54, 0x2a, 0x54, 0x2a, 0x04, 0x60, 0x7e, 0x7e, 0x7e,
		  0x7e, 0x7e, 0x0e, 0x00, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x1e, 0x00, 0x00, 0x00, 0x7c, 0x7f,
		  0x7f, 0x63, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x60, 0x78, 0x7c, 0x3e,
		  0x1f, 0x07, 0x07, 0x06, 0x06, 0x06, 0x07, 0x3f, 0x7f, 0x7f, 0x60, 0x00, 0x00, 0x00, 0x60,
		  0x7e, 0x7f, 0x3f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c,
		  0x7f, 0x7f, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x7f, 0x7f, 0x07, 0x00, 0x00,
		  0x3f, 0x7f, 0x7f, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x7e,
		  0x7f, 0x7f, 0x63, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x61, 0x01, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x14, 0x0a, 0x15, 0x0a, 0x15, 0x02, 0x00, 0x14, 0x0a, 0x15, 0x0a, 0x15,
		  0x0a, 0x00, 0x14, 0x0a, 0x15, 0x0a, 0x15, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

};

#endif
