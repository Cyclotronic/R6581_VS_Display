/**
  ******************************************************************************
  * @file    settings.c
  * Settings page in flash (the last 1 KB page, 0x0800FC00): TFT timing sets, the boot counter, and the
  * timing-adjust mode that steps through the sets.
  ******************************************************************************
*/

#include "main.h"
#include "settings.h"
#include "lt7680.h"
#include "display.h"
#include "lcd.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// Boot diagnostics (BluePill speed test + splash screen): shown for the first BOOT_DIAG_SHOWS boots only.
// Holding DCV at power-up resets the count. The count lives in spare words of the settings page (see BootCountRead).
#define BOOT_MARKER_OFFSET   64         // byte offset in the settings page of the boot markers (settings use 0..31)
#define BOOT_MARKER_WORDS    16         // markers available; counting stops at BOOT_DIAG_SHOWS anyway
uint8_t boot_diag_number = 0;           // 1-based number of the boot being shown (0 = not shown)


// TFT timing vars
_Bool timingModsOnBoot = false;
_Bool timingModsOnBootDCV = false;
_Bool timingModspreviousstate = false;
uint8_t currentTimingSet = 0;		// Variable to track the current timing set (0 to 5)
static bool isFirstPress = true; // Tracks whether this is the first press
const uint32_t LCD_VBPD_SETTINGS[6]         = { 17, 17, 17, 17, 17, 10 };		// Define the timing settings for each mode
const uint32_t LCD_VFPD_SETTINGS[6]         = { 14, 14, 14, 15, 15, 12 };
const uint32_t LCD_VSPW_SETTINGS[6]         = { 2,  3,  4,  2,  2,  3 };
const uint32_t LCD_HBPD_SETTINGS[6]         = { 50, 50, 50, 50, 50, 80 };
const uint32_t LCD_HFPD_SETTINGS[6]         = { 30, 30, 30, 30, 30, 30 };
const uint32_t LCD_HSPW_SETTINGS[6]         = { 10, 10, 10, 10, 10, 20 };
const uint32_t REFRESH_RATE_SETTINGS[6]     = { 60, 60, 60, 60, 45, 60 };
const char ADA_BUY_SETTINGS[6][5]           = {"AdaF", "AdaF", "AdaF", "AdaF", "AdaF", "BuyD"};		// 4 characters plus the null terminator('\0').
uint8_t currentTimingSetNumberentries = 6;	// number of entries	
uint32_t LCD_VBPD = 17;
uint32_t LCD_VFPD = 14;
uint32_t LCD_VSPW = 2;
uint32_t LCD_HBPD = 50;
uint32_t LCD_HFPD = 30;
uint32_t LCD_HSPW = 10;
uint32_t REFRESH_RATE = 60;
char ADA_BUY[5] = "AdaF";
//******************************************************************************

// TFT LCD settings
uint32_t boot_LCD_VBPD;
uint32_t boot_LCD_VFPD;
uint32_t boot_LCD_VSPW;
uint32_t boot_LCD_HBPD;
uint32_t boot_LCD_HFPD;
uint32_t boot_LCD_HSPW;
uint32_t boot_REFRESH_RATE;
char boot_ADA_BUY[5];
uint32_t setting_LCD_VBPD;
uint32_t setting_LCD_VFPD;
uint32_t setting_LCD_VSPW;
uint32_t setting_LCD_HBPD;
uint32_t setting_LCD_HFPD;
uint32_t setting_LCD_HSPW;
uint32_t setting_REFRESH_RATE;
char setting_ADA_BUY[5];


// Write uint32_t to EEprom (Flash)
HAL_StatusTypeDef EEPROM_WriteData(uint32_t address, uint32_t data) {
	if (address < EEPROM_START_ADDRESS || address >= (EEPROM_START_ADDRESS + EEPROM_PAGE_SIZE)) {
		return HAL_ERROR; // Address out of range
	}
	HAL_FLASH_Unlock();
	HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, data);
	HAL_FLASH_Lock();
	return status;
}


// Write CHAR string to EEprom (Flash), 4 chars max
HAL_StatusTypeDef EEPROM_Write4CharString(uint32_t address, const char* str) {
	if (strlen(str) > 4) {
		return HAL_ERROR; // Limit the string length to 4 characters
	}

	uint32_t data = 0;

	// Copy up to 4 characters into a 32-bit word
	memcpy(&data, str, 4);

	HAL_FLASH_Unlock();
	HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, data);
	HAL_FLASH_Lock();

	return status;
}


// Read uint32_t from EEProm (Flash)
uint32_t EEPROM_ReadData(uint32_t address) {
	if (address < EEPROM_START_ADDRESS || address >= (EEPROM_START_ADDRESS + EEPROM_PAGE_SIZE)) {
		return 0xFFFFFFFF; // Invalid address
	}
	return *(uint32_t*)address;
}


// Read CHAR string from EEprom (Flash), 4 chars max
void EEPROM_Read4CharString(uint32_t address, char* buffer) {
	uint32_t data = *(uint32_t*)address; // Read the 32-bit word

	// Copy the data into the buffer
	memcpy(buffer, &data, 4);

	// Ensure the string is null-terminated
	buffer[4] = '\0';
}



// Erase EEprom (Flash) - necessary before write
HAL_StatusTypeDef EEPROM_ErasePage(uint32_t address) {
	if (address < EEPROM_START_ADDRESS || address >= (EEPROM_START_ADDRESS + EEPROM_PAGE_SIZE)) {
		return HAL_ERROR; // Address out of range
	}

	HAL_FLASH_Unlock();

	FLASH_EraseInitTypeDef eraseInit;
	uint32_t pageError;

	eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
	eraseInit.PageAddress = address;
	eraseInit.NbPages = 1; // Erase a single page

	HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&eraseInit, &pageError);

	HAL_FLASH_Lock();
	return status;
}


//**************************************************************************************************
// Boot counter in the settings page
//
// Flash bits can be cleared without erasing the page, so each boot programs the next unused (0xFFFFFFFF) marker word to 0.
// The count is the number of programmed markers. Anything that erases the settings page (timing mode, first-boot defaults,
// the DCV-held reset below) also clears the markers, which restarts the count.

uint8_t BootCountRead(void) {
	uint8_t n = 0;
	for (uint8_t i = 0; i < BOOT_MARKER_WORDS; i++) {
		if (EEPROM_ReadData(EEPROM_START_ADDRESS + BOOT_MARKER_OFFSET + 4u * i) == 0xFFFFFFFFu) break;
		n++;
	}
	return n;
}

void BootCountIncrement(uint8_t count) {
	if (count < BOOT_MARKER_WORDS)
		EEPROM_WriteData(EEPROM_START_ADDRESS + BOOT_MARKER_OFFSET + 4u * count, 0x00000000u);
}

// Erase the page and write the current TFT timing settings straight back, which clears the markers.
void BootCountReset(void) {
	EEPROM_ErasePage(EEPROM_START_ADDRESS);
	EEPROM_WriteData(EEPROM_START_ADDRESS, setting_LCD_VBPD);
	EEPROM_WriteData(EEPROM_START_ADDRESS + 4, setting_LCD_VFPD);
	EEPROM_WriteData(EEPROM_START_ADDRESS + 8, setting_LCD_VSPW);
	EEPROM_WriteData(EEPROM_START_ADDRESS + 12, setting_LCD_HBPD);
	EEPROM_WriteData(EEPROM_START_ADDRESS + 16, setting_LCD_HFPD);
	EEPROM_WriteData(EEPROM_START_ADDRESS + 20, setting_LCD_HSPW);
	EEPROM_WriteData(EEPROM_START_ADDRESS + 24, setting_REFRESH_RATE);
	EEPROM_Write4CharString(EEPROM_START_ADDRESS + 28, setting_ADA_BUY);
}



//**************************************************************************************************
// Load the TFT timing settings from the flash page; if they are invalid, write the defaults there first.
void Settings_Load(void) {
	// Load settings from EEProm (Flash)
	setting_LCD_VBPD = EEPROM_ReadData(EEPROM_START_ADDRESS);
	setting_LCD_VFPD = EEPROM_ReadData(EEPROM_START_ADDRESS + 4);
	setting_LCD_VSPW = EEPROM_ReadData(EEPROM_START_ADDRESS + 8);
	setting_LCD_HBPD = EEPROM_ReadData(EEPROM_START_ADDRESS + 12);
	setting_LCD_HFPD = EEPROM_ReadData(EEPROM_START_ADDRESS + 16);
	setting_LCD_HSPW = EEPROM_ReadData(EEPROM_START_ADDRESS + 20);
	setting_REFRESH_RATE = EEPROM_ReadData(EEPROM_START_ADDRESS + 24);
	EEPROM_Read4CharString(EEPROM_START_ADDRESS + 28, setting_ADA_BUY);


	// Copy retrieved vars from Flash for showing on splash screen
	boot_LCD_VBPD = setting_LCD_VBPD;
	boot_LCD_VFPD = setting_LCD_VFPD;
	boot_LCD_VSPW = setting_LCD_VSPW;
	boot_LCD_HBPD = setting_LCD_HBPD;
	boot_LCD_HFPD = setting_LCD_HFPD;
	boot_LCD_HSPW = setting_LCD_HSPW;
	boot_REFRESH_RATE = setting_REFRESH_RATE;
	strcpy(boot_ADA_BUY, setting_ADA_BUY);

	// Check if loaded values are ok
	if (setting_LCD_VBPD < 5 || setting_LCD_VBPD > 50) {

		// Assign default values
		setting_LCD_VBPD = LCD_VBPD;
		setting_LCD_VFPD = LCD_VFPD;
		setting_LCD_VSPW = LCD_VSPW;
		setting_LCD_HBPD = LCD_HBPD;
		setting_LCD_HFPD = LCD_HFPD;
		setting_LCD_HSPW = LCD_HSPW;
		setting_REFRESH_RATE = REFRESH_RATE;
		strcpy(setting_ADA_BUY, ADA_BUY);

		EEPROM_ErasePage(EEPROM_START_ADDRESS) == HAL_OK;

		EEPROM_WriteData(EEPROM_START_ADDRESS, setting_LCD_VBPD);
		EEPROM_WriteData(EEPROM_START_ADDRESS + 4, setting_LCD_VFPD);
		EEPROM_WriteData(EEPROM_START_ADDRESS + 8, setting_LCD_VSPW);
		EEPROM_WriteData(EEPROM_START_ADDRESS + 12, setting_LCD_HBPD);
		EEPROM_WriteData(EEPROM_START_ADDRESS + 16, setting_LCD_HFPD);
		EEPROM_WriteData(EEPROM_START_ADDRESS + 20, setting_LCD_HSPW);
		EEPROM_WriteData(EEPROM_START_ADDRESS + 24, setting_REFRESH_RATE);
		EEPROM_Write4CharString(EEPROM_START_ADDRESS + 28, setting_ADA_BUY);

	}

	// Populate the final vars to be used
	LCD_VBPD = setting_LCD_VBPD;
	LCD_VFPD = setting_LCD_VFPD;
	LCD_VSPW = setting_LCD_VSPW;
	LCD_HBPD = setting_LCD_HBPD;
	LCD_HFPD = setting_LCD_HFPD;
	LCD_HSPW = setting_LCD_HSPW;
	REFRESH_RATE = setting_REFRESH_RATE;
	strcpy(ADA_BUY, setting_ADA_BUY);
}


//**************************************************************************************************
// Timing-adjust page rows (X = top of the row). The meter's window shows the band the live display uses, X 128 to 352
// (docs/LAYOUT.md), so everything on this page stays inside it.
// Largest font that fits: six 32 px rows (20 px a character, so at most 47 characters across the 951 px), 36 px apart,
// ending at 340. On the bench the window shows down to about X 345.
#define TA_X_TITLE		128		// title and what to do
#define TA_X_HEADER		164		// column names
#define TA_X_CURRENT	200		// the set in use at power-up
#define TA_X_NEW		236		// the set just saved (after the first press)
#define TA_X_MCU		272		// MCU identity
#define TA_X_PANEL		308		// panel and LT7680

//**************************************************************************************************
// Two rows on the timing-adjust page identifying the hardware, for diagnosing boards that differ (docs/BLUE-PILL-CHIPS.md):
// the Blue Pill's core CPUID (0x411FC231 on ST's STM32F103), its device ID register (reads 0 on ST's STM32F103 without a
// debugger attached, an ST erratum), flash size, and whether the DWT cycle counter that DelayMicroseconds() relies on runs;
// then the panel's ST7701S ID bytes and power-mode byte (read at boot) and how the LT7680 set-up went. The LT7680 has no ID register.
static void DrawIdLine(uint16_t x, uint32_t colour, const char* s) {
	SetTextColors(colour, 0x000000);
	ConfigureFontAndPosition(
		0b00,    // Internal CGROM
		0b10,    // Font size
		0b00,    // ISO 8859-1
		0,       // Full alignment enabled
		0,       // Chroma keying disabled
		1,       // Rotate 90 degrees counterclockwise
		0b00,    // Width multiplier
		0b00,    // Height multiplier
		1,       // Line spacing
		4,       // Character spacing
		x,       // Cursor X
		0        // Cursor Y
	);
	DrawText(s);
}

static void DrawChipId(void) {
	char line[100];
	uint32_t c0 = DWT->CYCCNT;
	uint8_t cyc_ok = !(DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) && (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) && DWT->CYCCNT != c0;

	snprintf(line, sizeof(line), "MCU  CPUID %08lX ID %08lX %uKB CYCCNT %s",
		(unsigned long)SCB->CPUID, (unsigned long)DBGMCU->IDCODE, *(volatile uint16_t*)FLASHSIZE_BASE,
		cyc_ok ? "OK" : "NONE");
	DrawIdLine(TA_X_MCU, 0x00FFFF, line);

	char panel[24], lt[24];
	if (lcd_id == 0xFFFFFFFFu) strcpy(panel, "no reply");
	else snprintf(panel, sizeof(panel), "%06lX  Pwr %02X", (unsigned long)lcd_id, lcd_pwr & 0xFF);
	if (lt_fault) snprintf(lt, sizeof(lt), "fault %02X", lt_fault);
	else strcpy(lt, "OK");
	snprintf(line, sizeof(line), "Panel  ST7701S %s  LT7680 %s, %u %s", panel, lt, lt_init_attempts,
		lt_init_attempts == 1 ? "try" : "tries");
	DrawIdLine(TA_X_PANEL, 0x00FFFF, line);
}

//**************************************************************************************************
// Timing-adjust mode (DCV held at power-up): each DCV press steps to the next timing set, applies it and saves it.
// Called from the main loop's 35 ms tick.
void TimingAdjust_Poll(void) {

	// Timing mode adjust, toggle round TFT LCD timings using DCV button
	
	// Read pins A11/A12 - Front panel DCV switch momentary
	GPIO_PinState pinA11 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11);
	GPIO_PinState pinA12 = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_12);

	// DCV button pressed
	if (pinA11 == GPIO_PIN_SET && pinA12 == GPIO_PIN_RESET) {
		// Button is NOT pressed (normal state)
		timingModspreviousstate = false;
	} else if (pinA11 == GPIO_PIN_RESET && pinA12 == GPIO_PIN_RESET && pinA11 == pinA12) {
		// Button is pressed (both pins are the same, and LOW)
		if (!timingModspreviousstate) {
			// Toggle the mode on the first detection of the press
			timingModsOnBootDCV = !timingModsOnBootDCV;

			// On each press cycle round the various settings
			// Rotate through the timing settings
			currentTimingSet = (currentTimingSet + 1) % currentTimingSetNumberentries;  // Cycle through 0 to n
			// Update the user settings based on the current set

			if (isFirstPress == false) {
				setting_LCD_VBPD = LCD_VBPD_SETTINGS[currentTimingSet];
				setting_LCD_VFPD = LCD_VFPD_SETTINGS[currentTimingSet];
				setting_LCD_VSPW = LCD_VSPW_SETTINGS[currentTimingSet];
				setting_LCD_HBPD = LCD_HBPD_SETTINGS[currentTimingSet];
				setting_LCD_HFPD = LCD_HFPD_SETTINGS[currentTimingSet];
				setting_LCD_HSPW = LCD_HSPW_SETTINGS[currentTimingSet];
				setting_REFRESH_RATE = REFRESH_RATE_SETTINGS[currentTimingSet];
				strcpy(setting_ADA_BUY, ADA_BUY_SETTINGS[currentTimingSet]);

				LCD_VBPD = setting_LCD_VBPD;
				LCD_VFPD = setting_LCD_VFPD;
				LCD_VSPW = setting_LCD_VSPW;
				LCD_HBPD = setting_LCD_HBPD;
				LCD_HFPD = setting_LCD_HFPD;
				LCD_HSPW = setting_LCD_HSPW;
				REFRESH_RATE = setting_REFRESH_RATE;
				strcpy(ADA_BUY, setting_ADA_BUY);

				// run startup subs again to apply new settings
				AdaFruit_Init();
				//HAL_Delay(5);
				Delay_NonBlocking(5);  // Wait ms in a non-blocking way
				LT7680_PLL_Initial_LT();
				SDRAM_SetRefresh_LT();		// the memory clock follows PCLK, so the refresh interval must too
				//HAL_Delay(5);
				Delay_NonBlocking(5);  // Wait ms in a non-blocking way
				LCD_Horizontal_Non_Display_LT(LCD_HBPD);  // Horizontal Back Porch
				//HAL_Delay(5);
				Delay_NonBlocking(5);  // Wait ms in a non-blocking way
				LCD_HSYNC_Start_Position_LT(LCD_HFPD);    // HSYNC Start Position
				//HAL_Delay(5);
				Delay_NonBlocking(5);  // Wait ms in a non-blocking way
				LCD_HSYNC_Pulse_Width_LT(LCD_HSPW);       // HSYNC Pulse Width
				//HAL_Delay(5);
				Delay_NonBlocking(5);  // Wait ms in a non-blocking way
				LCD_Vertical_Non_Display_LT(LCD_VBPD);    // Vertical Back Porch
				//HAL_Delay(5);
				Delay_NonBlocking(5);  // Wait ms in a non-blocking way
				LCD_VSYNC_Start_Position_LT(LCD_VFPD);    // VSYNC Start Position
				//HAL_Delay(5);
				Delay_NonBlocking(5);  // Wait ms in a non-blocking way
				LCD_VSYNC_Pulse_Width_LT(LCD_VSPW);       // VSYNC Pulse Width
				//HAL_Delay(5);
				Delay_NonBlocking(5);  // Wait ms in a non-blocking way

				uint8_t bootsKept = BootCountRead();	// the page erase below also clears the boot counter
				EEPROM_ErasePage(EEPROM_START_ADDRESS) == HAL_OK;		// Erase Flash
				for (uint8_t b = 0; b < bootsKept; b++) BootCountIncrement(b);

				// Save the updated settings to flash
				EEPROM_WriteData(EEPROM_START_ADDRESS, setting_LCD_VBPD);
				EEPROM_WriteData(EEPROM_START_ADDRESS + 4, setting_LCD_VFPD);
				EEPROM_WriteData(EEPROM_START_ADDRESS + 8, setting_LCD_VSPW);
				EEPROM_WriteData(EEPROM_START_ADDRESS + 12, setting_LCD_HBPD);
				EEPROM_WriteData(EEPROM_START_ADDRESS + 16, setting_LCD_HFPD);
				EEPROM_WriteData(EEPROM_START_ADDRESS + 20, setting_LCD_HSPW);
				EEPROM_WriteData(EEPROM_START_ADDRESS + 24, setting_REFRESH_RATE);
				EEPROM_Write4CharString(EEPROM_START_ADDRESS + 28, setting_ADA_BUY);
			}

			//HAL_Delay(6);
			Delay_NonBlocking(6);  // Wait ms in a non-blocking way

			SetTextColors(0x00FF00, 0x000000); // Foreground: green, Background: Black
			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b10,    // Font size
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b00,    // Width multiplier
				0b00,    // Height multiplier
				1,       // Line spacing
				4,       // Character spacing
				TA_X_TITLE,     // Cursor X
				0      // Cursor Y
			);
			char text1[] = "TFT Timing: DCV select/save, pwr cycle to apply";
			DrawText(text1);

			//HAL_Delay(6);
			Delay_NonBlocking(6);  // Wait ms in a non-blocking way

			SetTextColors(0xFFFFFF, 0x000000); // Foreground: white, Background: Black

			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b10,    // Font size
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b00,    // Width multiplier
				0b00,    // Height multiplier
				1,       // Line spacing
				3,       // Character spacing (3: the 49-character rows fit the 951 px)
				TA_X_HEADER,     // Cursor X
				0       // Cursor Y
			);
			char text4[] = "          VBPD VFPD VSPW HBPD HFPD HSPW REFR COG";
			DrawText(text4);

			//HAL_Delay(6);
			Delay_NonBlocking(6);  // Wait ms in a non-blocking way

			SetTextColors(0xFFFF00, 0x000000); // Foreground: Yellow, Background: Black
			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b10,    // Font size
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b00,    // Width multiplier
				0b00,    // Height multiplier
				1,       // Line spacing
				3,       // Character spacing (3: the 49-character rows fit the 951 px)
				TA_X_CURRENT,     // Cursor X
				0      // Cursor Y
			);
			char redefineValuesCurr[128]; // Ensure the buffer is large enough
			snprintf(redefineValuesCurr, sizeof(redefineValuesCurr),
				"Current:  %d   %d   %d    %d   %d   %d   %d   %s",
				boot_LCD_VBPD,
				boot_LCD_VFPD,
				boot_LCD_VSPW,
				boot_LCD_HBPD,
				boot_LCD_HFPD,
				boot_LCD_HSPW,
				boot_REFRESH_RATE,
				boot_ADA_BUY
			);
			DrawText(redefineValuesCurr);

			//HAL_Delay(6);
			Delay_NonBlocking(6);  // Wait ms in a non-blocking way

			if (isFirstPress == false) {
				SetTextColors(0x00FF00, 0x000000); // Foreground: Yellow, Background: Black
				ConfigureFontAndPosition(
					0b00,    // Internal CGROM
					0b10,    // Font size
					0b00,    // ISO 8859-1
					0,       // Full alignment enabled
					0,       // Chroma keying disabled
					1,       // Rotate 90 degrees counterclockwise
					0b00,    // Width multiplier
					0b00,    // Height multiplier
					1,       // Line spacing
					3,       // Character spacing (3: the 49-character rows fit the 951 px)
					TA_X_NEW,     // Cursor X
					0      // Cursor Y
				);
				char redefineValues[128]; // Ensure the buffer is large enough
				snprintf(redefineValues, sizeof(redefineValues),
					"New/Saved:%d   %d   %d    %d   %d   %d   %d   %s",
					setting_LCD_VBPD,
					setting_LCD_VFPD,
					setting_LCD_VSPW,
					setting_LCD_HBPD,
					setting_LCD_HFPD,
					setting_LCD_HSPW,
					setting_REFRESH_RATE,
					setting_ADA_BUY
				);
				DrawText(redefineValues);
			}

			DrawChipId();		// chip identities, below the timing rows


			// Delay for button
			//HAL_Delay(120);
			Delay_NonBlocking(120);  // Wait ms in a non-blocking way

		}

		timingModspreviousstate = true;		// Update the previous state
		isFirstPress = false;				// Reset the flag after the first press
	}
}
