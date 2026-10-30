/**
  ******************************************************************************
  * @file    lt_bench.c
  * LT7680 bench checks read over SWD: register dump (LT_REGDUMP), SDRAM retention soak (SDRAM_SOAK) and
  * display memory read-back (RENDER_VERIFY). None of it runs in a normal build.
  ******************************************************************************
*/

#include "lt7680.h"
#include "main.h"
#include "spi.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

//**************************************************************************************************
// Register readback for SWD (build with -DLT_REGDUMP=1). Stage 0 of docs/REWORK-PLAN.md.
// Reads the PLL, panel and SDRAM registers into lt_regdump[] (offsets LTRD_* in lt7680.h); tools/regdump.py decodes it.
// The SDRAM timing-parameter bank is read by setting REG[E4h] bit 2 for the duration of four reads, the same E4h writes
// SDRAM_Init_LT() already makes (0x04, then 0x00). Bit 0 (start initialisation) is never written as 1 here.
#if LT_REGDUMP
volatile uint8_t lt_regdump[LTRD_SIZE];
volatile uint8_t lt_regdump_request;     // set to 1 over SWD; the main loop dumps again and clears it

static uint8_t ReadReg_LT(uint8_t reg) {
    WriteRegister(reg);
    return ReadData();
}

void LT_RegDump(void) {
    static const uint8_t pll[6] = { 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A };
    uint8_t i, e4;

    lt_regdump[LTRD_STSR] = ReadStatus();
    lt_regdump[LTRD_R00] = ReadReg_LT(0x00);
    lt_regdump[LTRD_R01] = ReadReg_LT(0x01);
    lt_regdump[LTRD_R03] = ReadReg_LT(0x03);
    lt_regdump[LTRD_R12] = ReadReg_LT(0x12);
    for (i = 0; i < 6; i++) lt_regdump[LTRD_PLL + i] = ReadReg_LT(pll[i]);

    for (i = 0; i < 5; i++) lt_regdump[LTRD_SDR + i] = ReadReg_LT(0xE0 + i);   // normal bank E0h-E4h
    e4 = lt_regdump[LTRD_SDR + 4];

    WriteRegister(0xE4); WriteData((e4 & 0x0A) | 0x04);                         // select the timing-parameter bank
    lt_regdump[LTRD_E4_TBANK] = ReadReg_LT(0xE4);
    for (i = 0; i < 4; i++) lt_regdump[LTRD_TIM + i] = ReadReg_LT(0xE0 + i);
    WriteRegister(0xE4); WriteData(e4 & 0x0A);                                  // back to the normal bank
    lt_regdump[LTRD_E4_AFTER] = ReadReg_LT(0xE4);
    for (i = 0; i < 4; i++) lt_regdump[LTRD_SDR_AGAIN + i] = ReadReg_LT(0xE0 + i);

    lt_regdump[LTRD_MAGIC + 0] = 'L';
    lt_regdump[LTRD_MAGIC + 1] = 'T';
    lt_regdump[LTRD_MAGIC + 2] = 'R';
    lt_regdump[LTRD_MAGIC + 3] = 'D';
    lt_regdump[LTRD_SEQ]++;
}
#endif

//**************************************************************************************************
// SDRAM retention soak (build with -DSDRAM_SOAK=1): a non-visual check for the display-memory corruption.
// A pseudo-random pattern is written to SOAK_REGIONS areas of SDRAM that the panel never scans (like the glyph region),
// then read back through the memory data port and compared bit by bit. Reading a row also refreshes it, so each region
// is read at its own interval; the last one only when asked over SWD. Each write is read straight back once, so a wrong
// read procedure shows up as errors at time zero (soak_verify_errs) instead of passing silently. tools/soak.py reads it.
#if SDRAM_SOAK
static const uint32_t soak_period_ms[SOAK_REGIONS] = { 10000u, 60000u, 600000u, 0u };   // 0 = only on request
volatile uint32_t soak_errs[SOAK_REGIONS];       // bits wrong at the last read
volatile uint32_t soak_errs_max[SOAK_REGIONS];   // most bits wrong at any read
volatile uint32_t soak_reads[SOAK_REGIONS];      // reads done
volatile uint32_t soak_read_ms[SOAK_REGIONS];    // HAL tick of the last read
volatile uint32_t soak_written_ms;               // HAL tick of the pattern write
volatile uint32_t soak_verify_errs;              // bits wrong when read straight back after writing (must be 0)
volatile uint32_t soak_fifo_timeouts;            // read FIFO never became non-empty
volatile uint8_t  soak_request;                  // over SWD: bit n = read region n now, bit 7 = rewrite all patterns
volatile uint8_t  soak_peek[8];                  // first bytes of region 0 as last read, for checking byte order

static uint32_t SoakAddr(uint8_t r) { return SOAK_BASE + (uint32_t)r * SOAK_STRIDE; }

static uint8_t SoakByte(uint32_t *s) {           // xorshift32: fixed sequence per region, all bit patterns
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return (uint8_t)(x >> 24);
}

static uint8_t PopCount8(uint8_t v) {
    v = v - ((v >> 1) & 0x55);
    v = (v & 0x33) + ((v >> 2) & 0x33);
    return (v + (v >> 4)) & 0x0F;
}

static void SoakBegin(uint8_t r) {               // point the memory port at the start of region r
    Graphics_Mode();
    SetCanvasStart32_LT(SoakAddr(r));
    ResetGraphicWritePosition_LT();
    SetGraphicRWYCoordinate_LT();
}

static void SoakEnd(void) {                      // back to the visible page in text mode, as LoadCustomGlyphs_LT() leaves it
    SetCanvasStart32_LT(0x00000000UL);
    ResetGraphicWritePosition_LT();
    SetGraphicRWYCoordinate_LT();
    Text_Mode();
    WaitForLT7680Ready();
}

static void SoakWrite(uint8_t r) {
    uint32_t s = 0x9E3779B9u ^ (r * 0x85EBCA6Bu);
    uint16_t i;
    SoakBegin(r);
    for (i = 0; i < SOAK_BYTES; i++) { WriteRegister(0x04); WriteData(SoakByte(&s)); }
    WaitForLT7680Ready();
    SoakEnd();
}

static uint32_t SoakRead(uint8_t r) {            // returns the number of bits that differ from the pattern
    uint32_t s = 0x9E3779B9u ^ (r * 0x85EBCA6Bu), bad = 0;
    uint16_t i, t;
    uint8_t v;
    SoakBegin(r);
    WriteRegister(0x04);
    (void)ReadData();                            // the first read after setting the position is a dummy
    for (i = 0; i < SOAK_BYTES; i++) {
        for (t = 0; (ReadStatus() & 0x10) && t < 1000; t++) { }   // status bit 4: read FIFO empty
        if (t >= 1000) soak_fifo_timeouts++;
        v = ReadData();
        if (r == 0 && i < sizeof(soak_peek)) soak_peek[i] = v;
        bad += PopCount8(v ^ SoakByte(&s));
    }
    SoakEnd();
    return bad;
}

static void SoakCheck(uint8_t r) {
    uint32_t bad = SoakRead(r);
    soak_errs[r] = bad;
    if (bad > soak_errs_max[r]) soak_errs_max[r] = bad;
    soak_reads[r]++;
    soak_read_ms[r] = HAL_GetTick();
}

void SdramSoak_Start(void) {                     // write every pattern, read each straight back once
    uint8_t r;
    soak_verify_errs = 0;
    for (r = 0; r < SOAK_REGIONS; r++) {
        SoakWrite(r);
        soak_verify_errs += SoakRead(r);
        soak_errs[r] = soak_errs_max[r] = soak_reads[r] = 0;
        soak_read_ms[r] = HAL_GetTick();
    }
    soak_written_ms = HAL_GetTick();
}

void SdramSoak_Poll(void) {                      // call from the main loop, between draws
    uint8_t r, req = soak_request;
    if (req & 0x80) { SdramSoak_Start(); soak_request = 0; return; }
    for (r = 0; r < SOAK_REGIONS; r++) {
        if ((req & (1u << r)) || (soak_period_ms[r] && (HAL_GetTick() - soak_read_ms[r]) >= soak_period_ms[r]))
            SoakCheck(r);
    }
    if (req) soak_request = 0;
}
#endif

//**************************************************************************************************
// Display memory read-back (Stage 6 bench checks, display.c RENDER_VERIFY). Reads px pixels (16 bpp, 2 bytes each) of the
// visible page from memory X = x0, Y = y through the memory data port. Call LT_ReadDone() after the last row.
void LT_ReadPixels(uint16_t x0, uint16_t y, uint16_t px, uint8_t* buf) {
    uint16_t i, t;
    WaitForLT7680Ready();
    Graphics_Mode();
    WriteRegister(0x5F); WriteData(x0 & 0xFF);          // memory read position
    WriteRegister(0x60); WriteData((x0 >> 8) & 0x1F);
    WriteRegister(0x61); WriteData(y & 0xFF);
    WriteRegister(0x62); WriteData((y >> 8) & 0x1F);
    WriteRegister(0x04);
    (void)ReadData();                                   // the first read after setting the position is a dummy
    for (i = 0; i < px * 2u; i++) {
        for (t = 0; (ReadStatus() & 0x10) && t < 1000; t++) { }   // status bit 4: read FIFO empty
        buf[i] = ReadData();
    }
}

void LT_ReadDone(void) {                                // back to text mode, as LoadCustomGlyphs_LT() leaves it
    ResetGraphicWritePosition_LT();
    SetGraphicRWYCoordinate_LT();
    Text_Mode();
    WaitForLT7680Ready();
}

