/**
  ******************************************************************************
  * @file    lt_draw.c
  * LT7680 drawing: font and cursor set-up, text, colours, lines, the screen fill, custom glyphs and
  * the boot logo.
  ******************************************************************************
*/

#include "lt7680.h"
#include "main.h"
#include "spi.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>


//******************************************************************************
// ROUTINES

volatile uint16_t lt_draw_timeouts = 0;

// Wait for the drawing engine: bit 7 of its control register (67h line, 76h rectangle) reads 1 while it draws
static void LT_WaitDraw(uint8_t reg) {
    uint32_t n = 0;
    WriteRegister(reg);
    while ((ReadData() & 0x80) && ++n < LT_DRAW_POLLS) { }
    if (n >= LT_DRAW_POLLS && lt_draw_timeouts < 0xFFFF) lt_draw_timeouts++;
}

// Draw line on LCD, start point, end point, and RGB colour
// Width is 1-pixel (not polyline)
// Origin is top left on R6581T orientaton
void DrawLine(uint16_t startX, uint16_t startY, uint16_t endX, uint16_t endY, uint16_t colorRED, uint16_t colorGREEN, uint16_t colorBLUE) {

    // Start the drawing process
    //WriteRegister(0x67);
    //WriteData(0x01); // Set bit 0 to start drawing

    // Set the starting point X-coordinate
    WriteRegister(0x68); // DLHSR[7:0]
    WriteData(startX & 0xFF);
    WriteRegister(0x69); // DLHSR[12:8]
    WriteData((startX >> 8) & 0x1F);

    // Set the starting point Y-coordinate
    WriteRegister(0x6A); // DLVSR[7:0]
    WriteData(startY & 0xFF);
    WriteRegister(0x6B); // DLVSR[12:8]
    WriteData((startY >> 8) & 0x1F);

    // Set the ending point X-coordinate
    WriteRegister(0x6C); // DLHER[7:0]
    WriteData(endX & 0xFF);
    WriteRegister(0x6D); // DLHER[12:8]
    WriteData((endX >> 8) & 0x1F);

    // Set the ending point Y-coordinate
    WriteRegister(0x6E); // DLVER[7:0]
    WriteData(endY & 0xFF);
    WriteRegister(0x6F); // DLVER[12:8]
    WriteData((endY >> 8) & 0x1F);

    // Set line width
    //WriteRegister(0x63); // Line Width Register (Assumed for line width)
    //WriteData(lineWidth);

    // Set line color (Foreground Color Register)
    WriteRegister(0xD2); // Foreground Color Low Byte
    WriteData(colorRED);
    WriteRegister(0xD3); // Foreground Color Low Byte
    WriteData(colorGREEN);
    WriteRegister(0xD4); // Foreground Color Low Byte
    WriteData(colorBLUE);

    WriteRegister(0x67); // Draw Line/Triangle Control Register
    WriteData(0x80 | 0x00); // Start drawing (bit 7 = 1) and select "Draw Line" (bits 4-1 = 0000)

    LT_WaitDraw(0x67);      // so the next call can't reprogram the engine mid-line
}

// Print character to LCD
void ConfigureFontAndPosition(uint8_t fontSource, uint8_t characterHeight, uint8_t isoCoding, uint8_t fullAlignment, uint8_t chromaKeying, uint8_t rotation, uint8_t widthFactor, uint8_t heightFactor, uint8_t lineGap, uint8_t charSpacing, uint16_t cursorX, uint16_t cursorY) {

    /*
    fontSource,       memory area
    characterHeight,  00b: 16 dots, 01b: 24 dots, 10b: 32 dots
    isoCoding,        00b: ISO 8859-1, 01b: ISO 8859-2, 10b: ISO 8859-4, 11b: ISO 8859-5
    fullAlignment,    0: Disable, 1: Enable
    chromaKeying,     0: Disable, 1: Enable
    rotation,         0: Normal, 1: Counterclockwise 90 degrees
    widthFactor,      00b: X1, 01b: X2, 10b: X3, 11b: X4
    heightFactor,     00b: X1, 01b: X2, 10b: X3, 11b: X4
    lineGap,          Line gap in pixels (0-31)
    charSpacing,      Character-to-character spacing in pixels (0-63)
    cursorX,          X-coordinate for text cursor
    cursorY           Y-coordinate for text cursor
    */

    uint8_t ccr0 = 0; // Character Control Register 0
    uint8_t ccr1 = 0; // Character Control Register 1

    // Configure CCR0 (REG[CCh])
    ccr0 |= ((fontSource & 0b11) << 6);         // Font source selection
    ccr0 |= ((characterHeight & 0b11) << 4);    // Character height
    ccr0 |= (isoCoding & 0b11);                 // ISO coding

    WriteRegister(0xCC); // Write to CCR0
    WriteData(ccr0);

    // Configure CCR1 (REG[CDh])
    ccr1 |= (fullAlignment << 7);               // Full alignment
    ccr1 |= (chromaKeying << 6);                // Chroma keying
    ccr1 |= (rotation << 4);                    // Rotation
    ccr1 |= ((widthFactor & 0b11) << 2);        // Character width enlargement
    ccr1 |= (heightFactor & 0b11);              // Character height enlargement

    WriteRegister(0xCD); // Write to CCR1
    WriteData(ccr1);

    // Configure Character Line Gap (REG[D0h])
    WriteRegister(0xD0);
    WriteData(lineGap & 0x1F); // Line gap (5 bits)

    // Configure Character-to-Character Space (REG[D1h])
    WriteRegister(0xD1);
    WriteData(charSpacing & 0x3F); // Character spacing (6 bits)

    // Set Cursor Position
    WriteRegister(0x63); // X lower byte
    WriteData(cursorX & 0xFF);
    WriteRegister(0x64); // X upper byte
    WriteData((cursorX >> 8) & 0x1F);

    WriteRegister(0x65); // Y lower byte
    WriteData(cursorY & 0xFF);
    WriteRegister(0x66); // Y upper byte
    WriteData((cursorY >> 8) & 0x1F);
}




#if !LT_CLEAR_TEXT
// The whole page (memory 400 x 960, overscan included) to black: one filled rectangle from the drawing engine.
// Leaves the text colours black, as the text version did.
void ClearScreen() {
    SetTextColors(0x000000, 0x000000);      // the fill uses the foreground colour
    WriteRegister(0x68); WriteData(0);      // start X, Y = 0, 0
    WriteRegister(0x69); WriteData(0);
    WriteRegister(0x6A); WriteData(0);
    WriteRegister(0x6B); WriteData(0);
    WriteRegister(0x6C); WriteData((LCD_XSIZE_TFT - 1) & 0xFF);          // end X
    WriteRegister(0x6D); WriteData(((LCD_XSIZE_TFT - 1) >> 8) & 0x1F);
    WriteRegister(0x6E); WriteData((LCD_YSIZE_TFT - 1) & 0xFF);          // end Y
    WriteRegister(0x6F); WriteData(((LCD_YSIZE_TFT - 1) >> 8) & 0x1F);
    WriteRegister(0x76); WriteData(0xE0);   // start (bit 7), filled (bit 6), rectangle (bits 5-4 = 10)
    LT_WaitDraw(0x76);
}
#else
void ClearScreen() {
    uint16_t charWidth = 8;      // Character width in pixels
    uint16_t charHeight = 16;    // Character height in pixels

    SetTextColors(0x000000, 0x000000); // foreground, background - black

    // Configure the font and position once
    ConfigureFontAndPosition(
        0b00,    // Internal CGROM
        0b00,    // Font size
        0b00,    // ISO 8859-1
        0,       // Full alignment disabled
        0,       // Chroma keying disabled
        1,       // Rotate 90 degrees counterclockwise
        0b01,    // Width X1
        0b01,    // Height X1
        0,       // Line spacing
        0,       // Char spacing
        0,       // Cursor X
        0        // Cursor Y
    );

    // Iterate through rows and columns
    for (uint16_t y = 0; y < LCD_YSIZE_TFT; y += charWidth) {   // In rotation, width becomes Y step
        for (uint16_t x = 0; x < LCD_XSIZE_TFT; x += charHeight) { // In rotation, height becomes X step
            // Set cursor position
            WriteRegister(0x63); // X lower byte
            WriteData(x & 0xFF);
            WriteRegister(0x64); // X upper byte
            WriteData((x >> 8) & 0x1F);

            WriteRegister(0x65); // Y lower byte
            WriteData(y & 0xFF);
            WriteRegister(0x66); // Y upper byte
            WriteData((y >> 8) & 0x1F);

            // Print the character
            DrawText(" ");
        }
    }

}
#endif


// Draw text with LT7680 busy checking - this version no longer requires delays between DrawText() calls
void DrawText(const char* text)
{
	while (*text != '\0') {

		// Wait until LT7680 core/text engine is idle
		WaitForLT7680Ready();

		WriteRegister(0x04); // Register for writing text
		WriteData((uint8_t)*text); // Write each character

		++text;
	}

	// Wait until final character has completed
	WaitForLT7680Ready();
}


// Set text colours
void SetTextColors(uint32_t foreground, uint32_t background) {
    // Set foreground color
    WriteRegister(0xD2); // Foreground Red
    WriteData((foreground >> 16) & 0xFF);
    WriteRegister(0xD3); // Foreground Green
    WriteData((foreground >> 8) & 0xFF);
    WriteRegister(0xD4); // Foreground Blue
    WriteData(foreground & 0xFF);

    // Set background color
    WriteRegister(0xD5); // Background Red
    WriteData((background >> 16) & 0xFF);
    WriteRegister(0xD6); // Background Green
    WriteData((background >> 8) & 0xFF);
    WriteRegister(0xD7); // Background Blue
    WriteData(background & 0xFF);
}


// Register 0x03 - Bit 2 only
void Text_Mode(void) // Set the LCD to Text Mode
{
    uint8_t temp = 0;

    // Set Bit 2 to 1 for Text Mode
    temp |= (1 << 2);

    // Configure Bits 1-0 for Display RAM (if required)
    temp &= ~(0b11); // Clear Bits 1-0 to select Display RAM

    // Write the value to Register 0x03 (Text/Graphic Mode register)
    WriteRegister(0x03);
    WriteData(temp);
}


// Register 0x03 - Bit 2 only
void Graphics_Mode()     // - OK
{
    uint8_t temp = 0;

    // Configure the register directly for Text Mode
    temp |= (0 << 2); // Enable Graphics Mode (clear bit 2)

    // Write the value directly to the Text/Graphic Mode register (0x03)
    WriteRegister(0x03);
    WriteData(temp);
}


//**************************************************************************************************
// Custom glyphs (ohm, sigma) - loaded once into their own SDRAM regions

static uint32_t cgram_start_cached = 0xFFFFFFFFUL;

// REG[DBh-DEh] CGRAM start address: user-defined glyph code n is read from start + n * glyph_size
void SetCGRAMStart_LT(uint32_t addr) {
    if (addr == cgram_start_cached) return;
    WriteRegister(0xDB); WriteData(addr & 0xFF);
    WriteRegister(0xDC); WriteData((addr >> 8) & 0xFF);
    WriteRegister(0xDD); WriteData((addr >> 16) & 0xFF);
    WriteRegister(0xDE); WriteData((addr >> 24) & 0xFF);
    cgram_start_cached = addr;
}

// Canvas start address (REG[50h-53h]) - where graphic/pixel writes through REG[04h] land
void SetCanvasStart32_LT(uint32_t a) {
    WriteRegister(0x50); WriteData(a & 0xFF);
    WriteRegister(0x51); WriteData((a >> 8) & 0xFF);
    WriteRegister(0x52); WriteData((a >> 16) & 0xFF);
    WriteRegister(0x53); WriteData((a >> 24) & 0xFF);
}

// 16x32 glyphs, 64 bytes each (rows of 2 bytes, MSB = left pixel). Code 0 = ohm, code 1 = sigma.
static const uint8_t glyphs16x32[128] = {
    // code 0: ohm (Ian Johnston's glyph)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xF0, 0x1F, 0xF8,
    0x30, 0x0C, 0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 0x60, 0x06,
    0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 0x30, 0x0C, 0x18, 0x18, 0x0C, 0x30, 0x0C, 0x30, 0x0C, 0x30,
    0x7C, 0x3E, 0x7C, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // code 1: lowercase sigma (tools/make_sigma.py)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xFF, 0x0F, 0xFF, 0x1C, 0x70, 0x18, 0x30, 0x38, 0x38,
    0x30, 0x18, 0x30, 0x18, 0x30, 0x18, 0x30, 0x18, 0x30, 0x18, 0x38, 0x38, 0x18, 0x30, 0x1C, 0x70,
    0x0F, 0xE0, 0x07, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Write the glyph set. The bytes go through the memory data port into a canvas that is temporarily pointed at
// the glyph region, so they land at CGRAM start + code * size without touching the visible display buffer.
void LoadCustomGlyphs_LT(void) {
    uint16_t i;

    Graphics_Mode();                                   // REG[03h] = 0: pixel data to display RAM, not text
    SetCanvasStart32_LT(CGRAM_BASE_MAIN);
    ResetGraphicWritePosition_LT();
    SetGraphicRWYCoordinate_LT();
    for (i = 0; i < sizeof(glyphs16x32); i++) { WriteRegister(0x04); WriteData(glyphs16x32[i]); }

    SetCanvasStart32_LT(0x00000000UL);                 // back to the visible page
    ResetGraphicWritePosition_LT();
    SetGraphicRWYCoordinate_LT();
    Text_Mode();
    WaitForLT7680Ready();
    cgram_start_cached = 0xFFFFFFFFUL;                 // force the next draw to set the start address
}

//**************************************************************************************************
// Boot logo: a 1-bit bitmap (boot_logo.h) drawn in one colour on black into the visible page.
// Screen column -> memory Y, screen row -> memory X (the panel is rotated 90 degrees). The canvas start address is moved to the
// bitmap's top-left corner and the active window set to the bitmap's size, so the pixel stream through REG[04h] fills exactly it.
#include "boot_logo.h"
uint16_t BootLogoWidth_LT(void) { return BOOT_LOGO_W; }

void DrawBootLogo_LT(uint16_t x0, uint16_t y0, uint16_t rgb565) {   // top-left at memory X = x0 (even), Y = y0
    uint32_t a = ((uint32_t)y0 * LCD_XSIZE_TFT + x0) * 2u;   // 16 bpp; x0 must be even so the start is 4-byte aligned
    uint16_t r, c;

    Graphics_Mode();
    SetCanvasStart32_LT(a);
    WriteRegister(0x56); WriteData(0x00); WriteRegister(0x57); WriteData(0x00);               // window upper-left = canvas start
    WriteRegister(0x58); WriteData(0x00); WriteRegister(0x59); WriteData(0x00);
    WriteRegister(0x5A); WriteData(BOOT_LOGO_H & 0xFF); WriteRegister(0x5B); WriteData(BOOT_LOGO_H >> 8);   // width  (memory X) = bitmap height
    WriteRegister(0x5C); WriteData(BOOT_LOGO_W & 0xFF); WriteRegister(0x5D); WriteData(BOOT_LOGO_W >> 8);   // height (memory Y) = bitmap width
    ResetGraphicWritePosition_LT();
    SetGraphicRWYCoordinate_LT();
    for (c = 0; c < BOOT_LOGO_W; c++) {
        for (r = 0; r < BOOT_LOGO_H; r++) {
            uint8_t on = (boot_logo[r * BOOT_LOGO_BPR + (c >> 3)] >> (7 - (c & 7))) & 1;
            uint16_t px = on ? rgb565 : 0x0000;
            WriteRegister(0x04); WriteData(px & 0xFF);       // low byte first (to be confirmed on the panel)
            WriteRegister(0x04); WriteData(px >> 8);
        }
    }
    SetCanvasStart32_LT(0x00000000UL);                       // back to the visible page and the full-screen window
    ConfigureActiveDisplayArea_LT();
    ResetGraphicWritePosition_LT();
    SetGraphicRWYCoordinate_LT();
    Text_Mode();
    WaitForLT7680Ready();
}


