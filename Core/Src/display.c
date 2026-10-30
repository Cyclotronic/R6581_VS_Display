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

static uint8_t CheckDisplayStatus(void);

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

// Draw cells from..to-1 of a line (cell i at y0 + i * pitch).
static void DrawCells(const char* s, int from, int to, const LineFmt* f)
{
	int i = from;
	while (i < to && s[i]) {
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
			while (i + len < to && s[i + len] && GlyphCodeFor(s[i + len]) < 0 && len < 31) { seg[len] = s[i + len]; len++; }
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

static void DrawLineWithGlyphs(const char* s, int n, const LineFmt* f)
{
	DrawCells(s, 0, n, f);
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


// 1 V mode (ONE_VOLT_MODE): "+ 999.99709   mVDC" becomes "+ 0.99999709   VDC". s holds the 18 main characters.
static void MainToOneVolt(char s[19])
{
	// Extract the numerical part, divide by 1000 all whilst remainng string data
	char prefix[19] = { 0 };       // To store the string before the numeric part
	char numericPart[13] = { 0 };  // To store the numeric part
	char suffix[] = "VDC";         // Fixed suffix
	char result[13] = { 0 };       // To store the transformed numeric part

	// Extract the prefix (including the sign) before the numeric part
	sscanf(s, "%[^0-9]", prefix);

	// Extract the numeric part (ignoring the sign)
	sscanf(s, "%*[^0-9]%12s", numericPart);

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

	// Clear and rebuild s
	memset(s, ' ', 18);  // Fill with spaces
	s[18] = '\0';        // Null-terminate the string
	s[0] = prefix[0];    // Set the sign (+ or -) at position 0
	s[1] = ' ';          // Add space after the sign
	strncpy(s + 2, result, strlen(result));            // Copy numericPart starting at position 2
	strncpy(s + 18 - strlen(suffix), suffix, strlen(suffix)); // Add the suffix
}


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

			MainToOneVolt(MaindisplayString);


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
uint8_t CheckDisplayStatus() {
	uint8_t turnedOn = 0;	// 1 = the display has just come back on: the caller redraws everything

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
			turnedOn = 1;
		}
	}

	// Update the previous status
	displayBlankPrevious = displayBlank;
	return turnedOn;
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

// ANNUNCIATORS
static const char* const AnnuncNames[18] = {
	"SMPL", "IDLE", "AUTO", "LOP", "NULL", "DFILT", "MATH", "AZERO",
	"ERROR", "", "FRONT", "REAR", "", "LO_G", "RMT", "TLK",   // ERR shown as ERROR; INFO and SLOT are unused per the manual and left blank (they share a position)
	"LTN", "SRQ"
};

// Y position of each annunciator: labels run one after another in 12 px character cells (24-dot font) with a
// gap between names. INFO and SLOT are blank and share the position of FRONT / LO_G. Layout after Hydron's display.c.
#define ANN_CELL 12
#define ANN_GAP  13
#define ANN_Y(chars, gaps) (Ypos_ANNUC + (chars) * ANN_CELL + (gaps) * ANN_GAP)
static const uint16_t AnnuncYCoords[18] = {
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

// One annunciator: ON in the annunciator colour (ERROR in red), OFF as its name drawn in black over itself
static void DrawAnnunciator(int i, _Bool on)
{
	SetTextColors(on ? ((i == 8) ? 0xFF0000 : AnnunColourFore) : 0x000000, 0x000000); // Foreground, Background: Black
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
	DrawText(AnnuncNames[i]);
}

void DisplayAnnunciators() {
	for (int i = 0; i < 18; i++) DrawAnnunciator(i, Annunc[i + 1]);
}


//******************************************************************************
// Redraw engine (Stage 6 of docs/REWORK-PLAN.md)
//
// Render_Update() builds the main and aux lines exactly as DisplayMain() and DisplayAux() draw them, compares them cell by
// cell with what it drew last, and draws only the runs of cells that changed, and only the annunciators that changed.
// Everything is drawn the first time, after DISPLAY OFF, when a colour changes, and when the caller asks (main.c repaints
// everything every FULL_REDRAW_MS). Returns 1 if it drew anything.

static struct {
	char main[LINE1_LEN];
	char aux[LINE2_LEN];
	_Bool ann[18];
	uint32_t mainCol, auxCol, annCol;
	uint8_t valid;
} shown;

volatile uint32_t render_calls, render_draws, render_full, render_runs, render_cells, render_ann;

// The main line as DisplayMain() draws it. Returns 1 for the plain-text case, the only one where DISPLAY OFF is checked.
static uint8_t BuildMain(char m[LINE1_LEN + 1])
{
	for (int i = 1; i <= LINE1_LEN; i++) MaindisplayString[i - 1] = G[i];
	MaindisplayString[LINE1_LEN] = '\0';
	memcpy(m, MaindisplayString, LINE1_LEN + 1);
	if (HasGlyph(m, LINE1_LEN)) return 0;
	if (oneVoltmode && onethousandmVmodedetected && (strstr(m, "OVERLOAD") == NULL) && (strpbrk(m, "0123456789") != NULL)) {
		MainToOneVolt(m);
		return 0;
	}
	return 1;
}

// The aux line as DisplayAux() draws it; also sets onethousandmVmodedetected, as DisplayAux() does
static void BuildAux(char a[LINE2_LEN + 1])
{
	for (int i = 0; i < LINE2_LEN; i++) a[i] = G[LINE1_LEN + 1 + i];
	a[LINE2_LEN] = '\0';
	if (HasGlyph(a, LINE2_LEN)) return;
	onethousandmVmodedetected = (strstr(a, "1000mV Range") != NULL);
	if (oneVoltmode && onethousandmVmodedetected) memcpy(a, "   1 V Range                 ", LINE2_LEN + 1);
}

// Draw each run of cells where now[] differs from was[] (every cell if all), and bring was[] up to date
static uint8_t DrawChanged(const char* now, char* was, int n, const LineFmt* f, uint32_t colour, uint8_t all)
{
	uint8_t drew = 0;
	for (int i = 0; i < n; ) {
		if (!all && now[i] == was[i]) { i++; continue; }
		int end = i + 1;
		while (end < n && (all || now[end] != was[end])) end++;
		SetTextColors(colour, 0x000000);
		DrawCells(now, i, end, f);
		memcpy(was + i, now + i, end - i);
		render_runs++;
		render_cells += end - i;
		drew = 1;
		i = end;
	}
	return drew;
}

#if EDGE_WIPE
static void EdgeWipe(void)       // Ian's right-edge wipe: memory Y 952..959 to black
{
	for (uint16_t y = 952; y < LCD_YSIZE_TFT; y++) DrawLine(0, y, LCD_XSIZE_TFT - 1, y, 0x00, 0x00, 0x00);
}
#endif

uint8_t Render_Update(uint8_t all)
{
	char m[LINE1_LEN + 1], a[LINE2_LEN + 1];
	uint8_t drew = 0;

	render_calls++;
	BuildAux(a);                                     // first: it sets onethousandmVmodedetected, which BuildMain() uses
	if (BuildMain(m) && CheckDisplayStatus()) all = 1;   // back on after DISPLAY OFF (glyphs reloaded): draw everything
	if (!shown.valid) all = 1;
#if EDGE_WIPE
	if (all) EdgeWipe();
#endif
	drew |= DrawChanged(m, shown.main, LINE1_LEN, &LineFmtMain, MainColourFore, all || shown.mainCol != MainColourFore);
	drew |= DrawChanged(a, shown.aux, LINE2_LEN, &LineFmtAux, AuxColourFore, all || shown.auxCol != AuxColourFore);
	for (int i = 0; i < 18; i++) {
		_Bool on = Annunc[i + 1];
		if (all || on != shown.ann[i] || (on && shown.annCol != AnnunColourFore)) {
			DrawAnnunciator(i, on);
			shown.ann[i] = on;
			render_ann++;
			drew = 1;
		}
	}
	shown.mainCol = MainColourFore;
	shown.auxCol = AuxColourFore;
	shown.annCol = AnnunColourFore;
	shown.valid = 1;
	if (all) render_full++;
	if (drew) render_draws++;
	return drew;
}

#if RENDER_VERIFY
//******************************************************************************
// Redraw check (bench, -DRENDER_VERIFY=1): the screen the engine leaves must match, pixel for pixel, the screen Ian's full
// redraw (DisplayMain/Aux/Annunciators) leaves. Display memory is read back over SPI and hashed per band, memory X 128..351:
// annunciators 128..183, main 184..287, aux 288..351. Also checks that the right-edge rows (Y 952..959) hold nothing lit
// outside the cells text reaches there, and that ClearScreen() leaves the whole page black. Results are read over SWD.
volatile uint32_t rv_checks, rv_mismatch, rv_mismatch_bands, rv_hash[2][3], rv_ms;
volatile uint32_t rv_self_checks, rv_self_mismatch, rv_self_cells;
volatile uint32_t rv_edge_checks, rv_edge_stray, rv_edge_first;
volatile uint32_t rv_clear_dirty = 0xFFFFFFFF, rv_clear_ms;
volatile char rv_bad_main[LINE1_LEN + 1], rv_bad_aux[LINE2_LEN + 1];   // G[] at the first mismatch

#define RV_X0 Xpos_ANNUNC          // 128: top of the annunciator row
#define RV_X1 (Xpos_AUX + 64)      // 352: below the aux row
static uint8_t rv_row[LCD_XSIZE_TFT * 2];

static void BandHashes(volatile uint32_t* h)
{
	h[0] = h[1] = h[2] = 2166136261u;                // FNV-1a
	for (uint16_t y = 0; y < LCD_YSIZE_TFT; y++) {
		LT_ReadPixels(RV_X0, y, RV_X1 - RV_X0, rv_row);
		for (int i = 0; i < (RV_X1 - RV_X0) * 2; i++) {
			int x = RV_X0 + i / 2, b = (x < Xpos_MAIN) ? 0 : (x < Xpos_AUX) ? 1 : 2;
			h[b] = (h[b] ^ rv_row[i]) * 16777619u;
		}
	}
	LT_ReadDone();
}

static void EdgeCheck(void)
{
	for (uint16_t y = 952; y < LCD_YSIZE_TFT; y++) {
		LT_ReadPixels(0, y, LCD_XSIZE_TFT, rv_row);
		for (uint16_t x = 0; x < LCD_XSIZE_TFT; x++) {
			if (!(rv_row[2 * x] | rv_row[2 * x + 1])) continue;
			if (y == 952 && x >= Xpos_ANNUNC && x < Xpos_ANNUNC + 48) continue;   // SRQ's cell ends at 952
			if (y <= 955 && x >= Xpos_AUX && x < Xpos_AUX + 64) continue;         // the last aux cell is 924..955
			if (!rv_edge_stray) rv_edge_first = ((uint32_t)y << 16) | x;
			rv_edge_stray++;
		}
	}
	LT_ReadDone();
	rv_edge_checks++;
}

// Hash the screen, redraw everything the old way over it, hash again. Returns the bands that differed.
uint32_t Render_Verify(void)
{
	uint32_t t0 = HAL_GetTick(), bands = 0;
	BandHashes(rv_hash[0]);
	DisplayMain();
	DisplayAux();
	DisplayAnnunciators();
	BandHashes(rv_hash[1]);
	for (int b = 0; b < 3; b++) if (rv_hash[0][b] != rv_hash[1][b]) bands |= 1u << b;
	if (bands && !rv_mismatch_bands) {
		for (int i = 0; i < LINE1_LEN; i++) rv_bad_main[i] = G[1 + i];
		for (int i = 0; i < LINE2_LEN; i++) rv_bad_aux[i] = G[LINE1_LEN + 1 + i];
	}
	rv_mismatch_bands |= bands;
	EdgeCheck();
	rv_ms = HAL_GetTick() - t0;
	return bands;
}

void Render_ClearCheck(void)      // after ClearScreen(): count lit pixels anywhere in the page
{
	uint32_t t0 = HAL_GetTick(), n = 0;
	for (uint16_t y = 0; y < LCD_YSIZE_TFT; y++) {
		LT_ReadPixels(0, y, LCD_XSIZE_TFT, rv_row);
		for (uint16_t x = 0; x < LCD_XSIZE_TFT; x++) if (rv_row[2 * x] | rv_row[2 * x + 1]) n++;
	}
	LT_ReadDone();
	rv_clear_dirty = n;
	rv_clear_ms = HAL_GetTick() - t0;
}

// Synthetic screens before the live loop: random changes to G[] and Annunc[] (text, ohm and sigma placeholders, the
// micro sign, whole lines, annunciators), each drawn by the engine and then checked against the full redraw.
static uint32_t rv_rand = 0x2545F491u;
static uint32_t RvRand(void) { rv_rand ^= rv_rand << 13; rv_rand ^= rv_rand >> 17; rv_rand ^= rv_rand << 5; return rv_rand; }

static char RvChar(void)
{
	static const char set[] = "0123456789.+-E VmkMDCARngeOHMSTAUZ" "$$\x7F\x7F\xB5";
	return set[RvRand() % (sizeof(set) - 1)];
}

void Render_SelfTest(uint16_t steps)
{
	uint32_t cells0 = render_cells;
	for (int i = 1; i <= 47; i++) G[i] = ' ';
	for (uint16_t s = 0; s < steps; s++) {
		uint32_t r = RvRand();
		switch (r % 6) {
		case 0:                                                    // a whole new main line
			for (int i = 1; i <= LINE1_LEN; i++) G[i] = RvChar();
			break;
		case 1:                                                    // a whole new aux line
			for (int i = LINE1_LEN + 1; i <= 47; i++) G[i] = RvChar();
			break;
		case 2:                                                    // annunciators
			for (int k = 0; k < 1 + (int)((r >> 8) % 4); k++) { int a = 1 + RvRand() % 18; Annunc[a] = !Annunc[a]; }
			break;
		default:                                                   // a few cells anywhere, like a changing reading
			for (int k = 0; k < 1 + (int)((r >> 8) % 4); k++) G[1 + RvRand() % 47] = RvChar();
			break;
		}
		Render_Update(0);
		rv_self_checks++;
		if (Render_Verify()) rv_self_mismatch++;
	}
	rv_self_cells = render_cells - cells0;
	for (int i = 1; i <= 47; i++) G[i] = ' ';
	for (int i = 1; i <= 18; i++) Annunc[i] = 0;
	Render_Update(0);
}
#endif

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
// It stays inside the live layout's visible band, X 128 (top of the annunciators) to 352 (bottom of the aux row), and is centred
// across the visible width (Y 0..951; the right-edge wipe covers 952..959). Rows, top to bottom (X = screen row):
//   speed line (grey, 24-dot) | two notes (white, 32-dot): the screen only appears for the first boots, hold DCV to bring it back |
//   timing numbers + boot n/N (grey, 24-dot) | credits (green, 24-dot) with Ian's character (same green, 48 px tall) to their right.
#define BOOT_SPEED_ROW_X    128
#define BOOT_NOTE1_ROW_X    176
#define BOOT_NOTE2_ROW_X    214
#define BOOT_SETTINGS_ROW_X 266
#define BOOT_LOGO_ROW_X     304   // even, so the logo's canvas start is 4-byte aligned; rows 304..351
#define BOOT_CREDIT_ROW_X   316   // 24-dot line centred on the logo
#define BOOT_VISIBLE_Y      952
#define BOOT_CREDIT_GAP     16    // between the credits and the logo
#define BOOT_GREEN          0x00FF00
#define BOOT_GREEN_565      0x07E0

// One line of text: height code 0b00/01/10 = 16/24/32-dot font; y == 0xFFFF centres it across the visible width.
static void BootLine(const char* s, uint8_t heightCode, uint16_t cellW, uint16_t x, uint16_t y, uint32_t colour)
{
	uint16_t w = (uint16_t)(strlen(s) * cellW);
	if (y == 0xFFFF) y = (w < BOOT_VISIBLE_Y) ? (BOOT_VISIBLE_Y - w) / 2 : 0;
	SetTextColors(colour, 0x000000);
	ConfigureFontAndPosition(
		0b00, heightCode, 0b00,   // built-in font, size, ISO 8859-1
		0, 0, 1,                  // alignment off, chroma key off, rotate 90
		0b00, 0b00, 1, 0,         // x1, x1, line spacing 1, char spacing 0
		x, y);
	DrawText(s);
}

void DisplayFaultLine(const char* s)
{
	BootLine(s, 0b10, 16, BOOT_NOTE1_ROW_X, 0xFFFF, 0xFF0000);
}

void DisplayBootScreen(uint8_t bootNumber, uint8_t of)
{
	char line[96];
	static const char credits[] = "Reverse engineering by MickleT / TFT LCD by Ian Johnston";

	ClearScreen();

	snprintf(line, sizeof(line), "BluePill speed = %lu", dbg_loop_per_sec);
	BootLine(line, 0b01, 12, BOOT_SPEED_ROW_X, 0xFFFF, AuxColourForeLS);

	snprintf(line, sizeof(line), "Splash + timing test: first %d boots only", (int)of);
	BootLine(line, 0b10, 16, BOOT_NOTE1_ROW_X, 0xFFFF, 0xFFFFFF);
	BootLine("After that, hold DCV at power-up to reset", 0b10, 16, BOOT_NOTE2_ROW_X, 0xFFFF, 0xFFFFFF);

	snprintf(line, sizeof(line), "%d %d %d %d %d %d %d %s   boot %d/%d",
		(int)LCD_VBPD, (int)LCD_VFPD, (int)LCD_VSPW, (int)LCD_HBPD, (int)LCD_HFPD, (int)LCD_HSPW, (int)REFRESH_RATE, ADA_BUY,
		(int)bootNumber, (int)of);
	BootLine(line, 0b01, 12, BOOT_SETTINGS_ROW_X, 0xFFFF, 0x909090);

	// credits and the logo as one centred group
	uint16_t cw = (uint16_t)(sizeof(credits) - 1) * 12;
	uint16_t y0 = (BOOT_VISIBLE_Y - (cw + BOOT_CREDIT_GAP + BootLogoWidth_LT())) / 2;
	BootLine(credits, 0b01, 12, BOOT_CREDIT_ROW_X, y0, BOOT_GREEN);
	DrawBootLogo_LT(BOOT_LOGO_ROW_X, y0 + cw + BOOT_CREDIT_GAP, BOOT_GREEN_565);
}
