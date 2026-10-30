/**
  ******************************************************************************
  * @file    lt7680.h
  * @brief   This file provides code for the setup
  *          of the LT7680
  ******************************************************************************
*/


#ifndef H
#define H

#include "stm32f1xx_hal.h"
#include "main.h"  // Assuming your pin definitions are in main.h
#include <stdint.h>

void LCDConfigTurnOff_LT(void);
void Ohms12x24SymbolStoreUCG(void);

//void DrawText(const char* text);

// Hardware control
void HardwareReset(void);

// Core commands
void WriteRegister(uint8_t reg);
void WriteData(uint8_t data);
uint8_t ReadStatus(void);
uint8_t ReadData(void);
void WriteDataToRegister(uint8_t reg, uint8_t value);

// Testing routines
//void OriginalFillSDRAM_LT(void);
//void BootClearToRed(void);
//void ClearSDRAM_LT(uint16_t color);
//void FillSDRAM_LT(uint16_t color);
void FillSDRAM(unsigned short color);
void TEST_Fill_initiate(void);
void TEST_SDRAM_Fill_LT(uint32_t startAddress, uint32_t length, uint16_t color);
void ClearSDRAM(void);
void SetActiveWindowFullScreen(void);
//void FastClearWithBTE(uint16_t color);
void FillSDRAMWithColor(void);
void DrawSmallSquare(void);
void WriteSinglePixel(void);
void FillSDRAMWithColorIntegrated(void);
void FillDisplayRAM(void);

// LT7680 Commands and Configuration
void SoftwareReset(void);
void SetBacklightFull(void);
void FillScreen(uint32_t color);
void SendAllToLT7680_LT(void);
//void SetBackgroundColor(color);
void Text_Mode(void);
void SetTextColors(uint32_t foreground, uint32_t background);
//void SetFontTypeSize(uint8_t fontType, uint8_t fontSize);
//void SetTextCursor(uint16_t x, uint16_t y);
//void DrawText(char* text);
//void ConfigureFontAndPosition(uint8_t fontSource, uint8_t characterHeight, uint8_t isoCoding, uint8_t fullAlignment, uint8_t chromaKeying, uint8_t rotation, uint8_t widthFactor, uint8_t heightFactor, uint8_t lineGap, uint8_t charSpacing, uint16_t cursorX, uint16_t cursorY)

// Register Configuration
void LT7680_PLL_Initial_LT(void);
void Configure_Main_PIP_Window_LT(void);
void SDRAM_Init_LT(void);
void SDRAM_SetRefresh_LT(void);         // rewrite the refresh interval for lt_mclk_mhz (after any PLL change)
void Check_SDRAM_Ready_LT(void);        // SDRAM_INIT_LEGACY only
extern uint16_t lt_mclk_mhz;            // memory clock programmed by LT7680_PLL_Initial_LT()
extern volatile uint16_t lt_sdram_init_timeouts;
void Set_LCD_Panel_LT(void);
void LCD_HorizontalWidth_VerticalHeight_LT(uint16_t WX, uint16_t HY);
void LCD_Horizontal_Non_Display_LT(uint16_t val);
void LCD_HSYNC_Start_Position_LT(uint16_t val);
void LCD_HSYNC_Pulse_Width_LT(uint16_t val);
void LCD_Vertical_Non_Display_LT(uint16_t val);
void LCD_VSYNC_Start_Position_LT(uint16_t val);
void LCD_VSYNC_Pulse_Width_LT(uint16_t val);
void LCDConfig_LT(void);
void LCDTurnOn_LT(void);
void PWM_Prescaler_LT(void);
void PWM_Clock_Mux_LT(void);
void PWM_Configuration_LT(void);
void Software_Reset_LT(void);
void Software_ResetPLL_LT(void);
void Set_MISA_LT(void);
void SetMainImageWidth_LT(void);
void SetMainWindowUpperLeftX_LT(void);
void SetActiveWindow_LT(void);
void SetColorDepth_LT(void);
void ConfigureActiveDisplayArea_LT(void);
void ResetGraphicWritePosition_LT(void);
void SetGraphicRWYCoordinate_LT(void);
void SetCanvasStartAddress_LT(void);
void SetCanvasImageWidth_LT(void);
//void ClearScreen(void);

void DrawLine(uint16_t startX, uint16_t startY,uint16_t endX, uint16_t endY,uint16_t colorRED, uint16_t colorGREEN, uint16_t colorBLUE);


// Pin definitions for LT7680 controller
// The SCK, MOSI, MISO, and CS pins are defined and configured as part of the SPI peripheral initialization in the STM32 HAL driver setup.
// These are typically specified in the MX_SPI_Init function in the main.c file when using STM32CubeMX to generate code.
// SCK = PA5
// MISO = PA6
// MOSI = PA7
// CS = PA4

// Required by LT7680 controller interface
#define RESET_PIN				GPIO_PIN_10		// PB10
#define RESET_PORT				GPIOB

// GPIO macros
#define RESET_LOW()  HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_RESET)
#define RESET_HIGH() HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_SET)

// Chip Select Control Macros
#define SPI_CS_LOW()       HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_RESET)
#define SPI_CS_HIGH()      HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_SET)

// TFT LCD Panel parameters - (Some of them plus refresh rate are used to calculate CCLK, MCLK & PCLK)
extern uint32_t LCD_VBPD;
extern uint32_t LCD_VFPD;
extern uint32_t LCD_VSPW;
extern uint32_t LCD_HBPD;
extern uint32_t LCD_HFPD;
extern uint32_t LCD_HSPW;
extern uint32_t REFRESH_RATE;

// These have been moved to main.c as part of the user selectable timings
//#define LCD_VBPD				17			// Vertical Back Porch				17	17	17
//#define LCD_VFPD				14			// Vertical Front Porch				15	14	14		Adafruit tft timings say 15 but per forum user changed to 14 to stop flickering
//#define LCD_VSPW				4			// Vertical Start Position			2	4	2		Adafruit tft timings says 2 but leaves single vertical line at far right, 4 is no line but may flicker, 2 works better but has line
//#define LCD_HBPD				50			// Horizontal Back Porch			50
//#define LCD_HFPD				30			// Horizontal Front Porch			30
//#define LCD_HSPW				10			// Horizontal Start Position		10
//#define REFRESH_RATE			60			// Hz								60

#define LCD_XSIZE_TFT			400			// Resolution - Horizontal pixels (320 LCD is the 400 with first 80 missing)
#define LCD_YSIZE_TFT			960			// Resolution - Vertical Pixels
#define HSYNC_ACTIVE			0			// 0 = HSYNC Low Active
#define VSYNC_ACTIVE			0			// 0 = VSYNC Low Active
#define DE_ACTIVE				0			// 0 = DE High Active
#define PCLK_EDGE				0			// 0 = PCLK Rising Edge
#define HSYNC_IDLE_STATE		1			// HSYNC Idle State
#define VSYNC_IDLE_STATE		1			// VSYNC Idle State
#define PDE_IDLE_STATE			0			// PDE Idle STATE
#define PCLK_IDLE_STATE			0			// PCLK Idle State
#define PD_IDLE_STATE			0			// PD Idle State
#define SCLK_MAX				65			// Max allowable	65
#define MCLK_MAX				100			// Max allowable	100
#define CCLK_MAX				100			// Max allowable	100
#define LT_XI_MHZ				10u			// LT7680 crystal (XI)
// Clocks (Stage 2 of docs/REWORK-PLAN.md): memory and core clocks fixed instead of 2 x PCLK. LT_CLOCKS_LEGACY=1 restores
// Ian's clocks (MCLK = CCLK = 2 x PCLK, PCLK VCO below its 100 MHz minimum) for A/B comparison.
#ifndef LT_CLOCKS_LEGACY
#define LT_CLOCKS_LEGACY		0
#endif
#ifndef LT_MCLK_MHZ
#define LT_MCLK_MHZ				100u		// SDRAM clock (max 133)
#endif
#ifndef LT_CCLK_MHZ
#define LT_CCLK_MHZ				100u		// core clock: text, BTE and drawing engines (max 100)
#endif
#define LT_BL_PWM_HZ			3300u		// backlight PWM frequency (Ian's setting gave ~3.3 kHz at a 58 MHz core clock)
extern uint16_t lt_cclk_mhz;
extern uint32_t lt_pclk_khz;
// SDRAM set-up (Stage 1 of docs/REWORK-PLAN.md). SDRAM_INIT_LEGACY=1 restores Ian's sequence, which writes E0h-E3h
// into the timing-parameter bank and leaves auto-refresh disabled; kept only for A/B comparison.
#ifndef SDRAM_INIT_LEGACY
#define SDRAM_INIT_LEGACY		0
#endif
#define SDRAM_ATTR				0x29		// REG[E0h] for LT7680A-R: 4 banks, 4,096 rows, 512 columns (datasheet table 19-6)
#ifndef SDRAM_REFRESH_NS
#define SDRAM_REFRESH_NS		15000u		// one row every 15 us: 64 ms / 4,096 rows = 15.625 us, less ~4% margin
#endif
#define SDRAM_INIT_TIMEOUT_MS	100u		// give up waiting for SDRAM init after this (counted in lt_sdram_init_timeouts)
// Ian's values, used only by the legacy sequence:
#define SDRAM_MCLK				20			// MHz
#define SDRAM_CLKFREQ			64000000	// SDRAM clock frequency in Hz (clock source)		64000000
#define SDRAM_SIZE				8192		// 2048 = 64Mb, 8192 = 128Mb (depends on LT768x used), LT7680A-R is 128Mb
#define VSCAN_DIRECTION			0			// 0 = VSCAN Top to Bottom
#define TFT_BIT					0b01		// 0b01 = 18bit, 0b00 = 24bit, 0b10 = 16bit, 0b11 = No TFT output - 18bit bus LT7680 to ST7701S
#define HOST_BUS				0			// 0 = 8bit, 1 = 16bit
#define OUTPUT_SEQ				0b000		// 0b000 = RGB, 0b001 = RBG, 0b010 = GRB, 0b011 = GBR, 0b100 = BRG, 0b101 = BGR, 0b110 = Grey, 0b111 = Idle State			100
#define REG_CONTROL				0x00		// Control register address
#define REG_TEXT_CURSOR_X       0x20		// X-coordinate of text cursor
#define REG_TEXT_CURSOR_Y       0x21		// Y-coordinate of text cursor
#define MAIN_IMAGE_START		0x000000	// Main image start address set to 0 (MISA)			0x20004000
#define PD_OUTPUT_SEQ			0b000		// Parallel PD[23:0] Output Sequence				RGB

// User
#define DISP_TEST				0			// Display the LT7680 test card: 1 = test card, 0 = normal operation
#define BACKLIGHTFULL			100			// Backlighting brightness 0-100%
#define BACKLIGHTOFF			0			// Backlighting brightness 0-100%


// --- Added prototypes (needed by display.c/main.c and for calls before definitions) ---
void LCDConfigTurnOn_LT(void);

void Ohms16x32SymbolStoreUCG(void);

void ConfigureFontAndPosition(uint8_t fontSource,
    uint8_t characterHeight,
    uint8_t isoCoding,
    uint8_t fullAlignment,
    uint8_t chromaKeying,
    uint8_t rotation,
    uint8_t widthFactor,
    uint8_t heightFactor,
    uint8_t lineGap,
    uint8_t charSpacing,
    uint16_t cursorX,
    uint16_t cursorY);

void DrawText(const char* text);

void ClearScreen(void);

void ConfigurePWMAndSetBrightness(uint8_t brightnessPercentage);

#endif

// ---- Custom (user-defined) glyphs: ohm = code 0, sigma = code 1 --------------------------------------------
// The glyph set lives in its own region of SDRAM, away from the display buffer (page 0 = 0x000000..0x0BB7FF), so
// ordinary drawing can never overwrite them and they only need loading once (boot, and again after DISPLAY OFF).
#define CGRAM_BASE_MAIN   0x00200000UL   // 16x32 glyphs, 64 bytes each (main line, and the aux line drawn at 2x)
#define GLYPH_CODE_OHM    0
#define GLYPH_CODE_SIGMA  1
void SetCGRAMStart_LT(uint32_t addr);          // REG[DBh-DEh]: where user-defined glyph code 0 lives
void LoadCustomGlyphs_LT(void);                // write the glyph sets into both regions
void WaitForLT7680Ready(void);

// ---- SDRAM retention soak (non-visual corruption check); build with -DSDRAM_SOAK=1, read with tools/soak.py ------
#ifndef SDRAM_SOAK
#define SDRAM_SOAK 0
#endif
#if SDRAM_SOAK
#define SOAK_REGIONS   4             // read every 10 s, 60 s, 10 min, and only on request
#define SOAK_BASE      0x00300000UL  // above the display page (0..0xBB7FF) and the glyphs (0x200000), below 8 MB
#define SOAK_STRIDE    0x00010000UL  // 64 KB apart, so each region sits in different SDRAM rows
#define SOAK_BYTES     1024u         // per region (one 512-column row of 16-bit words)
void SdramSoak_Start(void);
void SdramSoak_Poll(void);
#endif

// ---- Register readback for SWD (Stage 0 of docs/REWORK-PLAN.md); build with -DLT_REGDUMP=1 ------------------
#ifndef LT_REGDUMP
#define LT_REGDUMP 0
#endif
#if LT_REGDUMP
#define LTRD_MAGIC         0    // 4 bytes "LTRD" once the first dump is complete
#define LTRD_SEQ           4    // dump count
#define LTRD_STSR          5    // status register
#define LTRD_R00           6    // REG[00h]
#define LTRD_R01           7    // REG[01h]
#define LTRD_R03           8    // REG[03h]
#define LTRD_R12           9    // REG[12h]
#define LTRD_PLL          10    // REG[05h-0Ah], 6 bytes
#define LTRD_SDR          16    // REG[E0h-E4h], normal bank, 5 bytes
#define LTRD_TIM          21    // REG[E0h-E3h] with E4h bit 2 set (timing-parameter bank), 4 bytes
#define LTRD_E4_TBANK     25    // REG[E4h] read while the timing bank is selected
#define LTRD_E4_AFTER     26    // REG[E4h] after switching back
#define LTRD_SDR_AGAIN    27    // REG[E0h-E3h], normal bank, read again after switching back, 4 bytes
#define LTRD_INIT_POLLS_LO 31   // Check_SDRAM_Ready_LT: number of E4h polls until bit 0 read 1
#define LTRD_INIT_POLLS_HI 32
#define LTRD_INIT_E4      33    // the E4h value that ended the poll
#define LTRD_INIT_STSR    34    // status register at that moment
#define LTRD_SIZE         36
extern volatile uint8_t lt_regdump[LTRD_SIZE];
extern volatile uint8_t lt_regdump_request;
void LT_RegDump(void);
#endif
void DrawBootLogo_LT(uint16_t x0, uint16_t y0, uint16_t rgb565);
uint16_t BootLogoWidth_LT(void);   // boot screen: Ian's character, white 1-bit bitmap at memory X/Y
