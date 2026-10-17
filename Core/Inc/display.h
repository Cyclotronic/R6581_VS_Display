/**
  ******************************************************************************
  * @file    display.h
  * @brief   This file contains all the function prototypes for
  *          the display.c file
  ******************************************************************************
*/


/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stddef.h>
#include "main.h"

//#ifdef __cplusplus
//extern "C" {
//#endif

// External global variable
extern char G[48];
extern _Bool Annunc[19];
extern uint32_t MainColourFore;
extern uint32_t AuxColourFore;
extern uint32_t AnnunColourFore;
extern _Bool oneVoltmode;

extern uint32_t LCD_VBPD;
extern uint32_t LCD_VFPD;
extern uint32_t LCD_VSPW;
extern uint32_t LCD_HBPD;
extern uint32_t LCD_HFPD;
extern uint32_t LCD_HSPW;
extern uint32_t REFRESH_RATE;
extern char ADA_BUY[5];

// Function prototypes
void DisplayMain(void);
void DisplayAuxFirstHalf(void);
void DisplayAuxSecondHalf(void);
void DisplayAnnunciatorsHalf(void);

void DisplaySplash(void);
void DisplayBootScreen(uint8_t bootNumber, uint8_t of);   // boot-time diagnostics screen (speed, credits, timing settings)

// ONE_VOLT_MODE: Ian's 1 V display mode (on the 1000 mV range, pressing DCV shows the reading in volts: "+ 0.99999709 VDC", aux "1 V Range").
// Default 0 = off: the DCV press no longer toggles it, so the code is inert. Build with -DONE_VOLT_MODE=1 to get Ian's behaviour back.
#ifndef ONE_VOLT_MODE
#define ONE_VOLT_MODE 0
#endif

// SPLASH_IN_LOOP=1 brings back Ian's splash drawn inside the live loop (it overlaps the live display); default 0 = boot screen only
#ifndef SPLASH_IN_LOOP
#define SPLASH_IN_LOOP 0
#endif
void DisplayAux(void);
void DisplayAnnunciators(void);

// Glyph self-test: set GLYPH_SELFTEST to 1 (e.g. -DGLYPH_SELFTEST=1) to run it once at boot before normal operation.
#ifndef GLYPH_SELFTEST
#define GLYPH_SELFTEST 0
#endif
#if GLYPH_SELFTEST
void GlyphSelfTest(void);
#endif

// Settings suited for 400x960 TFT LCD (320x960 physical)
#define Xpos_MAIN				184			// X = vertical position on the screen (the panel is rotated 90 deg); main line, 48x96 px cells
#define Xpos_AUX				288			// aux line, 32x64 px cells (32-dot font drawn x2)
#define Xpos_ANNUNC				128			// annunciator row, 12x48 px
#define Spacing_MAIN			5			// main line character spacing: pitch = 16 * 3 + 5 = 53
#define Spacing_AUX				1			// aux line character spacing: pitch = 16 * 2 + 1 = 33
#define Ypos_ANNUC				2			// first label starts 2 px in; the last (SRQ) ends at ~953 of 960
#define Xpos_SPLASH				376			// small text below the aux row (self-test label)
#define Ypos_MAIN				3			// first main cell starts 3 px in
#define Ypos_AUX				0			// aux line starts flush left


#endif // DISPLAY_H
