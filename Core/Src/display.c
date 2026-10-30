/**
  ******************************************************************************
  * @file    display.c
  * Screens outside the live display: the boot screen, the init fault line, Ian's splash, and the glyph
  * self-test.
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

extern volatile uint32_t dbg_loop_per_sec;

#define DURATION_MS 5000     // 5 seconds in milliseconds
#define TIMER_INTERVAL_MS 35 // The interval of your timed sub in milliseconds

uint32_t BackgroundColour = 0x000000; // Black
uint32_t AuxColourForeLS = 0xA0A0A0; // Grey

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

