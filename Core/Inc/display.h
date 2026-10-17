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
#define Xpos_MAIN				182			// These are actually the Y position on the R6581 because LCD is rotated 90deg in use. Values in pixels.
#define Xpos_AUX				280
#define Xpos_ANNUNC				126			// annunciator row (24-dot font, 48 px tall) sits just above the main line at 182
#define Ypos_ANNUC				2			// first label starts 2 px in; the last (SRQ) ends at ~953 of 960
#define Xpos_SPLASH				330
#define Ypos_MAIN				0			// start at far left
#define Ypos_AUX				60


#endif // DISPLAY_H
