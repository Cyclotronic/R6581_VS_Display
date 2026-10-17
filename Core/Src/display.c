/**
  ******************************************************************************
  * @file    display.c
  * @brief   This file provides code for the
  *          display MAIN, AUX, ANNUNCIATORS & SPLASH.
  ******************************************************************************
*/


/* Includes ------------------------------------------------------------------*/
#include "spi.h"
#include "main.h"
#include "lcd.h"
#include "lt7680.h"
#include "display.h"
#include <string.h>  // For strchr, strncpy
#include <stdio.h>   // For debugging (optional)
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>		// required for float (soft FPU)


#define DURATION_MS 5000     // 5 seconds in milliseconds
#define TIMER_INTERVAL_MS 35 // The interval of your timed sub in milliseconds

// Display colours default
uint32_t MainColourFore = 0xFFFF00; // Yellow
uint32_t AuxColourFore = 0xFFFFFF; // White
uint32_t AnnunColourFore = 0x00FF00; // Green
uint32_t BackgroundColour = 0x000000; // Black
uint32_t AuxColourForeLS = 0xA0A0A0; // Grey

static void CheckDisplayStatus(void);

//******************************************************************************
// Custom glyph drawing
//
// The decode table maps the VFD's ohm and sigma patterns to placeholder characters ('$' and 0x7F). Those are not in
// the built-in font, so a line containing them is drawn in pieces: ordinary text from the built-in font, each
// placeholder from the user-defined glyph regions (see LoadCustomGlyphs_LT). One routine serves the MAIN and AUX lines.
#define GLYPH_PLACEHOLDER_OHM    '$'
#define GLYPH_PLACEHOLDER_SIGMA  '\x7F'

typedef struct {
	uint8_t  height;     // font height code: 0b01 = 24-dot, 0b10 = 32-dot
	uint8_t  mult;       // width/height multiplier code
	uint8_t  spacing;    // character spacing
	uint16_t pitch;      // pixels per character cell along the line (glyph width * multiplier + spacing)
	uint16_t x;          // cursor X (the line's position across the rotated display)
	uint16_t y0;         // cursor Y of the first character
	uint32_t cgram;      // glyph region for this size
} LineFmt;

static const LineFmt LineFmtMain = { 0b10, 0b10, Spacing_MAIN, 16 * 3 + Spacing_MAIN, Xpos_MAIN, Ypos_MAIN, CGRAM_BASE_MAIN };
// The aux line uses the 32-dot font drawn x2 (32x64 px cells), so it draws its glyphs from the same 16x32 set as the main line.
static const LineFmt LineFmtAux  = { 0b10, 0b01, Spacing_AUX,  16 * 2 + Spacing_AUX,  Xpos_AUX,  Ypos_AUX,  CGRAM_BASE_MAIN };

static int GlyphCodeFor(char c)
{
	if (c == GLYPH_PLACEHOLDER_OHM)   return GLYPH_CODE_OHM;
	if (c == GLYPH_PLACEHOLDER_SIGMA) return GLYPH_CODE_SIGMA;
	return -1;
}

static int HasGlyph(const char* s, int n)
{
	for (int i = 0; i < n && s[i]; i++) if (GlyphCodeFor(s[i]) >= 0) return 1;
	return 0;
}

static void DrawLineWithGlyphs(const char* s, int n, const LineFmt* f)
{
	int i = 0;
	while (i < n && s[i]) {
		int code = GlyphCodeFor(s[i]);
		if (code >= 0) {
			SetCGRAMStart_LT(f->cgram);
			ConfigureFontAndPosition(
				0b10,    // User-defined font
				f->height, 0b00, 0, 0, 1, f->mult, f->mult, 1, f->spacing,
				f->x, f->y0 + i * f->pitch);
			WriteRegister(0x04);
			WriteData(0x00);              // code, high byte
			WriteData((uint8_t)code);     // code, low byte
			WaitForLT7680Ready();
			i++;
		} else {
			char seg[32];
			int len = 0;
			while (i + len < n && s[i + len] && GlyphCodeFor(s[i + len]) < 0 && len < 31) { seg[len] = s[i + len]; len++; }
			seg[len] = '\0';
			ConfigureFontAndPosition(
				0b00,    // built-in font
				f->height, 0b00, 0, 0, 1, f->mult, f->mult, 1, f->spacing,
				f->x, f->y0 + i * f->pitch);
			DrawText(seg);
			i += len;
		}
	}
}


_Bool onethousandmVmodedetected;
char MaindisplayString[19] = "";              // String for G[1] to G[18]
_Bool displayBlank = false;
_Bool displayBlankPrevious = false;

extern volatile uint32_t dbg_loop_per_sec;

//float test15 = 0;
//char test16[12];


//uint16_t dollarPositionAUX = 0xFFFF;

//************************************************************************************************************************************************************


void DisplayMain() {

	// This sub needs re-written in the same way the AUX line is now written - it's on the todo list.

	// MAIN ROW - Print text to LCD, detect if there is an OHM symbol ($) and if so split into 3 parts, before-OHM-after
	SetTextColors(MainColourFore, 0x000000); // Foreground, Background

	//char MaindisplayString[19] = "";              // String for G[1] to G[18]
	char BeforeDollar[19] = "";                   // To store characters before the $
	char AfterDollar[19] = "";                    // To store characters after the $
	uint16_t dollarPosition = 0xFFFF;            // Initialize to an invalid position

	// Populate MaindisplayString from G[1] to G[18]
	for (int i = 1; i <= 18; i++) {
		MaindisplayString[i - 1] = G[i];
	}
	MaindisplayString[18] = '\0';                 // Null-terminate MaindisplayString

	// Find the position of the '$' symbol
	for (int i = 0; i < 18; i++) {
		if (MaindisplayString[i] == '$') {
			dollarPosition = i;                   // Record the position of '$'
			break;                                // Exit loop once found
		}
	}

	// If in 2W or 4W Resistance measurement mode the display will contain the OHM symbol on the MAIN display.
	// If it appears then the dollarPositionAUX var will not be 0xFFFF
	if (HasGlyph(MaindisplayString, 18)) {

		// Ohm and/or sigma on the MAIN line: drawn in pieces by the central routine
		DrawLineWithGlyphs(MaindisplayString, 18, &LineFmtMain);

	} else {

		// Every mode other than one that includes an OHM symbol
		// and also if OVERLOAD is not being displayed
		// and also that there are numbers being displayed, because when changing ranges manually the display can be blanked (no numbers)

		if (oneVoltmode && onethousandmVmodedetected && (strstr(MaindisplayString, "OVERLOAD") == NULL) && (strpbrk(MaindisplayString, "0123456789") != NULL)) {
		//if (oneVoltmode && onethousandmVmodedetected) {

			// Standard R6581 display non-OHM mode, user has selected 1VDC rather than 1000mV mode by pressing DCV button whilst on 1000mV mode

			// Take MaindisplayString and replace numerical part, i.e.:
			// "+ 999.99709   mVDC"
			// change to
			// "+ 0.99999709   VDC"

			char MaindisplayString[19] = "";              // String for G[1] to G[18]
			for (int i = 1; i <= 18; i++) {
				MaindisplayString[i - 1] = G[i];          // Copy characters
			}

			// Extract the numerical part, divide by 1000 all whilst remainng string data
			char prefix[19] = { 0 };       // To store the string before the numeric part
			char numericPart[13] = { 0 };  // To store the numeric part
			char suffix[] = "VDC";         // Fixed suffix
			char result[13] = { 0 };       // To store the transformed numeric part

			// Extract the prefix (including the sign) before the numeric part
			sscanf(MaindisplayString, "%[^0-9]", prefix);

			// Extract the numeric part (ignoring the sign)
			sscanf(MaindisplayString, "%*[^0-9]%12s", numericPart);

			// Find the position of the decimal point
			char* dot = strchr(numericPart, '.');
			int integerLength = dot ? (dot - numericPart) : strlen(numericPart); // Length of the integer part

			// Rebuild the result by dividing the value by 1000
			int resultIndex = 0;
			if (integerLength > 3) {
				// Copy digits before the new decimal point
				for (int i = 0; i < integerLength - 3; i++) {
					result[resultIndex++] = numericPart[i];
				}
				result[resultIndex++] = '.'; // Add the decimal point

				// Copy the remaining digits from the original integer part
				for (int i = integerLength - 3; i < integerLength; i++) {
					result[resultIndex++] = numericPart[i];
				}
			}
			else {
				// For numbers smaller than 1000, prepend "0." and leading zeros
				result[resultIndex++] = '0';
				result[resultIndex++] = '.';
				for (int i = 0; i < 3 - integerLength; i++) {
					result[resultIndex++] = '0';
				}
				// Copy the entire integer part
				for (int i = 0; i < integerLength; i++) {
					result[resultIndex++] = numericPart[i];
				}
			}

			// Copy the fractional part after the decimal point
			if (dot) {
				strcpy(result + resultIndex, dot + 1);
			}

			// Clear and rebuild MaindisplayString
			memset(MaindisplayString, ' ', 18);  // Fill with spaces
			MaindisplayString[18] = '\0';        // Null-terminate the string
			MaindisplayString[0] = prefix[0];    // Set the sign (+ or -) at position 0
			MaindisplayString[1] = ' ';          // Add space after the sign
			strncpy(MaindisplayString + 2, result, strlen(result));            // Copy numericPart starting at position 2
			strncpy(MaindisplayString + 18 - strlen(suffix), suffix, strlen(suffix)); // Add the suffix

			/*
			// atof test - needs work, not working
			// atof works, but after that string is empty for some reason
			float voltage;
			voltage = (atof(numericPart)) / 1000;
			// Format voltage into a string
			//char result2[12]; // Enough to hold the formatted voltage
			//snprintf(result2, sizeof(result2), "%.3f", voltage); // Format voltage to 3 decimal places
			//test16[12] = result2;
	
			char voltageString[12];
			snprintf(voltageString, sizeof(voltageString), "%.8f", voltage);
			test16[12] = voltageString[12];

			// Clear and rebuild MaindisplayString
			memset(MaindisplayString, ' ', 18);  // Fill with spaces
			MaindisplayString[18] = '\0';        // Null-terminate the string

			// Add the prefix (including the sign)
			if (prefix[0] != '\0') {
				MaindisplayString[0] = prefix[0];    // Set the sign (+ or -) at position 0
			}

			// Add a space after the sign
			MaindisplayString[1] = ' ';

			// Add the formatted numeric part
			strncpy(MaindisplayString + 2, voltageString, strlen(voltageString)); // Copy numeric part starting at position 2

			// Add the suffix at the end
			if (suffix[0] != '\0') {
				strncpy(MaindisplayString + 18 - strlen(suffix), suffix, strlen(suffix));
			}
			*/

			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b10,    // Font size
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b10,    // Width multiplier
				0b10,    // Height multiplier
				1,       // Line spacing
				LineFmtMain.spacing,   // Character spacing
				LineFmtMain.x,         // Cursor X
				LineFmtMain.y0         // Cursor Y
			);
			DrawText(MaindisplayString);

		} else {

			// Standard R6581 display non-OHM mode, including standard 1000mV mode

			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b10,    // Font size
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b10,    // Width multiplier
				0b10,    // Height multiplier
				1,       // Line spacing
				LineFmtMain.spacing,   // Character spacing
				LineFmtMain.x,         // Cursor X
				LineFmtMain.y0         // Cursor Y
			);
			char MaindisplayString[19] = "";              // String for G[1] to G[18]
			for (int i = 1; i <= 18; i++) {
				MaindisplayString[i - 1] = G[i];          // Copy characters
			}
			MaindisplayString[18] = '\0';                 // Null-terminate
			DrawText(MaindisplayString);

			// (strstr(MaindisplayString, "DISPLAY OFF") != NULL))
			// LCDConfigTurnOn_LT();
			// LCDConfigTurnOff_LT();
			CheckDisplayStatus();

		}

	}

}


//******************************************************************************


// "DISPLAY OFF" logic
void CheckDisplayStatus() {

	// Check if "DISPLAY OFF" is present in MaindisplayString
	displayBlank = (strstr(MaindisplayString, "DISPLAY OFF") != NULL);

	// If the display status has changed
	if (displayBlank != displayBlankPrevious) {
		if (displayBlank) {
			// Changed to "DISPLAY OFF"
			ConfigurePWMAndSetBrightness(0);
			LCDConfigTurnOff_LT();
		}
		else {
			// Changed from "DISPLAY OFF" to something else, i.e. user has pressed a button to revive
			ConfigurePWMAndSetBrightness(BACKLIGHTFULL);  // Configure Timer-1 and PWM-1 for backlighting. Settable 0-100%
			LCDConfigTurnOn_LT();
			LoadCustomGlyphs_LT();    // glyph memory is reloaded after DISPLAY OFF
		}
	}

	// Update the previous status
	displayBlankPrevious = displayBlank;

}


//******************************************************************************


void DisplayAux() {

	// AUX ROW text to LCD

	SetTextColors(AuxColourFore, 0x000000); // Foreground, Background

	char AuxdisplayString[30] = "";               // String for G[19] to G[47]
	uint16_t dollarPositions[5] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF }; // To store positions of up to 5 $
	int dollarCount = 0; // Count of $ symbols found

	// Customizable fudge factors for each $ symbol position
	//int fudgeFactors[5] = { 23, 23, 32, 104 }; // Adjust these values for each $ symbol's position

	// Populate AuxdisplayString from G[19] to G[47]
	for (int i = 19; i <= 47; i++) {
		AuxdisplayString[i - 19] = G[i];
	}
	AuxdisplayString[29] = '\0'; // Null-terminate at the 30th position because array starts at 0

	// Find positions of up to 5 $ symbols
	for (int i = 0; i < 30 && dollarCount < 5; i++) {
		if (AuxdisplayString[i] == '$') {
			dollarPositions[dollarCount++] = i; // Store position and increment count
		}
	}

	uint16_t yposohm = 0; // Initialize y-position for OHM symbol
	uint16_t yposoffset = Ypos_AUX; // Initialize y-position for OHM symbol

	int auxHasGlyph = HasGlyph(AuxdisplayString, 29);

	if (auxHasGlyph) {

		// Ohm and/or sigma on the AUX line: drawn in pieces by the central routine
		DrawLineWithGlyphs(AuxdisplayString, 29, &LineFmtAux);

	}

	// If no $ symbols were found, print the entire string as-is	

	if (!auxHasGlyph) {

		// Detect 1000mV Range
		//if ((strstr(MaindisplayString, "OVERLOAD") == NULL) &&			// does not contain
		//	(strpbrk(MaindisplayString, "0123456789") != NULL) &&		// does contain
		//	(strstr(AuxdisplayString, "1000mV Range") != NULL)) {		// does contain
		//	onethousandmVmodedetected = true;
		//} else {
		//	onethousandmVmodedetected = false;
		//	oneVoltmode = false;
		//}
		if ((strstr(AuxdisplayString, "1000mV Range") != NULL)) {		// does contain
			onethousandmVmodedetected = true;
		}
		else {
			onethousandmVmodedetected = false;
			//oneVoltmode = false;
		}

		// If in 1000mV range and user has enabled the new 1VDC mode
		if (oneVoltmode && onethousandmVmodedetected) {
			yposohm = Ypos_AUX;
		}
		else {
			yposohm = Ypos_AUX;
		}

		ConfigureFontAndPosition(
			0b00,    // Internal CGROM
			LineFmtAux.height,    // Font size: 32-dot, drawn x2
			0b00,    // ISO 8859-1
			0,       // Full alignment enabled
			0,       // Chroma keying disabled
			1,       // Rotate 90 degrees counterclockwise
			LineFmtAux.mult,    // Width multiplier
			LineFmtAux.mult,    // Height multiplier
			5,       // Line spacing
			LineFmtAux.spacing,       // Character spacing
			LineFmtAux.x,     // Cursor X
			yposohm       // Cursor Y
		);

		// If in 1000mV range and user has enabled the new 1VDC mode
		if (oneVoltmode && onethousandmVmodedetected) {
			DrawText("   1 V Range                 ");
		}
		else {
			DrawText(AuxdisplayString);
		}

	}
}


//******************************************************************************

void DisplayAnnunciators() {

	// ANNUNCIATORS - Print or clear text on the LCD
	const char* AnnuncNames[19] = {
		"SMPL", "IDLE", "AUTO", "LOP", "NULL", "DFILT", "MATH", "AZERO",
		"ERROR", "", "FRONT", "REAR", "", "LO_G", "RMT", "TLK",   // ERR shown as ERROR; INFO and SLOT are unused per the manual and left blank (they share a position)
		"LTN", "SRQ"
	};


	// Y position of each annunciator: labels run one after another in 12 px character cells (24-dot font) with a
	// gap between names. INFO and SLOT are blank and share the position of FRONT / LO_G. Layout after Hydron's display.c.
	#define ANN_CELL 12
	#define ANN_GAP  13
	#define ANN_Y(chars, gaps) (Ypos_ANNUC + (chars) * ANN_CELL + (gaps) * ANN_GAP)
	int AnnuncYCoords[19] = {
		ANN_Y(0, 0),    // SMPL
		ANN_Y(4, 1),    // IDLE
		ANN_Y(8, 2),    // AUTO
		ANN_Y(12, 3),   // LOP
		ANN_Y(15, 4),   // NULL
		ANN_Y(19, 5),   // DFILT
		ANN_Y(24, 6),   // MATH
		ANN_Y(28, 7),   // AZERO
		ANN_Y(33, 8),   // ERROR
		ANN_Y(38, 9),   // INFO (blank)
		ANN_Y(38, 9),   // FRONT
		ANN_Y(43, 10),  // REAR
		ANN_Y(47, 11),  // SLOT (blank)
		ANN_Y(47, 11),  // LO_G
		ANN_Y(51, 12),  // RMT
		ANN_Y(54, 13),  // TLK
		ANN_Y(57, 14),  // LTN
		ANN_Y(60, 15)   // SRQ
	};


	for (int i = 0; i < 18; i++) {
		if (Annunc[i + 1] == 1) {  // Turn the annunciator ON
			SetTextColors((i == 8) ? 0xFF0000 : AnnunColourFore, 0x000000); // Foreground: the annunciator colour (ERROR in red), Background: Black
			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b01,    // 24-dot font size (12 px wide, x2 tall = 12x48)
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b00,    // Width X0
				0b01,    // Height X0
				5,       // Line spacing
				0,       // Character spacing
				Xpos_ANNUNC,  // Cursor X (fixed)
				AnnuncYCoords[i] // Cursor Y (from array)
			);
			DrawText(AnnuncNames[i]); // Print the corresponding name
		}
		else {  // Turn the annunciator OFF
			SetTextColors(0x000000, 0x000000); // Foreground: Black, Background: Black
			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b01,    // 24-dot font size (12 px wide, x2 tall = 12x48)
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b00,    // Width X0
				0b01,    // Height X0
				5,       // Line spacing
				0,       // Character spacing
				Xpos_ANNUNC,  // Cursor X (fixed)
				AnnuncYCoords[i] // Cursor Y (from array)
			);
			DrawText(AnnuncNames[i]); // Clear the text by drawing in black
		}
	}

}

//******************************************************************************

void DisplaySplash() {
#if !SPLASH_IN_LOOP
	return;   // the splash now runs once at boot (DisplayBootScreen), not inside the live loop
#endif

	// Splash text to display
	static uint32_t cycle_count = 0; // Persistent counter for cycles
	static uint8_t timer_active = 1; // Flag to track timer status
	if (timer_active) {
		cycle_count++;
		// Check if the 5-second period has elapsed
		if (cycle_count >= (DURATION_MS / TIMER_INTERVAL_MS)) {
			// Runs once
			timer_active = 0; // Stop counting after 5 seconds
			SetTextColors(0x00FF00, 0x000000); // Foreground: Yellow, Background: Black
			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b00,    // Font size
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b00,    // Width multiplier
				0b00,    // Height multiplier
				1,       // Line spacing
				4,       // Character spacing
				Xpos_SPLASH,     // Cursor X
				100      // Cursor Y
			);
			char text[] = "                                                           ";
			DrawText(text);

			SetTextColors(0xFF0000, 0x000000); // Foreground: Yellow, Background: Black
			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b00,    // Font size
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b00,    // Width multiplier
				0b00,    // Height multiplier
				1,       // Line spacing
				4,       // Character spacing
				Xpos_SPLASH + 22,  // Cursor X: below the credit line, clear of the (taller) annunciator row at 126..174
				640      // Cursor Y
			);
			char text2[] = "                        ";
			DrawText(text2);

		}
		else {
			// Perform operations within the 5-second window
			// Splash text
			SetTextColors(0x00FF00, 0x000000); // Foreground: Yellow, Background: Black
			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b00,    // Font size
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b00,    // Width multiplier
				0b00,    // Height multiplier
				1,       // Line spacing
				4,       // Character spacing
				Xpos_SPLASH,     // Cursor X
				100      // Cursor Y
			);
			char text[] = "Reverse engineering by By MickleT / TFT LCD by Ian Johnston";
			DrawText(text);

			SetTextColors(0x909090, 0x000000); // Foreground: grey, Background: Black
			ConfigureFontAndPosition(
				0b00,    // Internal CGROM
				0b00,    // Font size
				0b00,    // ISO 8859-1
				0,       // Full alignment enabled
				0,       // Chroma keying disabled
				1,       // Rotate 90 degrees counterclockwise
				0b00,    // Width multiplier
				0b00,    // Height multiplier
				1,       // Line spacing
				4,       // Character spacing
				Xpos_SPLASH + 22,  // Cursor X: below the credit line, clear of the (taller) annunciator row at 126..174
				640      // Cursor Y
			);
			char textsettings[128]; // Ensure the buffer is large enough
			snprintf(textsettings, sizeof(textsettings),
				"%d %d %d %d %d %d %d %s",
				LCD_VBPD,
				LCD_VFPD,
				LCD_VSPW,
				LCD_HBPD,
				LCD_HFPD,
				LCD_HSPW,
				REFRESH_RATE,
				ADA_BUY
			);
			DrawText(textsettings);
		}
	}

}


// Write Cpu speed rating to the MAIN TFT.
void DisplayCloneDeterminationMain(void)
{
	SetTextColors(MainColourFore, BackgroundColour);
	ConfigureFontAndPosition(
		0b00,    // Internal CGROM
		0b10,    // Font size
		0b00,    // ISO 8859-1
		0,       // Full alignment enabled
		0,       // Chroma keying disabled
		1,       // Rotate 90 degrees counterclockwise
		0b11,    // Width multiplier
		0b11,    // Height multiplier
		1,       // Line spacing
		4,       // Character spacing
		Xpos_MAIN,
		Ypos_MAIN);
	char loopStr[32];

	strcpy(loopStr, "LS=");
	sprintf(&loopStr[strlen(loopStr)], "%lu", dbg_loop_per_sec);

	DrawText(loopStr);
}


// Write Cpu speed rating to the AUX TFT.
void DisplayCloneDeterminationAux(void)
{
	SetTextColors(AuxColourForeLS, BackgroundColour);
	ConfigureFontAndPosition(
		0b00,    // Internal CGROM
		0b01,    // Font size
		0b00,    // ISO 8859-1
		0,       // Full alignment enabled
		0,       // Chroma keying disabled
		1,       // Rotate 90 degrees counterclockwise
		0b01,    // Width multiplier
		0b01,    // Height multiplier
		5,       // Line spacing
		0,       // Character spacing
		Xpos_AUX,
		Ypos_AUX);
	char loopStr[32];

	strcpy(loopStr, "BluePill speed = ");
	sprintf(&loopStr[strlen(loopStr)], "%lu", dbg_loop_per_sec);

	DrawText(loopStr);
}


//******************************************************************************
// GLYPH SELF-TEST (build with -DGLYPH_SELFTEST=1)
//
// Runs once at boot, before normal operation. It fills G[] and Annunc[] with test data and calls the
// real DisplayMain()/DisplayAux()/DisplayAnnunciators(), so it exercises exactly the production
// drawing path (fonts, sizes, spacing, the $ -> ohm glyph split) without the meter sending anything.
//
//  Pages 1-6 : every printable ASCII char 0x20-0x7E (except '$', tested on the ohm pages) on the MAIN and AUX lines
//  Page  7   : the special codes the font table can return (micro, degree, arrows, diag-mode patterns)
//  Page  8   : ohm symbol, MAIN and AUX ("$" placeholder), 2W
//  Page  9   : all 18 annunciators ON
//  Page 10   : all annunciators OFF + a long worst-case line
//  Page 11-12: sigma / ohm custom glyphs (statistics screen; mixed on both lines)
// Each page is held for GLYPH_PAGE_MS. A page label is shown at the splash position.
//******************************************************************************
#if GLYPH_SELFTEST

#define GLYPH_PAGE_MS 9000

static void GlyphLabel(int page, int total)
{
	char buf[20];
	snprintf(buf, sizeof(buf), "GLYPH TEST %d/%d", page, total);
	SetTextColors(0x00FFFF, 0x000000);
	ConfigureFontAndPosition(
		0b00, 0b00, 0b00,   // CGROM, 16-dot, ISO 8859-1
		0, 0, 1,            // alignment off, chroma key off, rotate 90
		0b00, 0b00, 1, 4,   // X1, X1, line spacing 1, char spacing 4
		Xpos_SPLASH, 100);
	DrawText(buf);
}

static void GlyphPage(const char* main18, const char* aux29, _Bool annOn, int page, int total)
{
	int i;
	for (i = 1; i <= 47; i++) G[i] = ' ';
	for (i = 0; i < 18 && main18[i]; i++) G[1 + i] = main18[i];
	for (i = 0; i < 29 && aux29[i]; i++) G[19 + i] = aux29[i];
	for (i = 1; i <= 18; i++) Annunc[i] = annOn;

	ClearScreen();
	DisplayMain();
	DisplayAux();
	DisplayAnnunciators();
	GlyphLabel(page, total);
	Delay_NonBlocking(GLYPH_PAGE_MS);
}

void GlyphSelfTest(void)
{
	// All printable ASCII except '$' (94 chars)
	char all[96];
	int n = 0, c;
	for (c = 0x20; c <= 0x7E; c++) if (c != '$') all[n++] = (char)c;
	all[n] = '\0';

	const int total = 12;
	char main18[19], aux29[30];
	int page;

	for (page = 0; page < 6; page++) {
		int start = page * 18;
		memset(main18, ' ', 18); main18[18] = '\0';
		for (int i = 0; i < 18 && (start + i) < n; i++) main18[i] = all[start + i];
		// AUX shows the same 18 chars so every char is checked at both sizes
		memset(aux29, ' ', 29); aux29[29] = '\0';
		memcpy(aux29, main18, 18);
		GlyphPage(main18, aux29, 0, page + 1, total);
	}

	// Page 7: special codes
	{
		const unsigned char sp[18] = { 0xB5, 0xB0, 0x10, 0x11, 0x1A, 0x1E, 0x1F, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0B, 0x0C, 0x16 };
		for (int i = 0; i < 18; i++) main18[i] = (char)sp[i];
		main18[18] = '\0';
		memset(aux29, ' ', 29); aux29[29] = '\0';
		memcpy(aux29, main18, 18);
		GlyphPage(main18, aux29, 0, 7, total);
	}

	// Page 8: ohm symbol on both lines, in the same forms the meter sends
	GlyphPage("  1.234567    k$2W", " 10k$ Range  OHM$TEST $       ", 0, 8, total);

	// Page 9: every annunciator ON, with a realistic reading
	GlyphPage("- 1.234567     VDC", "  10 V Range                 ", 1, 9, total);

	// Page 10: annunciators OFF, worst-case wide line
	GlyphPage("-123.45678E+00 mVDC", "1000mV Range  STATISTICS    ", 0, 10, total);

	// Page 11: the statistics screen as the meter sends it - sigma label (0x7F placeholder) on the aux line
	GlyphPage(" 3.0651986E-04     ", "\x7F             \x1e\x1f" "key:NEXT DATA", 0, 11, total);

	// Page 12: ohm and sigma mixed on both lines, several per line
	GlyphPage("\x7F $ \x7F$ 1.234 $\x7F", "$ \x7F $ \x7F $\x7F  \x7F$  $$  ", 0, 12, total);

	ClearScreen();
	for (int i = 1; i <= 47; i++) G[i] = ' ';
	for (int i = 1; i <= 18; i++) Annunc[i] = 0;
}

#endif // GLYPH_SELFTEST


//******************************************************************************
// Boot screen: shown at boot for the first few boots, before the live display starts, so nothing else is being drawn.
// Layout: speed line (aux size) from DisplayCloneDeterminationAux; credit and timing lines below it in the 16-dot font.
#define BOOT_CREDIT_X    352   // 16-dot lines under the speed line (aux row 296..344 at boot): 352..368 and 374..390
#define BOOT_SETTINGS_X  374
void DisplayBootScreen(uint8_t bootNumber, uint8_t of)
{
	ClearScreen();
	DisplayCloneDeterminationAux();    // "BluePill speed = N"

	SetTextColors(0x00FF00, 0x000000);
	ConfigureFontAndPosition(
		0b00, 0b00, 0b00,   // built-in font, 16-dot, ISO 8859-1
		0, 0, 1,            // alignment off, chroma key off, rotate 90
		0b00, 0b00, 1, 4,   // X1, X1, line spacing 1, char spacing 4
		BOOT_CREDIT_X, 100);
	DrawText("Reverse engineering by By MickleT / TFT LCD by Ian Johnston");

	char line[112];
	snprintf(line, sizeof(line), "%d %d %d %d %d %d %d %s   boot %d/%d - hold DCV at power-up to repeat",
		(int)LCD_VBPD, (int)LCD_VFPD, (int)LCD_VSPW, (int)LCD_HBPD, (int)LCD_HFPD, (int)LCD_HSPW, (int)REFRESH_RATE, ADA_BUY,
		(int)bootNumber, (int)of);
	SetTextColors(0x909090, 0x000000);
	ConfigureFontAndPosition(
		0b00, 0b00, 0b00,
		0, 0, 1,
		0b00, 0b00, 1, 4,
		BOOT_SETTINGS_X, 100);
	DrawText(line);
}
