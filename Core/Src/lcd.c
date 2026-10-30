/**
  ******************************************************************************
  * @file    lcd.c
  * @brief   This file provides code for the configuration
  *          of the lcd
  ******************************************************************************
  * ST7701S supports two kinds of RGB interface, DE mode (mode 1) and HV mode
  * (mode 2), and 16bit/18bit and 24 bit data format. When DE mode is selected
  * and the VSYNC, HSYNC, DOTCLK, DE, D23:0 pins can be used; when HV mode is
  * selected and the VSYNC, HSYNC, DOTCLK, D23:0 pins can be used. When using
  * RGB interface, only serial interface can be selected.
*/


/* Includes ------------------------------------------------------------------*/
#include "spi.h"
#include "main.h"
#include "lcd.h"
#include "lt7680.h"


//************************************************************************************************************************************************************

// Bit bang SPI to LCD (9bit)
void LCD_SPI_Write(uint16_t data, uint8_t bits) {
	for (int i = bits - 1; i >= 0; i--) {  // Loop through each bit (MSB first)
		// Set SDA based on the current bit
		if (data & (1 << i)) {
			HAL_GPIO_WritePin(LCD_SDI_Port, LCD_SDI_Pin, GPIO_PIN_SET);  // SDA = 1
		}
		else {
			HAL_GPIO_WritePin(LCD_SDI_Port, LCD_SDI_Pin, GPIO_PIN_RESET); // SDA = 0
		}

		// Toggle the clock signal
		HAL_GPIO_WritePin(LCD_SCK_Port, LCD_SCK_Pin, GPIO_PIN_SET); // CLK high
		DelayMicroseconds(LCD_BIT_US);                                 // Hold high
		HAL_GPIO_WritePin(LCD_SCK_Port, LCD_SCK_Pin, GPIO_PIN_RESET); // CLK low
		DelayMicroseconds(LCD_BIT_US);                                 // Hold low
	}
}


void LCDWriteRegister(uint8_t reg) {
	HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_RESET); // Pull CS low
	DelayMicroseconds(LCD_CS_US);

	// Use LCD_SPI_Write for 9-bit SPI communication
	LCD_SPI_Write((0 << 8) | reg, 9); // D/CX = 0, reg[7:0]

	HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_SET);   // Pull CS high
	DelayMicroseconds(LCD_CS_US);
}



void LCDWriteData(uint8_t data) {
	HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_RESET); // Pull CS low
	DelayMicroseconds(LCD_CS_US);

	// Use LCD_SPI_Write for 9-bit SPI communication
	LCD_SPI_Write((1 << 8) | data, 9); // D/CX = 1, data[7:0]

	HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_SET);   // Pull CS high
	DelayMicroseconds(LCD_CS_US);
}



// Read 'bits' bits from the panel after a 9-bit command, with the SDA pin released and given the pull 'pull'.
// The ST7701S drives SDA after the falling clock edge; the bit is sampled while the clock is high.
static uint32_t LCD_ReadBits(uint8_t cmd, uint8_t bits, uint32_t pull) {
	GPIO_InitTypeDef g = { .Pin = LCD_SDI_Pin, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_HIGH };
	uint32_t v = 0;

	HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_RESET); // Pull CS low
	DelayMicroseconds(LCD_CS_US);
	LCD_SPI_Write((0 << 8) | cmd, 9);                           // D/CX = 0, command
	g.Mode = GPIO_MODE_INPUT; g.Pull = pull;
	HAL_GPIO_Init(LCD_SDI_Port, &g);                            // let the panel drive SDA
	for (uint8_t i = 0; i < bits; i++) {
		HAL_GPIO_WritePin(LCD_SCK_Port, LCD_SCK_Pin, GPIO_PIN_SET);
		DelayMicroseconds(LCD_BIT_US);
		v = (v << 1) | (HAL_GPIO_ReadPin(LCD_SDI_Port, LCD_SDI_Pin) == GPIO_PIN_SET);
		HAL_GPIO_WritePin(LCD_SCK_Port, LCD_SCK_Pin, GPIO_PIN_RESET);
		DelayMicroseconds(LCD_BIT_US);
	}
	HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_SET);   // Pull CS high
	g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(LCD_SDI_Port, &g);                            // SDA back to an output
	DelayMicroseconds(LCD_CS_US);
	return v;
}

// Panel identity for diagnostics (docs/BLUE-PILL-CHIPS.md): RDID1-3 (DAh, DBh, DCh), one byte each, read once with the
// pin pulled up and once pulled down. If the two disagree, nothing drove the line; all ones or all zeros means the line is
// held by something other than the panel. Either way the panel's SDA isn't read back on this board (or the panel doesn't
// answer), and the result is 0xFFFFFFFF. On the bench board the line reads high with either pull.
volatile uint32_t lcd_id = 0;
uint32_t LCD_ReadID(void) {
	uint32_t up = 0, down = 0;
	for (uint8_t c = 0xDA; c <= 0xDC; c++) {
		up = (up << 8) | LCD_ReadBits(c, 8, GPIO_PULLUP);
		down = (down << 8) | LCD_ReadBits(c, 8, GPIO_PULLDOWN);
	}
	lcd_id = (up == down && up != 0x00FFFFFFu && up != 0) ? up : 0xFFFFFFFFu;
	return lcd_id;
}


// delay used by the bit bang SPI: counts CPU cycles (DWT CYCCNT), which wraps cleanly; the old SysTick version could end early
void DelayMicroseconds(uint16_t us) {
	if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
		DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	}
	uint32_t start = DWT->CYCCNT;
	uint32_t ticks = (SystemCoreClock / 1000000u) * us;
	while ((DWT->CYCCNT - start) < ticks) { }
}


//**************************************************************************************************
// Commands to LCD


void LCD_Clear(uint16_t color) {

	LCDWriteRegister(0x2A); // Column Address Set
	LCDWriteData(0x00);     // Start column high byte
	LCDWriteData(0x00);     // Start column low byte
	LCDWriteData((LCD_XSIZE_TFT - 1) >> 8);  // End column high byte
	LCDWriteData((LCD_XSIZE_TFT - 1) & 0xFF); // End column low byte

	LCDWriteRegister(0x2B); // Row Address Set
	LCDWriteData(0x00);     // Start row high byte
	LCDWriteData(0x00);     // Start row low byte
	LCDWriteData((LCD_YSIZE_TFT - 1) >> 8);  // End row high byte
	LCDWriteData((LCD_YSIZE_TFT - 1) & 0xFF); // End row low byte

	// Write the color to the display RAM
	LCDWriteRegister(0x2C); // Memory Write command

	// Send color data for the entire screen
	for (uint32_t i = 0; i < (LCD_XSIZE_TFT * LCD_YSIZE_TFT); i++) {
		LCDWriteData(color >> 8); // Send high byte of color
		LCDWriteData(color & 0xFF); // Send low byte of color
	}
}


void LCD_Hor_Ver_Timing() {
	// Horizontal Timing
	LCDWriteRegister(0x14); LCDWriteData(0x27); // Horizontal Display Width
	LCDWriteRegister(0x16); LCDWriteData(0x09); // Horizontal Back Porch
	LCDWriteRegister(0x18); LCDWriteData(0x0C); // Horizontal Front Porch
	LCDWriteRegister(0x19); LCDWriteData(0x00); // HSYNC Pulse Width

	// Vertical Timing
	LCDWriteRegister(0x1A); LCDWriteData(0xFF); // Vertical Display Height (Low byte)
	LCDWriteRegister(0x1B); LCDWriteData(0x03); // Vertical Display Height (High bits)
	LCDWriteRegister(0x1C); LCDWriteData(0x05); // Vertical Back Porch (Low byte)
	LCDWriteRegister(0x1D); LCDWriteData(0x00); // Vertical Back Porch (High bits)
	LCDWriteRegister(0x1E); LCDWriteData(0x18); // Vertical Front Porch
	LCDWriteRegister(0x1F); LCDWriteData(0x01); // VSYNC Pulse Width
}


// First half of the panel set-up, up to and including Sleep Out (11h)
void AdaFruit_Init_Start() {

	//ST7701S+AUO4.58
	//https://cdn-shop.adafruit.com/product-files/5805/AUO4.58-ST7701S-3W-RGB18BIT_initcode.txt

	LCDWriteRegister(0xFF);
	LCDWriteData(0x77);
	LCDWriteData(0x01);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x13);

	LCDWriteRegister(0xEF);
	LCDWriteData(0x08);

	LCDWriteRegister(0xFF);
	LCDWriteData(0x77);
	LCDWriteData(0x01);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x10);

	LCDWriteRegister(0xC0);
	LCDWriteData(0x77);				// 0x77 - GAMALOT made this 0x79 to help stability of first line
	LCDWriteData(0x00);

	//uint32_t hexVarVBPD = LCD_VBPD;		// Match the Porch settings from the LT780A-R to the ST7701S
	//uint32_t hexVarVFPD = LCD_VFPD;
	//LCDWriteRegister(0xC1);
	//LCDWriteData((uint8_t)hexVarVBPD);
	//LCDWriteData((uint8_t)hexVarVFPD);
	LCDWriteData(0x09);				// VBPD	09
	LCDWriteData(0x08);				// VFPD 08

	LCDWriteRegister(0xC2);//inv     
	LCDWriteData(0x01);
	LCDWriteData(0x02);

	LCDWriteRegister(0xC3); //????    
	LCDWriteData(0x02); //82 HVmode    02 DEmode 

	LCDWriteRegister(0xCC);
	LCDWriteData(0x10);

	LCDWriteRegister(0xB0);
	LCDWriteData(0x40);
	LCDWriteData(0x14);
	LCDWriteData(0x59);
	LCDWriteData(0x10);
	LCDWriteData(0x12);
	LCDWriteData(0x08);
	LCDWriteData(0x03);
	LCDWriteData(0x09);
	LCDWriteData(0x05);
	LCDWriteData(0x1E);
	LCDWriteData(0x05);
	LCDWriteData(0x14);
	LCDWriteData(0x10);
	LCDWriteData(0x68);
	LCDWriteData(0x33);
	LCDWriteData(0x15);

	LCDWriteRegister(0xB1);
	LCDWriteData(0x40);
	LCDWriteData(0x08);
	LCDWriteData(0x53);
	LCDWriteData(0x09);
	LCDWriteData(0x11);
	LCDWriteData(0x09);
	LCDWriteData(0x02);
	LCDWriteData(0x07);
	LCDWriteData(0x09);
	LCDWriteData(0x1A);
	LCDWriteData(0x04);
	LCDWriteData(0x12);
	LCDWriteData(0x12);
	LCDWriteData(0x64);
	LCDWriteData(0x29);
	LCDWriteData(0x29);

	LCDWriteRegister(0xFF);
	LCDWriteData(0x77);
	LCDWriteData(0x01);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x11);

	LCDWriteRegister(0xB0);
	LCDWriteData(0x6D);  //6D 

	LCDWriteRegister(0xB1);   //vcom  
	LCDWriteData(0x1D);

	LCDWriteRegister(0xB2);
	LCDWriteData(0x87);

	LCDWriteRegister(0xB3);
	LCDWriteData(0x80);

	LCDWriteRegister(0xB5);
	LCDWriteData(0x49);

	LCDWriteRegister(0xB7);
	LCDWriteData(0x85);

	LCDWriteRegister(0xB8);
	LCDWriteData(0x20);

	LCDWriteRegister(0xC1);
	LCDWriteData(0x78);

	LCDWriteRegister(0xC2);
	LCDWriteData(0x78);

	LCDWriteRegister(0xD0);
	LCDWriteData(0x88);

	LCDWriteRegister(0xE0);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x02);

	LCDWriteRegister(0xE1);
	LCDWriteData(0x02);
	LCDWriteData(0x8C);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x03);
	LCDWriteData(0x8C);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x33);
	LCDWriteData(0x33);

	LCDWriteRegister(0xE2);
	LCDWriteData(0x33);
	LCDWriteData(0x33);
	LCDWriteData(0x33);
	LCDWriteData(0x33);
	LCDWriteData(0xC9);
	LCDWriteData(0x3C);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0xCA);
	LCDWriteData(0x3C);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x00);

	LCDWriteRegister(0xE3);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x33);
	LCDWriteData(0x33);

	LCDWriteRegister(0xE4);
	LCDWriteData(0x44);
	LCDWriteData(0x44);

	LCDWriteRegister(0xE5);
	LCDWriteData(0x05);
	LCDWriteData(0xCD);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x01);
	LCDWriteData(0xC9);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x07);
	LCDWriteData(0xCF);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x03);
	LCDWriteData(0xCB);
	LCDWriteData(0x82);
	LCDWriteData(0x82);

	LCDWriteRegister(0xE6);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x33);
	LCDWriteData(0x33);

	LCDWriteRegister(0xE7);
	LCDWriteData(0x44);
	LCDWriteData(0x44);

	LCDWriteRegister(0xE8);
	LCDWriteData(0x06);
	LCDWriteData(0xCE);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x02);
	LCDWriteData(0xCA);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x08);
	LCDWriteData(0xD0);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x04);
	LCDWriteData(0xCC);
	LCDWriteData(0x82);
	LCDWriteData(0x82);

	LCDWriteRegister(0xEB);
	LCDWriteData(0x08);
	LCDWriteData(0x01);
	LCDWriteData(0xE4);
	LCDWriteData(0xE4);
	LCDWriteData(0x88);
	LCDWriteData(0x00);
	LCDWriteData(0x40);

	LCDWriteRegister(0xEC);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x00);

	LCDWriteRegister(0xED);
	LCDWriteData(0xFF);
	LCDWriteData(0xF0);
	LCDWriteData(0x07);
	LCDWriteData(0x65);
	LCDWriteData(0x4F);
	LCDWriteData(0xFC);
	LCDWriteData(0xC2);
	LCDWriteData(0x2F);
	LCDWriteData(0xF2);
	LCDWriteData(0x2C);
	LCDWriteData(0xCF);
	LCDWriteData(0xF4);
	LCDWriteData(0x56);
	LCDWriteData(0x70);
	LCDWriteData(0x0F);
	LCDWriteData(0xFF);

	LCDWriteRegister(0xEF);
	LCDWriteData(0x10);
	LCDWriteData(0x0D);
	LCDWriteData(0x04);
	LCDWriteData(0x08);
	LCDWriteData(0x3F);
	LCDWriteData(0x1F);

	LCDWriteRegister(0xFF);
	LCDWriteData(0x77);
	LCDWriteData(0x01);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x00);

	LCDWriteRegister(0x11);
}

// Second half: after LCD_SLEEPOUT_MS from Sleep Out (the caller waits, and may do other work meanwhile)
void AdaFruit_Init_Finish() {

	LCDWriteRegister(0x35);
	LCDWriteData(0x00);

	LCDWriteRegister(0x3A);
	LCDWriteData(0x66);

	//LCDWriteRegister(0x11);
	//Delay(120);

	LCDWriteRegister(0x29);
}

void AdaFruit_Init() {
	AdaFruit_Init_Start();
	HAL_Delay(LCD_SLEEPOUT_MS);
	AdaFruit_Init_Finish();
}


// First half of the panel set-up, up to and including Sleep Out (11h)
void BuyDisplay_Init_Start() {

	// Refer to ST7701S datasheet and your TFT LCD datasheet in order to make teh following settings

	//*************************************************************************************************************************************
	LCDWriteRegister(0xFF);			// CND2BKxSEL (FFh/FF00h): Command2 BK3 Selection - Command bank selection - Bank 3
	LCDWriteData(0x77);
	LCDWriteData(0x01);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x13);	// 10011b
	
	LCDWriteRegister(0xEF);			// not known
	LCDWriteData(0x08);
	
	//*************************************************************************************************************************************
	LCDWriteRegister(0xFF);			// CND2BKxSEL (FFh/FF00h): Command2 BK0 Selection - Command bank selection - Bank 0 (system)
	LCDWriteData(0x77);
	LCDWriteData(0x01);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x10);	// 10000b

	LCDWriteRegister(0xC0);			// LNESET (C0h/C000h): Display Line Setting
	LCDWriteData(0x77);				// 960 lines	77
	LCDWriteData(0x00);				// 960 lines	00

	LCDWriteRegister(0xC1);			// PORCTRL (C1h/C100h): Porch Control 
	LCDWriteData(0x0A);				// VBP = 0A		(10 decimal)		VBPD
	LCDWriteData(0x0C);				// VFP = 0C		(12 decimal)		VFPD

	LCDWriteRegister(0xC2);			// INVSET (C2h/C200h): Inversion selection & Frame Rate Control 
	LCDWriteData(0x37);				// C200h: Inversion Selection NLINV = 7								originally 37
	LCDWriteData(0x08);				// C201h: Frame Rate Control RTNI =	8								originally = 0x02, tried 0x00

	LCDWriteRegister(0xC3);			// RGBCTRL (C3h/C300h): RGB control
	LCDWriteData(0x81);				// C300h: DE/HV=HV, VSP=L, HSP=L, DP=Rising, EP=High	=> 10000001										0x81						81
	LCDWriteData(0x38);				// C301h: HBP_HVRGB[7:0] (Horizontal Back Porch in HSYNC in cycles)	originally 0x05 = 5					0x01	0x20 works		50	00
	LCDWriteData(0x22);				// C302h: VBP_HVRGB[7:0] (Vertical Back Porch HSYNC in cycles)		originally 0x0D = 13				0x0D					0A	22

	//LCDWriteRegister(0xC3); //????    
	//LCDWriteData(0x02); //82 HVmode    02 DEmode 
	
	LCDWriteRegister(0xCC);			// not known
	LCDWriteData(0x10);
	
	LCDWriteRegister(0xB0);			// PVGAMCTRL (B0h/B000h): Positive Voltage Gamma Control 
	LCDWriteData(0x40);				// VC0P[3:0], AJ0P[1:0]
	LCDWriteData(0x14);				// VC4P[5:0], AJ1P[1:0]
	LCDWriteData(0x59);				// VC8P[5:0], AJ2P[1:0]
	LCDWriteData(0x10);				// VC16P[4:0]
	LCDWriteData(0x12);				// VC24P[4:0], AJ3P[1:0]
	LCDWriteData(0x08);				// VC52P[3:0]
	LCDWriteData(0x03);				// VC80P[5:0]
	LCDWriteData(0x09);				// VC108P[3:0]
	LCDWriteData(0x05);				// VC147P[3:0]
	LCDWriteData(0x1E);				// VC175P[5:0]
	LCDWriteData(0x05);				// VC203P[3:0]
	LCDWriteData(0x14);				// VC231P[4:0], AJ4P[1:0]
	LCDWriteData(0x10);				// VC239P[4:0]
	LCDWriteData(0x68);				// VC247P[5:0], AJ5P[1:0]
	LCDWriteData(0x33);				// VC251P[5:0], AJ6P[1:0]
	LCDWriteData(0x15);				// VC255P[4:0], AJ7P[1:0]

	LCDWriteRegister(0xB1);			// VCOMS (B1h/B100h):VCOM amplitude setting  
	LCDWriteData(0x40);				// VC0N[3:0], AJ0N[1:0]
	LCDWriteData(0x08);				// VC4N[5:0], AJ1N[1:0]
	LCDWriteData(0x53);				// VC8N[5:0], AJ2N[1:0]
	LCDWriteData(0x09);				// VC16N[4:0]
	LCDWriteData(0x11);				// VC24N[4:0], AJ3N[1:0]
	LCDWriteData(0x09);				// VC52N[3:0]
	LCDWriteData(0x02);				// VC80N[5:0]
	LCDWriteData(0x07);				// VC108N[3:0]
	LCDWriteData(0x09);				// VC147N[3:0]
	LCDWriteData(0x1A);				// VC175N[5:0]
	LCDWriteData(0x04);				// VC203N[3:0]
	LCDWriteData(0x12);				// VC231N[4:0], AJ4N[1:0]
	LCDWriteData(0x12);				// VC239N[4:0]
	LCDWriteData(0x64);				// VC247N[5:0], AJ5N[1:0]
	LCDWriteData(0x29);				// VC251N[5:0], AJ6N[1:0]
	LCDWriteData(0x29);				// VC255N[4:0], AJ7N[1:0]

	//*************************************************************************************************************************************
	LCDWriteRegister(0xFF);			// CND2BKxSEL (FFh/FF00h): Command2 BK1 Selection - Command bank selection - Bank 1
	LCDWriteData(0x77);
	LCDWriteData(0x01);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x11);	//10001b

	LCDWriteRegister(0xB0);			// BK1: VRHS (B0h/B000h): Vop Amplitude setting 
	LCDWriteData(0x6D);				// VRHA[7:0] = 0x6D (109 in decimal) - 4.9Vdc

	LCDWriteRegister(0xB1);			// VCOMS (B1h/B100h): VCOM amplitude setting 
	LCDWriteData(0x1D);

	LCDWriteRegister(0xB2);			// VGHSS (B2h/B200h): VGH Voltage setting  
	LCDWriteData(0x87);

	LCDWriteRegister(0xB3);			// TESTCMD (B3h/B300h): Internal TEST Command Setting  
	LCDWriteData(0x00);				// 0x80 = enable test (if LCD has routine), 0x00 = disable										originally 0x80 = enabled

	LCDWriteRegister(0xB5);			// VGLS (B5h/B500h): VGL Voltage setting  
	LCDWriteData(0x49);

	LCDWriteRegister(0xB7);			// PWCTRL1 (B7h/B700h): Power Control 1  
	LCDWriteData(0x85);

	LCDWriteRegister(0xB8);			// PWCTRL2 (B8h/B800h): Power Control 2 
	LCDWriteData(0x20);

	LCDWriteRegister(0xC1);			// SPD1 (C1h/C100h): Source pre_drive timing set1  
	LCDWriteData(0x78);

	LCDWriteRegister(0xC2);			// SPD2 (C2h/C200h): Source EQ2 Setting  
	LCDWriteData(0x78);

	LCDWriteRegister(0xD0);			// MIPISET1 (D0h/D000h): MIPI Setting 1  - Ignore if LCD is using parallel or SPI comms
	LCDWriteData(0x88);

	LCDWriteRegister(0xE0);			// SECTRL (E0h/E000h): Sunlight Readable Enhancement  
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x02);

	// NRCTRL (E1h/E100h): Noise Reduce Control
	uint8_t enablenr = 0;  // 1 to enable, 0 to disable noise reduction
	uint8_t levelnr = 0x02; // Noise reduction level (0b00, 0b01, 0b10, 0b11)
	uint8_t byte1nr = (enablenr ? (1 << 4) : 0x00) | (levelnr & 0x03);
	LCDWriteRegister(0xE1);
	LCDWriteData(byte1nr); // Byte 1: Enable/Disable and Level Adjust
	LCDWriteData(0x8C);  // Byte 2: Reserved/unknown
	LCDWriteData(0x00);  // Byte 3: Reserved/unknown
	LCDWriteData(0x00);  // Byte 4: Reserved/unknown
	LCDWriteData(0x03);  // Byte 5: Reserved/unknown
	LCDWriteData(0x8C);  // Byte 6: Reserved/unknown
	LCDWriteData(0x00);  // Byte 7: Reserved/unknown
	LCDWriteData(0x00);  // Byte 8: Reserved/unknown
	LCDWriteData(0x00);  // Byte 9: Reserved/unknown
	LCDWriteData(0x33);  // Byte 10: Reserved/unknown
	LCDWriteData(0x33);  // Byte 11: Reserved/unknown

	// SECTRL (E2h/E200h): Sharpness Control  
	uint8_t enablesc = 0;  // 1 to enable sharpness, 0 to disable
	uint8_t levelsc = 0x02; // Sharpness level (0b00, 0b01, 0b10, 0b11)
	// Construct Byte 1: Enable/Disable and Level Adjust
	uint8_t byte1sc = (enablesc ? (1 << 4) : 0x00) | (levelsc & 0x03);
	LCDWriteRegister(0xE2);
	LCDWriteData(byte1sc); // Byte 1: Enable/Disable and Level Adjust
	LCDWriteData(0x33);  // Byte 2: Reserved/unknown
	LCDWriteData(0x33);  // Byte 3: Reserved/unknown
	LCDWriteData(0x33);  // Byte 4: Reserved/unknown
	LCDWriteData(0x33);  // Byte 5: Reserved/unknown
	LCDWriteData(0xC9);  // Byte 6: Reserved/unknown
	LCDWriteData(0x3C);  // Byte 7: Reserved/unknown
	LCDWriteData(0x00);  // Byte 8: Reserved/unknown
	LCDWriteData(0x00);  // Byte 9: Reserved/unknown
	LCDWriteData(0xCA);  // Byte 10: Reserved/unknown
	LCDWriteData(0x3C);  // Byte 11: Reserved/unknown
	LCDWriteData(0x00);  // Byte 12: Reserved/unknown
	LCDWriteData(0x00);  // Byte 13: Reserved/unknown
	LCDWriteData(0x00);  // Byte 14: Reserved/unknown

	LCDWriteRegister(0xE3);			// CCCTRL (E3h/E300h): Color Calibration Control 
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x33);
	LCDWriteData(0x33);

	LCDWriteRegister(0xE4);			// SKCTRL (E4h/E400h): Skin Tone Preservation Control 
	LCDWriteData(0x44);
	LCDWriteData(0x44);
	
	LCDWriteRegister(0xE5);			// not known
	LCDWriteData(0x05);
	LCDWriteData(0xCD);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x01);
	LCDWriteData(0xC9);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x07);
	LCDWriteData(0xCF);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x03);
	LCDWriteData(0xCB);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	
	LCDWriteRegister(0xE6);			// not known
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x33);
	LCDWriteData(0x33);

	LCDWriteRegister(0xE7);			// not known
	LCDWriteData(0x44);
	LCDWriteData(0x44);

	LCDWriteRegister(0xE8);			// not known
	LCDWriteData(0x06);
	LCDWriteData(0xCE);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x02);
	LCDWriteData(0xCA);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x08);
	LCDWriteData(0xD0);
	LCDWriteData(0x82);
	LCDWriteData(0x82);
	LCDWriteData(0x04);
	LCDWriteData(0xCC);
	LCDWriteData(0x82);
	LCDWriteData(0x82);

	LCDWriteRegister(0xEB);			// not known
	LCDWriteData(0x08);
	LCDWriteData(0x01);
	LCDWriteData(0xE4);
	LCDWriteData(0xE4);
	LCDWriteData(0x88);
	LCDWriteData(0x00);
	LCDWriteData(0x40);

	LCDWriteRegister(0xEC);			// not known
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x00);

	LCDWriteRegister(0xED);			// not known
	LCDWriteData(0xFF);
	LCDWriteData(0xF0);
	LCDWriteData(0x07);
	LCDWriteData(0x65);
	LCDWriteData(0x4F);
	LCDWriteData(0xFC);
	LCDWriteData(0xC2);
	LCDWriteData(0x2F);
	LCDWriteData(0xF2);
	LCDWriteData(0x2C);
	LCDWriteData(0xCF);
	LCDWriteData(0xF4);
	LCDWriteData(0x56);
	LCDWriteData(0x70);
	LCDWriteData(0x0F);
	LCDWriteData(0xFF);

	LCDWriteRegister(0xEF);			// not known
	LCDWriteData(0x10);
	LCDWriteData(0x0D);
	LCDWriteData(0x04);
	LCDWriteData(0x08);
	LCDWriteData(0x3F);
	LCDWriteData(0x1F);
	
	//*************************************************************************************************************************************
	LCDWriteRegister(0xFF);			// CND2BKxSEL (FFh/FF00h): Command2 BKx Selection - Command bank selection - Disable Banl function
	LCDWriteData(0x77);
	LCDWriteData(0x01);
	LCDWriteData(0x00);
	LCDWriteData(0x00);
	LCDWriteData(0x00);	//00000b

	LCDWriteRegister(0x11);			// SLPOUT (11h/1100h): Sleep Out  
}

// Second half: after LCD_SLEEPOUT_MS from Sleep Out (the caller waits, and may do other work meanwhile)
void BuyDisplay_Init_Finish() {

	//LCDWriteRegister(0x34);			// TEON (35h/3500h): Tearing Effect Line OFF
	//LCDWriteData(0x00);

	LCDWriteRegister(0x35);			// TEON (35h/3500h): Tearing Effect Line ON  
	LCDWriteData(0x00);

	LCDWriteRegister(0x3A);			// COLMOD (3Ah/3A00h): Interface Pixel Format  
	LCDWriteData(0x55);				// 16bpp for compatibility with LT7680A-R
	//LCDWriteData(0x66);				// 18bpp
	
	LCDWriteRegister(0x29); 		// DISPON (29h/2900h): Display On 
}

void BuyDisplay_Init() {
	BuyDisplay_Init_Start();
	HAL_Delay(LCD_SLEEPOUT_MS);
	BuyDisplay_Init_Finish();
}
