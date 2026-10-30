/**
  ******************************************************************************
  * @file    lt_init.c
  * LT7680 start-up: reset, PLLs, SDRAM, panel timing, display windows and backlight (Stages 1-3 of
  * docs/REWORK-PLAN.md).
  ******************************************************************************
*/

#include "lt7680.h"
#include "main.h"
#include "spi.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// Init robustness (Stage 3 of docs/REWORK-PLAN.md): results for SWD (tools/regdump.py prints them)
volatile uint8_t lt_fault = 0;
volatile uint8_t lt_init_attempts = 0;
volatile uint16_t lt_init_ms = 0;
volatile uint16_t lt_busy_timeouts = 0;
volatile uint16_t lt_pll_polls = 0;
volatile uint16_t lt_idle_polls = 0;
volatile uint8_t lt_resets = 0;         // hardware resets since power-up

#if LT_INIT_LEGACY
#define INIT_GAP(ms) HAL_Delay(ms)      // Ian's fixed waits between register groups
#else
#define INIT_GAP(ms) ((void)0)          // register writes need no gap; the PLL and SDRAM waits poll instead
#endif

void HardwareReset(void) {
    if (lt_resets < 0xFF) lt_resets++;
    LT_BusForget();
    HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_RESET); // Pull reset low
#if LT_INIT_LEGACY
    HAL_Delay(100); // Delay 100 ms
    HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_SET);   // Release reset
    HAL_Delay(100); // Delay 100 ms
#else
    HAL_Delay(LT_RESET_LOW_MS);
#if LT_FAULT_TEST
    if (lt_resets <= LT_FAULT_TEST) return;                   // bench test: leave the LT7680 held in reset
#endif
    HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_SET);   // Release reset; SendAllToLT7680_LT() waits for it
#endif
}

// Until the PLLs run, the LT7680 core runs from the 10 MHz crystal; keep the SPI clock well below that while talking to
// it then (72 MHz / 64 = 1.1 MHz), and go back to the normal rate (hspi1.Init, 9 MHz) once the PLLs are running.
static void LT_SpiSlow(uint8_t slow) {
    __HAL_SPI_DISABLE(&hspi1);          // BR must not change mid-transfer; HAL re-enables SPE on the next transfer
    MODIFY_REG(hspi1.Instance->CR1, SPI_CR1_BR, slow ? SPI_BAUDRATEPRESCALER_64 : hspi1.Init.BaudRatePrescaler);
}

// After a reset the status register's bit 1 reads 1 (inhibit: internal reset or power-on display init still running)
// until the chip can take commands; the Levetop library polls it the same way. Returns 1 once clear.
static uint8_t LT_WaitIdle_LT(void) {
    uint32_t t0 = HAL_GetTick();
    lt_idle_polls = 0;
    do {
        if (lt_idle_polls < 0xFFFF) lt_idle_polls++;
        if ((ReadStatus() & 0x02) == 0) return 1;
    } while ((HAL_GetTick() - t0) < LT_IDLE_TIMEOUT_MS);
    return 0;
}

// Reset and set up the LT7680, retrying once with a fresh hardware reset. Returns the LT_FAULT_* bits of the last try.
uint8_t LT_Init_LT(void) {
    uint32_t t0 = HAL_GetTick();
    uint8_t fault = 0;
    hspi1.Init.BaudRatePrescaler = LT_SPI_PRESC;   // the rate LT_SpiSlow(0) returns to
    for (lt_init_attempts = 1; lt_init_attempts <= LT_INIT_TRIES; lt_init_attempts++) {
        HardwareReset();
        fault = SendAllToLT7680_LT();
        if (fault == 0) break;
    }
    if (lt_init_attempts > LT_INIT_TRIES) lt_init_attempts = LT_INIT_TRIES;
    lt_fault = fault;
    lt_init_ms = (uint16_t)(HAL_GetTick() - t0);
    return fault;
}


//#define RESET_LOW()  HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_RESET)
//#define RESET_HIGH() HAL_GPIO_WritePin(RESET_PORT, RESET_PIN, GPIO_PIN_SET)


//**************************************************************************************************
// Subs to run and sent to the LT7680

// Returns LT_FAULT_* bits; stops early if the LT7680 does not answer at all.
uint8_t SendAllToLT7680_LT() {
    uint8_t fault = 0;
    uint16_t sdram_timeouts = lt_sdram_init_timeouts;

    LT_SpiSlow(1);                            // on the crystal clock until the PLLs run
    if (!LT_WaitIdle_LT()) fault |= LT_FAULT_RESET;   // hardware reset finished
    Software_Reset_LT();
    HAL_Delay(LT_SWRESET_MS);                 // the status register reads idle at once, but writes are lost for a while
    if (!LT_WaitIdle_LT()) fault |= LT_FAULT_RESET;
    fault |= LT7680_PLL_Initial_LT();         // Initialize PLL first for stable clocks (back to the normal SPI rate)
    if (fault & LT_FAULT_NOREPLY) return fault;
    INIT_GAP(100);
    SDRAM_Init_LT();                          // Initialize SDRAM after the reset
    if (lt_sdram_init_timeouts != sdram_timeouts) fault |= LT_FAULT_SDRAM;
    INIT_GAP(5);

    Set_LCD_Panel_LT();                       // Set up the panel interface
    INIT_GAP(100);

    //WriteRegister(0x84);                      // Set backlighting Prescaler to zero which effectively turns off backlighting
    //WriteData(0x00); // Prescaler = 00
    //HAL_Delay(5);

    LCDConfigTurnOn_LT();
    INIT_GAP(5);

    LCD_HorizontalWidth_VerticalHeight_LT(LCD_XSIZE_TFT, LCD_YSIZE_TFT);
    INIT_GAP(5);
    LCD_Horizontal_Non_Display_LT(LCD_HBPD);  // Horizontal Back Porch
    INIT_GAP(5);
    LCD_HSYNC_Start_Position_LT(LCD_HFPD);    // HSYNC Start Position
    INIT_GAP(5);
    LCD_HSYNC_Pulse_Width_LT(LCD_HSPW);       // HSYNC Pulse Width
    INIT_GAP(5);
    LCD_Vertical_Non_Display_LT(LCD_VBPD);    // Vertical Back Porch
    INIT_GAP(5);
    LCD_VSYNC_Start_Position_LT(LCD_VFPD);    // VSYNC Start Position
    INIT_GAP(5);
    LCD_VSYNC_Pulse_Width_LT(LCD_VSPW);       // VSYNC Pulse Width
    INIT_GAP(5);
    SetColorDepth_LT();                       // Configure canvas color depth
    INIT_GAP(5);
    Configure_Main_PIP_Window_LT();
    INIT_GAP(5);
    SetMainImageWidth_LT();
    INIT_GAP(5);
    SetMainWindowUpperLeftX_LT();
    INIT_GAP(5);
    ConfigureActiveDisplayArea_LT();
    INIT_GAP(5);
    SetActiveWindow_LT();                     // Set active window dimensions
    INIT_GAP(5);
    SetCanvasStartAddress_LT();
    INIT_GAP(5);
    SetCanvasImageWidth_LT();
    INIT_GAP(5);
    ResetGraphicWritePosition_LT();
    INIT_GAP(5);
    SetGraphicRWYCoordinate_LT();
    INIT_GAP(5);
    Set_MISA_LT();                            // Configure the Main Image Start Address
    INIT_GAP(5);
   
    Text_Mode();
    ClearScreen();                          // Draws black 'spaces' across the whole screen - fast
    return fault;
}

void ConfigureActiveDisplayArea_LT() {
    // Set Active Window to cover the entire screen
    WriteRegister(0x56); WriteData(0x00);  // X Start Low
    WriteRegister(0x57); WriteData(0x00);  // X Start High
    WriteRegister(0x58); WriteData(0x00);  // Y Start Low
    WriteRegister(0x59); WriteData(0x00);  // Y Start High
    WriteRegister(0x5A); WriteData((LCD_XSIZE_TFT - 1) & 0xFF);  // X End Low
    WriteRegister(0x5B); WriteData(((LCD_XSIZE_TFT - 1) >> 8) & 0xFF);  // X End High
    WriteRegister(0x5C); WriteData((LCD_YSIZE_TFT - 1) & 0xFF);  // Y End Low
    WriteRegister(0x5D); WriteData(((LCD_YSIZE_TFT - 1) >> 8) & 0xFF);  // Y End High
}

//**************************************************************************************************
// Subs to run and sent to the LT7680 - Translated from Levetop sample info

uint16_t lt_cclk_mhz = 0;               // core clock set by LT7680_PLL_Initial_LT() (backlight PWM timing)
uint32_t lt_pclk_khz = 0;               // pixel clock set by LT7680_PLL_Initial_LT()

static uint8_t pll_written[6];          // REG[05h-0Ah] as last written, for the read-back check

static void PllWrite_LT(uint8_t reg, uint8_t v) {
    pll_written[reg - 0x05] = v;
    WriteRegister(reg);
    WriteData(v);
}

// Switch to the new PLL settings with REG[00h] bit 7 and poll that bit until it reads 1 (the chip is running on the
// PLLs), then read REG[05h-0Ah] back: no LT7680 on the bus, or a dead SPI read path, shows up here. (Read before the
// switch they still return the settings in use, the reset values at boot, so the check has to come after it.) Returns to
// the normal SPI rate and returns LT_FAULT_* bits.
static uint8_t PLL_Switch_LT(void) {
    uint8_t fault = 0;
    uint32_t t0;
    LT_SpiSlow(1);
    WriteRegister(0x00);                // reconfigure the PLLs
    WriteData(0x80);
    t0 = HAL_GetTick();
    lt_pll_polls = 0;
    for (;;) {
        if (lt_pll_polls < 0xFFFF) lt_pll_polls++;
        WriteRegister(0x00);
        if (ReadData() & 0x80) break;
        if ((HAL_GetTick() - t0) >= LT_PLL_TIMEOUT_MS) { fault |= LT_FAULT_PLL; break; }
    }
    INIT_GAP(10);                       // Ian's settle time (legacy init only)
    LT_SpiSlow(0);
    for (uint8_t i = 0; i < 6; i++) {
        WriteRegister(0x05 + i);
        if (ReadData() != pll_written[i]) fault |= LT_FAULT_NOREPLY;
    }
    return fault;
}

#if !LT_CLOCKS_LEGACY
// Program one PLL (REG[c1] = OD/R/N[8], REG[c1 + 1] = N[7:0]) as close as possible to target_khz, keeping the VCO
// (XI * N / R) inside 100-500 MHz (datasheet V4.1, PLL electrical characteristics). R = 5 gives a 2 MHz loop input;
// the output divider is /4 (as in the datasheet's examples), which puts every output from 25 to 125 MHz well inside
// the VCO range (e.g. PCLK 29 MHz -> VCO 116 MHz, 100 MHz -> 400 MHz); /2 or /1 only above that. Returns the real output in kHz.
static uint32_t SetPLL_LT(uint8_t c1, uint32_t target_khz) {
    static const uint8_t od_code[3] = { 3, 1, 0 }, od_div[3] = { 4, 2, 1 };
    const uint32_t r = 5, in_khz = (LT_XI_MHZ * 1000u) / r;
    uint8_t k = 0;
    uint32_t n;
    while (k < 2 && target_khz * od_div[k] > 500000u) k++;
    n = (target_khz * od_div[k] + in_khz / 2) / in_khz;
    if (n < 2) n = 2;
    if (n > 511) n = 511;
    while (n > 2 && in_khz * n > 500000u) n--;
    PllWrite_LT(c1, (uint8_t)((od_code[k] << 6) | (r << 1) | ((n >> 8) & 1)));
    PllWrite_LT(c1 + 1, (uint8_t)(n & 0xFF));
    return (in_khz * n) / od_div[k];
}

// Register 0x05-0x0A, 0x00. Stage 2 of docs/REWORK-PLAN.md: PCLK from the panel timings, MCLK and CCLK fixed
// (LT_MCLK_MHZ, LT_CCLK_MHZ) instead of 2 x PCLK, every VCO in range. Datasheet rules: CCLK x 2 >= MCLK >= CCLK,
// CCLK >= 1.5 x PCLK; limits PCLK SCLK_MAX, MCLK 133, CCLK 100 MHz.
uint8_t LT7680_PLL_Initial_LT() {
    uint32_t pclk_khz = ((LCD_HBPD + LCD_HFPD + LCD_HSPW + LCD_XSIZE_TFT) *
        (LCD_VBPD + LCD_VFPD + LCD_VSPW + LCD_YSIZE_TFT) * REFRESH_RATE + 500u) / 1000u;
    uint32_t cclk = LT_CCLK_MHZ, mclk = LT_MCLK_MHZ;

    if (pclk_khz > SCLK_MAX * 1000u) pclk_khz = SCLK_MAX * 1000u;
    if (cclk > CCLK_MAX) cclk = CCLK_MAX;
    if (cclk * 2000u < pclk_khz * 3u) cclk = (pclk_khz * 3u + 1999u) / 2000u;   // CCLK >= 1.5 x PCLK
    if (mclk > 133u) mclk = 133u;
    if (mclk < cclk) mclk = cclk;                                              // MCLK >= CCLK
    if (mclk > cclk * 2u) mclk = cclk * 2u;                                    // MCLK <= 2 x CCLK

    lt_pclk_khz = SetPLL_LT(0x05, pclk_khz);
    lt_mclk_mhz = (uint16_t)(SetPLL_LT(0x07, mclk * 1000u) / 1000u);
    lt_cclk_mhz = (uint16_t)(SetPLL_LT(0x09, cclk * 1000u) / 1000u);
    return PLL_Switch_LT();
}

#else
// Ian's original clocks (build with -DLT_CLOCKS_LEGACY=1): MCLK = CCLK = 2 x PCLK, OD = /2 for all three, which leaves
// the PCLK VCO below its 100 MHz minimum.
// Register 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x00
uint8_t LT7680_PLL_Initial_LT() {
    // Parameters

    // Clock calculations
    unsigned int temp = (LCD_HBPD + LCD_HFPD + LCD_HSPW + LCD_XSIZE_TFT) *
        (LCD_VBPD + LCD_VFPD + LCD_VSPW + LCD_YSIZE_TFT) * REFRESH_RATE;              // = 38208000
  
    temp = (temp + 500000) / 1000000; // Round to the nearest MHz           1000000

    unsigned short SCLK = temp;
    unsigned short MCLK = temp * 2;
    unsigned short CCLK = temp * 2;

    // Limit frequencies to specified max values
    if (SCLK > SCLK_MAX) SCLK = SCLK_MAX;
    if (MCLK > MCLK_MAX) MCLK = MCLK_MAX;
    if (CCLK > CCLK_MAX) CCLK = CCLK_MAX;

    // PLL parameters
    unsigned short lpllOD_sclk = 2, lpllOD_cclk = 2, lpllOD_mclk = 2;
    unsigned short lpllR_sclk = 5, lpllR_cclk = 5, lpllR_mclk = 5;
    unsigned short lpllN_sclk = SCLK, lpllN_cclk = CCLK, lpllN_mclk = MCLK;

    // Configure PCLK PLL - TFT pixel clock (max=80MHz) (Registers 0x05 and 0x06)
    PllWrite_LT(0x05, (uint8_t)((lpllOD_sclk << 6) | (lpllR_sclk << 1) | ((lpllN_sclk >> 8) & 0x1)));      // 8A
    //WriteData(0x56);          // test
    
    PllWrite_LT(0x06, (uint8_t)(lpllN_sclk & 0xFF));                                                       // 1B
    //WriteData(0x10);        // test, fixes wierd colour on the "J" on "IanJ" text, but causes flicker

    // Configure MCLK PLL - Display memory clock (max=133MHz) (Registers 0x07 and 0x08)
    PllWrite_LT(0x07, (uint8_t)((lpllOD_mclk << 6) | (lpllR_mclk << 1) | ((lpllN_mclk >> 8) & 0x1)));      // 8A
    //WriteData(0x8A);          // test
    
    PllWrite_LT(0x08, (uint8_t)(lpllN_mclk & 0xFF));                // 36                                                       // 36
    //WriteData(0x56);          // test

    // Configure CCLK PLL - Core clock (max=100MHz) (Registers 0x09 and 0x0A)
    PllWrite_LT(0x09, (uint8_t)((lpllOD_cclk << 6) | (lpllR_cclk << 1) | ((lpllN_cclk >> 8) & 0x1)));      // 8A
    //WriteData(0x56);          // test
    
    PllWrite_LT(0x0A, (uint8_t)(lpllN_cclk & 0xFF));                                                       // 36
    //WriteData(0x56);          // test

    // Remember the clocks actually programmed: Fout = XI * N / (R * OD), OD code 2 = divide by 2
    lt_mclk_mhz = (uint16_t)((LT_XI_MHZ * lpllN_mclk) / (lpllR_mclk * 2u));
    lt_cclk_mhz = (uint16_t)((LT_XI_MHZ * lpllN_cclk) / (lpllR_cclk * 2u));
    lt_pclk_khz = (LT_XI_MHZ * 1000u * lpllN_sclk) / (lpllR_sclk * 2u);

    // Trigger PLL reconfiguration (Register 0x00) and wait for it
    return PLL_Switch_LT();
}
#endif // LT_CLOCKS_LEGACY


// Register 0x10, 0x11, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35
void Configure_Main_PIP_Window_LT() {
    uint8_t regValue = 0;

    // Hardcoded configuration:
    regValue |= (0 << 7);  // Bit 7: Disable PIP-1 Window
    regValue |= (0 << 6);  // Bit 6: Disable PIP-2 Window
    regValue |= (0 << 4);  // Bit 4: Configure PIP-1 parameters - Don't need to set this
    regValue |= (0b01 << 2);  // Bits 3-2: 16bpp Generic TFT (65K color) - Ties up with ST7701S setting
    regValue |= (0 << 0);  // Bit 0: Sync Mode (VSYNC, HSYNC, DE enabled)

    // Write the configuration to REG[10h]
    WriteRegister(0x10);
    WriteData(regValue);

    // PIP Window Upper-Left Corner (0,0)
    WriteRegister(0x2A); WriteData(0x00);  // Upper-left X coord Low Byte
    WriteRegister(0x2B); WriteData(0x00);  // Upper-left X coord High Byte
    WriteRegister(0x2C); WriteData(0x00);  // Upper-left Y coord Low Byte
    WriteRegister(0x2D); WriteData(0x00);  // Upper-left Y coord High Byte

    // PIP Window Bottom-Right Corner (100,100)
    WriteRegister(0x2E); WriteData((100 & 0xFC));     // Lower-right X coord Low Byte (ensure bit[1:0] = 0)
    WriteRegister(0x2F); WriteData((100 >> 8) & 0x1F); // Lower-right X coord High Byte (bits [12:8])
    WriteRegister(0x30); WriteData(100 & 0xFF);       // Lower-right Y coord Low Byte
    WriteRegister(0x31); WriteData((100 >> 8) & 0x1F); // Lower-right Y coord High Byte (bits [12:8])

    // Set PIP Image Start Address to 0x000000 (example, ensure data exists here in SDRAM)
    WriteRegister(0x32); WriteData(0x00);  // Address bits [7:0]
    WriteRegister(0x33); WriteData(0x00);  // Address bits [15:8]
    WriteRegister(0x34); WriteData(0x00);  // Address bits [23:16]
    WriteRegister(0x35); WriteData(0x00);  // Address bits [31:24]

    // Set PIP-1 and PIP-2 color depth to 16bpp (Bits 3-2 and 1-0 set to 01)
    uint8_t regValue2 = 0;
    regValue2 |= (0b01 << 2);  // PIP-1 Color Depth: 16bpp
    regValue2 |= (0b01 << 0);  // PIP-2 Color Depth: 16bpp
    // Write to Register 0x11
    WriteRegister(0x11);
    WriteData(regValue2);

}



uint16_t lt_mclk_mhz;                   // memory clock set by LT7680_PLL_Initial_LT(), used for the SDRAM refresh
volatile uint16_t lt_sdram_init_timeouts;   // SDRAM init waits that ran out (read over SWD)

#if !SDRAM_INIT_LEGACY
// Auto-refresh interval in memory clocks: one row every SDRAM_REFRESH_NS (64 ms / 4,096 rows = 15.625 us, less a margin).
// REG[E2h-E3h] = 0 would disable refresh, so never return 0.
static uint16_t SDRAM_RefreshInterval(uint16_t mclk_mhz) {
    uint32_t itv;
    if (mclk_mhz == 0) mclk_mhz = MCLK_MAX;             // unknown clock: assume the fastest, which refreshes most often
    itv = ((uint32_t)mclk_mhz * SDRAM_REFRESH_NS) / 1000u;
    if (itv > 0xFFFF) itv = 0xFFFF;
    if (itv < 1) itv = 1;
    return (uint16_t)itv;
}

// Write the refresh interval for the current memory clock. Normal register bank (REG[E4h] bit 2 = 0) must be selected,
// which it always is after SDRAM_Init_LT(). Call again whenever the PLL is reprogrammed.
void SDRAM_SetRefresh_LT(void) {
    uint16_t itv = SDRAM_RefreshInterval(lt_mclk_mhz);
    WriteRegister(0xE2); WriteData(itv & 0xFF);
    WriteRegister(0xE3); WriteData(itv >> 8);
}

// Wait for SDRAM initialisation to finish: REG[E4h] bit 0 reads 1 (what Ian's code polled) and the status register's
// SDRAM-ready bit 2 is set (what the Levetop library polls). Gives up after SDRAM_INIT_TIMEOUT_MS and counts it.
static void SDRAM_WaitReady_LT(void) {
    uint32_t t0 = HAL_GetTick();
    uint8_t e4 = 0, stsr = 0;
#if LT_REGDUMP
    uint16_t polls = 0;
#endif
    for (;;) {
        WriteRegister(0xE4);
        e4 = ReadData();
        stsr = ReadStatus();
#if LT_REGDUMP
        if (polls < 0xFFFF) polls++;
#endif
        if ((e4 & 0x01) && (stsr & 0x04)) break;
        if ((HAL_GetTick() - t0) >= SDRAM_INIT_TIMEOUT_MS) { lt_sdram_init_timeouts++; break; }
    }
#if LT_REGDUMP
    lt_regdump[LTRD_INIT_POLLS_LO] = polls & 0xFF;
    lt_regdump[LTRD_INIT_POLLS_HI] = polls >> 8;
    lt_regdump[LTRD_INIT_E4] = e4;
    lt_regdump[LTRD_INIT_STSR] = stsr;
#endif
}

// SDRAM set-up for the LT7680A-R (datasheet V4.1, REG[E0h-E4h]). Stage 1 of docs/REWORK-PLAN.md.
// The attribute, mode and refresh registers are written with REG[E4h] bit 2 clear, so they land in the normal bank;
// the timing-parameter bank (bit 2 set) is left at its reset values. Then initialisation is started with bit 0.
void SDRAM_Init_LT(void) {
    WriteRegister(0xE4); WriteData(0x00);   // normal register bank, initialisation not started
    WriteRegister(0xE0); WriteData(SDRAM_ATTR);   // 4 banks, 4,096 rows, 512 columns
    WriteRegister(0xE1); WriteData(0x03);   // CAS latency 3
    SDRAM_SetRefresh_LT();                  // REG[E2h-E3h] from the real memory clock
    WriteRegister(0xE4); WriteData(0x01);   // start initialisation
    SDRAM_WaitReady_LT();
    HAL_Delay(10);                          // settle, as the original sequence did
}

#else
// Ian's original sequence (build with -DSDRAM_INIT_LEGACY=1 for A/B comparison). It sets REG[E4h] bit 2 first, so the
// E0h-E3h writes below go to the timing-parameter bank and the refresh interval is left at 0 (refresh disabled).
void SDRAM_SetRefresh_LT(void) { }

// Register 0xE0, 0xE1, 0xE2, 0xE3, 0xE4
void SDRAM_Init_LT() {

    unsigned short sdram_itv;
    uint8_t regValue1 = 0;

    // Step 1: Enable SDRAM Timing Parameter Registers (Bit 2 = 1)
    WriteRegister(0xE4);        // Allow addresses 0xE0 to 0xE3 to be set
    regValue1 |= (1 << 2);      // Set Bit 2
    WriteData(regValue1);

    // Step 2: Configure SDRAM settings
    WriteRegister(0xE0);        // Register 0xE0: SDRAM Control Register
    WriteData(0x29);            // Default SDRAM control value specifically for LT7680A-R      LT7680A = 64MB, LT&^*)A-R 128MB I think!   set to 0x21 or 0x29

    WriteRegister(0xE1);        // Register 0xE1: SDRAM CAS Latency
    WriteData(0x03);            // Set CAS latency to 0x03 as suggested by manual

    // Calculate SDRAM refresh interval
    sdram_itv = (SDRAM_CLKFREQ / SDRAM_SIZE) / (1000 / SDRAM_MCLK); // Based on MCLK
    sdram_itv -= 2;

    // Write SDRAM interval (lower byte)
    WriteRegister(0xE2);        // Register 0xE2: SDRAM Refresh Interval Low Byte
    WriteData(sdram_itv);                                               // sdram_itv & 0xFF
    //WriteData(0x1A);            // Set to 0x06 as reference setting by manual for LT7680A-R

    // Write SDRAM interval (upper byte)
    WriteRegister(0xE3);        // Register 0xE3: SDRAM Refresh Interval High Byte
    WriteData(sdram_itv >> 8);                                 // sdram_itv >> 8) & 0xFF
    //WriteData(0x06);            // Set to 0x06 as reference setting by manual for LT7680A-R

    // Step 3: Trigger SDRAM Initialization (Set Bit 0 = 1)
    WriteRegister(0xE4);
    //regValue1 |= (1 << 0);      // Set Bit 0 (SDR_INITDONE)
    //WriteData(regValue1);
    WriteData(0x01);

    // Step 4: Disable Timing Parameter Registers (Clear Bit 2)
    WriteRegister(0xE4);        // Disable, addresses 0xE0 to 0xE3 can no longer be set
    regValue1 &= ~(1 << 2);     // Clear Bit 2
    //regValue1 |= (0 << 2);
    WriteData(regValue1);

    HAL_Delay(1); // 1 ms delay

    Check_SDRAM_Ready_LT();     // Call sub to wait for SDRAM initialization to complete

}


// Check if the SDRAM is ready for use - Address = 0xE4
void Check_SDRAM_Ready_LT() {
    
    unsigned char status;
    
    // Poll the SDRAM Ready Flag (Bit 0) in Register 0xE4
#if LT_REGDUMP
    uint16_t polls = 0;
#endif
    do {
        WriteRegister(0xE4);
        status = ReadData(); // Read the SDRAM status register
#if LT_REGDUMP
        if (polls < 0xFFFF) polls++;
#endif
    } while ((status & 0x01) == 0);  // Wait until the Ready bit (bit 0) is set
#if LT_REGDUMP
    lt_regdump[LTRD_INIT_POLLS_LO] = polls & 0xFF;
    lt_regdump[LTRD_INIT_POLLS_HI] = polls >> 8;
    lt_regdump[LTRD_INIT_E4] = status;
    lt_regdump[LTRD_INIT_STSR] = ReadStatus();
#endif

    // Optional delay to ensure SDRAM is stable after initialization
    HAL_Delay(10);  // 10 ms delay

}
#endif // SDRAM_INIT_LEGACY


// Register 0x01, 0x02, 0x03, 0x012, 0x13
void Set_LCD_Panel_LT() {
    uint8_t temp = 0;

    // Configure Register 0x01: TFT Panel I/F and Host Bus Width
    temp |= (TFT_BIT << 3);         // Set Bit 4-3 to 01b (18-bit TFT Panel I/F)
    temp |= (HOST_BUS << 0);        // Set Bit 0 to 0 (8-bit Host Bus)
    temp |= (0 << 1);               // Set Bit 1 to 0 (Disable SPI Master)
    temp |= (0 << 2);               // Set Bit 2 to 0 (Disable I2C Master)
    temp |= (0 << 5);               // Set Bit 5 to 0 (Disable Keypad-scan)
    temp |= (1 << 6);               // Set Bit 6 to 1 (Mask, WAIT# de-assert when CS# de-assert.)
    WriteRegister(0x01);
    WriteData(temp);

    // Configure Register 0x02: Host Read/Write Image Data Format
    temp = 0;                       // Reset temp for next register
    temp |= (0b00 << 6);            // Set Bit 7-6 to 00b (Direct Write using SPI)
    temp |= (0b00 << 4);            // Set Bit 5-4 to 00b (Read: Left to Right, Top to Bottom)
    temp |= (0b00 << 1);            // Set Bit 2-1 to 00b (Write: Left to Right, Top to Bottom)
    WriteRegister(0x02);
    WriteData(temp);

    // Configure Register 0x03: Graphic Mode and Memory Selection
    temp = 0;                       // Reset temp for next register
    temp |= (0 << 2);               // Set Bit 2 to 1 (Text Mode)
    temp |= (0 << 1);               // Set Bit 1 to 0
    temp |= (0 << 0);               // Set Bit 0 to 0 (Select SDRAM)
    WriteRegister(0x03);
    WriteData(temp);

    // Configure Display Parameters in Register 0x12
    temp = 0;                       // Reset temp for next register
    temp |= (PCLK_EDGE << 7);       // Set Bit 7 to 1 (PCLK Rising Edge)
    temp |= (VSCAN_DIRECTION << 3); // Set Bit 3 to 0 (VSCAN Top to Bottom)
    temp |= PD_OUTPUT_SEQ;          // Set Bits 2-0 to 000 (PDATA RGB Mode)
    //temp |= (0b000);
    WriteRegister(0x12);
    WriteData(temp);

    // Configure Display Parameters in Register 0x13
    temp = 0;                       // Reset temp for next register
    temp |= (HSYNC_ACTIVE << 7);    // Set Bit 7 to 0 (HSYNC Low Active)
    temp |= (VSYNC_ACTIVE << 6);    // Set Bit 6 to 0 (VSYNC Low Active)
    temp |= (DE_ACTIVE << 5);       // Set Bit 5 to 0 (DE High Active)
    temp |= (PDE_IDLE_STATE << 4);  // Bit 4
    temp |= (PCLK_IDLE_STATE << 3); // Bit 3
    temp |= (PD_IDLE_STATE << 2);   // Bit 2
    temp |= (HSYNC_IDLE_STATE << 1);// Bit 1
    temp |= (VSYNC_IDLE_STATE << 0);// Bit 0
    WriteRegister(0x13);
    WriteData(temp);

}


// Register 0x14, 0x15, 0x1A, 0x1B
void LCD_HorizontalWidth_VerticalHeight_LT(uint16_t WX, uint16_t HY) {             // 320x960 pixels

    uint16_t tempWidth = (WX / 8) - 1;      // Horizontal Display Width (pixels) = (HDWR + 1) * 8 + HDWFTR
    uint16_t tempWidthFineTune = 0;

    // Horizontal Width
    WriteRegister(0x14);
    WriteData(tempWidth);
    WriteRegister(0x15);
    WriteData(tempWidthFineTune & 0x0F);    // Bits 0-3 only

    // Vertical Height
    // Ensure height is within valid range
    if (HY > 1024) {
        HY = 1024; // Maximum valid height
    }
    if (HY < 1) {
        HY = 1; // Minimum valid height
    }
    uint16_t vdhr = HY - 1;             // Subtract 1 from the height as per the formula: VDHR = Vertical Display Height - 1
    // Write the lower 8 bits of VDHR to Register 0x1A
    WriteRegister(0x1A);
    WriteData(vdhr & 0xFF); // Lower 8 bits of VDHR
    // Write the upper 3 bits of VDHR to Register 0x1B
    WriteRegister(0x1B);
    WriteData(vdhr >> 8); // Upper 3 bits of VDHR (bits 10-8)

}


// Register 0x16, 0x17
void LCD_Horizontal_Non_Display_LT(uint16_t val) {

    uint16_t tempHBPD = (val / 8) - 1;      // Horizontal Display Width (pixels) = (HDWR + 1) * 8 + HDWFTR
    uint16_t tempHBPDFineTune = 0;

    // Horizontal HBPD
    WriteRegister(0x16);
    WriteData(tempHBPD);
    WriteRegister(0x17);
    WriteData(tempHBPDFineTune & 0x0F);    // Bits 0-3 only

}


// Register 0x18
void LCD_HSYNC_Start_Position_LT(uint16_t val) {

    uint16_t tempHFPD;

    if (val < 8) {
        tempHFPD = 0;  // Minimum valid value
    }
    else {
        tempHFPD = (val / 8) - 1;  // HSYNC Start Position = (HSTR + 1)* 8
    }

    // Write the calculated value to the register
    WriteRegister(0x18);
    WriteData(tempHFPD);

}


// Register 0x19
void LCD_HSYNC_Pulse_Width_LT(uint16_t val) {

    uint16_t tempHSPW;

    if (val < 8) {
        tempHSPW = 0;  // Minimum valid value
    }
    else {
        tempHSPW = (val / 8) - 1;  // HSYNC Start Position = (HSTR + 1)* 8
    }

    // Write the calculated value to the register
    WriteRegister(0x19);
    WriteData(tempHSPW);

}


// Register 0x1C, 0x1D
void LCD_Vertical_Non_Display_LT(uint16_t val) {

    uint8_t temp = val - 1;
    WriteRegister(0x1C);
    WriteData(temp & 0xFF);
    WriteRegister(0x1D);
    WriteData((temp >> 8) & 0xFF);

}


// Register 0x1E
void LCD_VSYNC_Start_Position_LT(uint16_t val) {

    uint8_t temp = val - 1;
    WriteRegister(0x1E);
    WriteData(temp & 0xFF);

}


// Register 0x1F
void LCD_VSYNC_Pulse_Width_LT(uint16_t val) {

    uint8_t temp = val - 1;
    WriteRegister(0x1F);
    WriteData(temp & 0xFF);

}


// Register 0x12
void LCDConfigTurnOn_LT() {
    uint8_t regValue = 0;

    // Bit 7: PCLK Inversion
    // 0: TFT Panel fetches PD at PCLK rising edge
    // 1: TFT Panel fetches PD at PCLK falling edge
    regValue |= (0 << 7); // Set to 0 (default to rising edge, adjust if necessary)

    // Bit 6: Display ON/OFF
    regValue |= (1 << 6); // Set to 1 (Display On)

    // Bit 5: Display Test Color Bar
    regValue |= (DISP_TEST << 5); // Set to 1 (Enable Color Bar for testing, adjust if needed)

    // Bit 4: Reserved
    regValue |= (0 << 4); // Set to 0 as per specification

    // Bit 3: VDIR - Vertical Scan Direction
    // 0: From Top to Bottom
    // 1: From Bottom to Top
    regValue |= (1 << 3); // Default to 0 (Top to Bottom, adjust if necessary)

    // Bits 2-0: Parallel PD[23:0] Output Sequence
    // 000: RGB, 001: RBG, 010: GRB, 011: GBR, 100: BRG, 101: BGR, 110: Gray, 111: Idle State
    regValue |= (OUTPUT_SEQ); // Set to RGB (default, adjust if necessary)

    // Write the value to Register 0x12
    WriteRegister(0x12);
    WriteData(regValue);
}


// Register 0x12
void LCDConfigTurnOff_LT() {
    uint8_t regValue = 0;

    // Bit 6: Display ON/OFF
    regValue &= ~(1 << 6); // Clear Bit 6 (Set to 0 for Display Off)

    // Write the value to Register 0x12
    WriteRegister(0x12);
    WriteData(regValue);
}


void ConfigurePWMAndSetBrightness(uint8_t brightnessPercentage) {

    // Configure Timer - 1 and PWM - 1 for backlighting.
    // Settable 0 - 100 %

    if (brightnessPercentage > 100) brightnessPercentage = 100; // Cap brightness to 100%

    // Step 1: Set the Prescaler (REG[84h])
    // Core Frequency: 54MHz -> Base Frequency = Core_Freq / (Prescaler + 1)
    // Example: Prescaler = 16 -> Base Frequency = 54MHz / (16 + 1) = ~3.18MHz
    // Prescaler from the real core clock so the PWM stays near LT_BL_PWM_HZ (Ian's 0x10 at 58 MHz): CCLK / (P + 1) / 4 / 256
    uint32_t presc = 0x10;
    if (lt_cclk_mhz) {
        presc = (lt_cclk_mhz * 1000000u) / (LT_BL_PWM_HZ * 4u * 256u);
        presc = presc ? presc - 1u : 0u;
        if (presc > 0xFF) presc = 0xFF;
    }
    WriteRegister(0x84);
    WriteData((uint8_t)presc);

    // Step 2: Configure PWM Clock Mux Register (REG[85h])
    // Timer-1 divisor = 1/4, PWM[1] = Timer-1 events
    WriteRegister(0x85);
    WriteData((2 << 6) | // Timer-1 divisor = 1/4
        (2 << 2)); // PWM[1] output Timer-1 events

    // Step 3: Calculate ON and OFF times for Timer-1
    // Count Buffer = Total period, Compare Buffer = ON time
    uint16_t compareValue = (brightnessPercentage * 255) / 100; // ON time
    uint16_t countValue = 255; // Fixed total period

    // Step 4: Set Compare Buffer for Timer-1 (REG[8Ch-8Dh])
    WriteRegister(0x8C); // Timer-1 Compare Buffer (low byte)
    WriteData(compareValue & 0xFF);
    WriteRegister(0x8D); // Timer-1 Compare Buffer (high byte)
    WriteData((compareValue >> 8) & 0xFF);

    // Step 5: Set Count Buffer for Timer-1 (REG[8Eh-8Fh])
    WriteRegister(0x8E); // Timer-1 Count Buffer (low byte)
    WriteData(countValue & 0xFF);
    WriteRegister(0x8F); // Timer-1 Count Buffer (high byte)
    WriteData((countValue >> 8) & 0xFF);

    // Step 6: Enable Timer-1 (REG[86h])
    // Auto-reload enabled, Timer-1 started, no inversion
    WriteRegister(0x86);
    WriteData((1 << 5) | (1 << 4)); // Auto-reload and Start
}


// Register 0x00
void Software_Reset_LT() {                                      // OK - needs verified
    uint8_t regValue = 0;

    // Bit 7: Reconfigure PLL Frequency
    // Set this to 1 to reconfigure the PLL frequency
    // 1. When user change PLL relative parameters, PLL clock won‟t 
    // change immediately, user must set this bit as “1” again. 
    // 2. User may read (check) this bit to know whether system already 
    // switch to PLL clock or not yet. 
    regValue |= (0 << 7);

    // Bit 6-1: Reserved, keep as 0
    //regValue |= (0 << 1);

    // Bit 0: Software Reset (Write-only)
    // Set this to 1 to perform a software reset
    regValue |= (1 << 0);

    // Write the configured value to Register 0x00
    WriteDataToRegister(0x00, regValue);
    LT_BusForget();                     // the reset clears the registers the bus has cached

    // Optional: Add a delay to allow reset/reconfiguration to complete
    //HAL_Delay(10);

    // Optional: Clear Bit 7 and Bit 0 (reset finished)
    //regValue &= ~(1 << 7); // Clear Bit 7
    //regValue &= ~(1 << 0); // Clear Bit 0
    //WriteDataToRegister(0x00, regValue);

    /*
    // Set porch settings via Pcb links
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0) == GPIO_PIN_SET) {
        // B0 high
        #define LCD_VFPD = 14;
    } else {
        #define LCD_VFPD = 15;
    }
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_SET) {
        // B1 high
    } else {

    }
    */

}


// Registers 0x20 to 0x23
void Set_MISA_LT() {
    
    // Hardcoded Main Image Start Address = 0x00000000
    WriteRegister(0x20); WriteData(0x00);  // MISA[7:0], ensure bit[1:0] = 0
    WriteRegister(0x21); WriteData(0x00);  // MISA[15:8]
    WriteRegister(0x22); WriteData(0x00);  // MISA[23:16]
    WriteRegister(0x23); WriteData(0x00);  // MISA[31:24]
     
}


// Register 0x24, 0x25
void SetMainImageWidth_LT() {

    // Set to 
    WriteRegister(0x24);
    WriteData(LCD_XSIZE_TFT & 0xFF);
    WriteRegister(0x25);
    WriteData((LCD_XSIZE_TFT >> 8) & 0x1F);  // Mask to 5 bits

}


// Register 0x26, 0x27
void SetMainWindowUpperLeftX_LT() {
    uint16_t xCoord = 0;  // Upper-left corner X-coordinate (0 for the left edge of the display)
    uint8_t lowByte, highByte;

    // Split the X-coordinate into low and high bytes
    lowByte = xCoord & 0xFC;         // Mask out bit[1:0] to ensure they are 0
    highByte = (xCoord >> 8) & 0x1F; // Only use bits[12:8] and ignore bits[7:5]

    // Write to MWULX registers
    WriteRegister(0x26);
    WriteData(lowByte);            // MWULX[7:0]
    WriteRegister(0x27);
    WriteData(highByte);           // MWULX[12:8]

}


// Registers 0x28, 0x29
void SetActiveWindow_LT() {
    // Main Window Horizontal Start = 0, End = 319
    WriteRegister(0x28); WriteData(0x00);  // Start X Low Byte
    WriteRegister(0x29); WriteData(0x00);  // Start X High Byte

}


// Register 0x5E
void SetColorDepth_LT() {
    uint8_t value = 0;

    // Hardcoded settings for AW_COLOR (Register 0x5E):
    // Bit 7-4: Not used (set to 0).
    // Bit 3: 0 (Read back Graphic Write position).
    // Bit 2: 0 (Block mode - X-Y coordinate addressing).
    // Bits 1-0: 01 (16bpp RGB565).

    value |= (0 << 3);  // Bit 3: Graphic Write position
    value |= (0 << 2);  // Bit 2: Block mode
    value |= (0 << 1);  // Bit 1: 16bpp
    value |= (1 << 0);  // Bit 0: RGB565 (16bpp)

    // Write the value to the AW_COLOR register
    WriteRegister(0x5E);
    WriteData(value);
}


void ResetGraphicWritePosition_LT() {
    // Set Graphic Write X-Coordinate to 0 (lower 8 bits)
    WriteRegister(0x5F);
    WriteData(0x00);

    // Set Graphic Write X-Coordinate to 0 (upper 5 bits)
    WriteRegister(0x60);
    WriteData(0x00);
}


void SetGraphicRWYCoordinate_LT() {
    uint16_t y = 0;  // Hardcoded to 0 for the initial Y-coordinate

    // Write to REG[61h] (Lower byte of Y-coordinate)
    WriteRegister(0x61);
    WriteData(y & 0xFF);  // Lower 8 bits of the Y-coordinate

    // Write to REG[62h] (Upper byte of Y-coordinate)
    WriteRegister(0x62);
    WriteData((y >> 8) & 0x1F);  // Bits [12:8], masked to 5 bits
}


void SetCanvasStartAddress_LT() {
    uint32_t startAddress = 0x00000000;  // Hardcoded to 0 (start of SDRAM)

    WriteRegister(0x50);
    WriteData(startAddress & 0xFF);         // Lower byte (CVSSA[7:0])

    WriteRegister(0x51);
    WriteData((startAddress >> 8) & 0xFF);  // Middle byte (CVSSA[15:8])

    WriteRegister(0x52);
    WriteData((startAddress >> 16) & 0xFF); // Upper byte (CVSSA[23:16])

    WriteRegister(0x53);
    WriteData((startAddress >> 24) & 0xFF); // Highest byte (CVSSA[31:24])

}


void SetCanvasImageWidth_LT() {
    WriteRegister(0x54);
    WriteData(LCD_XSIZE_TFT & 0xFF);          // Lower byte (CVS_IMWTH[7:0])

    WriteRegister(0x55);
    WriteData((LCD_XSIZE_TFT >> 8) & 0x3F);   // Upper byte (CVS_IMWTH[13:8]), masked to 6 bits
}


