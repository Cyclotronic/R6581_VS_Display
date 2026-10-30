/**
  ******************************************************************************
  * @file    render.c
  * The live screen: the main and aux lines and the annunciators drawn from G[] and Annunc[], Ian's
  * full-redraw functions, the redraw engine that draws only what changed (Stage 6 of docs/REWORK-PLAN.md),
  * and its read-back check (RENDER_VERIFY).
  ******************************************************************************
*/

#include "spi.h"
#include "main.h"
#include "lt7680.h"
#include "display.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

// Display colours default
uint32_t MainColourFore = 0xFFFF00; // Yellow
uint32_t AuxColourFore = 0xFFFFFF; // White
uint32_t AnnunColourFore = 0x00FF00; // Green

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
	BuildMain(m);                                    // also fills MaindisplayString, which CheckDisplayStatus() reads
	if (CheckDisplayStatus()) all = 1;               // back on after DISPLAY OFF (glyphs reloaded): draw everything. Checked on
	                                                 // every frame: Ian's code only checked when the main line had no glyph, so
	                                                 // DISPLAY ON on an ohms range left the panel dark
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
