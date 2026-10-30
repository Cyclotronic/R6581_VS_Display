/**
  ******************************************************************************
  * @file    lt_bus.c
  * LT7680 bus: register address and data writes, data and status reads over SPI1, the register cache,
  * the busy wait, and the bus bench tests (Stage 5 of docs/REWORK-PLAN.md).
  *
  * Commands:
  * WriteRegister = Specify address, and typicall follow up with WriteData (1 or more times) to write data to a register.
  * uint8_t ReadDataFromRegister(reg_address) = write to register and it will return the data within.
  * uint8_t status = ReadData() = Call after a WriteRegister and it will return data at register address.
  * WriteDataToRegister(uint8_t reg, uint8_t value) = In one go spedify address and data to send.
  *
  * Status Registers can read data through the state reading period, and it can only be 
  * read and cannot be written. Instruction Registers can control most of the functions through the 
  * "Command Write" cycle and the "Data Write" cycle. "Command Write" specifies the address of the 
  * register, and then the "Data Write" period can be written to the specified register. And when the specified 
  * register data is to be read, the master will need to send the "Command write" cycle first. Then use the 
  * "Data Read" cycle to read the data. In other words, "Command Write" is the set register address, "Data 
  * Read" is to read register data. 
  *
  * ER-TFT4.58-1 TFT LCD, 320x960 pixels
  * LCD Controller = ST7701S
  * Response time = 25ms
  * Type = IPS
  * Colours = 262K
  *
  * Controller = LT7680A-R
  ******************************************************************************
*/

#include "lt7680.h"
#include "main.h"
#include "spi.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

//**************************************************************************************************
// Core commands
//
// Stage 5 of docs/REWORK-PLAN.md: the bus talks to SPI1 and the CS pin directly (HAL costs several microseconds per
// byte), and keeps two kinds of state so it can skip frames that change nothing:
//  - the register address last sent: WriteRegister() only records the register, and the address frame goes out with
//    the next data write or read, and only if it differs from the last one sent (DrawText's 0x04 per character, say);
//  - a shadow of the registers that only the firmware changes (03h, CCh, CDh, D0h-D7h): a data write that matches the
//    shadow is skipped.
// A reset forgets both. LT_BUS_HAL=1 restores Ian's HAL functions for A/B comparison.

volatile uint8_t LT7680_SPI_Read_ok = 0;   // the last status read looked like an LT7680 answering

#if !LT_BUS_HAL
#if LT_SPI_STATS
volatile uint32_t lt_frames = 0;        // CS frames sent
volatile uint32_t lt_frames_saved = 0;  // address and data frames skipped
#define BUS_COUNT(v) ((v)++)
#else
#define BUS_COUNT(v) ((void)0)
#endif

static int16_t lt_reg = -1;             // register selected by the caller (WriteRegister)
static int16_t lt_addr = -1;            // register address last sent to the chip (-1: unknown)
static uint8_t lt_shadow[12];           // 03h, CCh, CDh, D0h-D7h
static uint16_t lt_shadow_valid = 0;    // bit n: lt_shadow[n] holds what the chip has

#if LT_REG_CACHE
static int8_t ShadowSlot(int16_t reg) {
    if (reg == 0x03) return 0;
    if (reg == 0xCC || reg == 0xCD) return (int8_t)(reg - 0xCC + 1);
    if (reg >= 0xD0 && reg <= 0xD7) return (int8_t)(reg - 0xD0 + 3);
    return -1;
}
#endif

// Forget the bus state (after a reset of the LT7680, or a write behind the bus's back)
void LT_BusForget(void) {
    lt_addr = -1;
    lt_shadow_valid = 0;
}

static inline uint8_t LT_Xfer(uint8_t b) {
    while (!(SPI1->SR & SPI_SR_TXE)) { }
    *(__IO uint8_t*)&SPI1->DR = b;
    while (!(SPI1->SR & SPI_SR_RXNE)) { }
    return *(__IO uint8_t*)&SPI1->DR;
}

// One CS frame: control byte, then one byte out (and the byte that came back)
static uint8_t LT_Frame(uint8_t control, uint8_t b) {
    uint8_t r;
    if (!(SPI1->CR1 & SPI_CR1_SPE)) SPI1->CR1 |= SPI_CR1_SPE;
    (void)SPI1->DR; (void)SPI1->SR;     // drop a stale byte and any overrun flag
    SPI_CS_PORT->BSRR = (uint32_t)SPI_CS_PIN << 16;
    LT_Xfer(control);
    r = LT_Xfer(b);
    while (SPI1->SR & SPI_SR_BSY) { }
    SPI_CS_PORT->BSRR = SPI_CS_PIN;
    BUS_COUNT(lt_frames);
    return r;
}

static inline void LT_SendAddr(void) {
    if (lt_reg != lt_addr) {
        LT_Frame(0x00, (uint8_t)lt_reg);  // A0 = 0, RW = 0: register address
        lt_addr = lt_reg;
    }
    else BUS_COUNT(lt_frames_saved);
}

// Write Register Address (sent with the next data write or read)
void WriteRegister(uint8_t reg) {
    lt_reg = reg;
}

// Write Data
void WriteData(uint8_t data) {
#if LT_REG_CACHE
    int8_t k = ShadowSlot(lt_reg);
    if (k >= 0) {
        if ((lt_shadow_valid & (1u << k)) && lt_shadow[k] == data) { BUS_COUNT(lt_frames_saved); return; }
        lt_shadow[k] = data;
        lt_shadow_valid |= (uint16_t)(1u << k);
    }
#endif
    LT_SendAddr();
    LT_Frame(0x80, data);               // A0 = 1, RW = 0
}

// Read Status Register
uint8_t ReadStatus(void) {
    LT7680_SPI_Read_ok = 1;
    return LT_Frame(0x40, 0x00);        // A0 = 0, RW = 1
}

// Read Data from Register
uint8_t ReadData(void) {
    LT_SendAddr();
    return LT_Frame(0xC0, 0x00);        // A0 = 1, RW = 1
}

#else // LT_BUS_HAL
void LT_BusForget(void) { }

// Write Register Address
void WriteRegister(uint8_t reg) {
    uint8_t controlByte = 0x00; // A0 = 0, RW = 0
    HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_RESET); // CS Low
    HAL_SPI_Transmit(&hspi1, &controlByte, 1, HAL_MAX_DELAY);                 // Send control byte
    HAL_SPI_Transmit(&hspi1, &reg, 1, HAL_MAX_DELAY);                         // Send register address
    HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_SET);   // CS High
}
// Write Data
void WriteData(uint8_t data) {
    uint8_t controlByte = 0x80; // A0 = 1, RW = 0
    HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_RESET); // CS Low
    HAL_SPI_Transmit(&hspi1, &controlByte, 1, HAL_MAX_DELAY);                 // Send control byte
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);                        // Send data byte
    HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_SET);   // CS High
}

// Read Status Register
uint8_t ReadStatus(void) {
    uint8_t controlByte = 0x40; // A0 = 0, RW = 1
    uint8_t status = 0x00;      // Variable to hold the status byte
    HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_RESET); // CS Low

    // Attempt SPI transmit and receive
    if (HAL_SPI_Transmit(&hspi1, &controlByte, 1, HAL_MAX_DELAY) == HAL_OK &&
        HAL_SPI_Receive(&hspi1, &status, 1, HAL_MAX_DELAY) == HAL_OK) {
        LT7680_SPI_Read_ok = 1; // SPI read was successful
    }
    else {
        LT7680_SPI_Read_ok = 0; // SPI read failed
    }

    HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_SET);   // CS High
    return status;
}

// Read Data from Register
uint8_t ReadData(void) {
    uint8_t controlByte = 0xC0; // A0 = 1, RW = 1
    uint8_t data = 0x00; // Variable to hold the data byte
    HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_RESET); // CS Low
    HAL_SPI_Transmit(&hspi1, &controlByte, 1, HAL_MAX_DELAY);                 // Send control byte
    HAL_SPI_Receive(&hspi1, &data, 1, HAL_MAX_DELAY);                         // Read data byte
    HAL_GPIO_WritePin(SPI_CS_PORT, SPI_CS_PIN, GPIO_PIN_SET);   // CS High
    return data;
}

#endif // LT_BUS_HAL

// Write Register Address and Data (combined) - optional
void WriteDataToRegister(uint8_t reg, uint8_t value) {
    WriteRegister(reg); // Write the register address
    WriteData(value);   // Write the data to the register
}


// DrawText helper
uint8_t WaitForLT7680Ready(void)
{
	uint32_t timeout = 100000;
	uint8_t status;

	do {
		status = ReadStatus(); // STSR
	} while ((status & (1 << 3)) && --timeout);
	if (timeout == 0) {
		if (lt_busy_timeouts < 0xFFFF) lt_busy_timeouts++;
		return 0;
	}
	return 1;
}

// ---- Bus test (Stage 5 of docs/REWORK-PLAN.md); build with -DLT_BUS_TEST=1, read the results over SWD ------------
#if LT_BUS_TEST && !LT_BUS_HAL
volatile uint32_t lt_bus_tests = 0;     // register write/read-back pairs done
volatile uint32_t lt_bus_errs = 0;      // read-backs that differed
volatile uint8_t lt_bus_first_bad[4];   // register, written, read, 0xEE once set
volatile uint16_t lt_burst_errs = 0xFFFF;   // bytes wrong after a 256-byte burst in one CS frame (0xFFFF: not run)
volatile uint16_t lt_single_errs = 0xFFFF;  // bytes wrong after the same pattern written one byte per frame
volatile uint8_t lt_burst_peek[8];

#define BURST_ADDR 0x00380000UL         // unused SDRAM, away from the soak regions and the glyph store

static void BurstBegin(void) {
    Graphics_Mode();
    SetCanvasStart32_LT(BURST_ADDR);
    ResetGraphicWritePosition_LT();
    SetGraphicRWYCoordinate_LT();
}

static uint16_t BurstVerify(uint8_t seed) {
    uint16_t bad = 0, i, t;
    uint8_t v;
    BurstBegin();
    WriteRegister(0x04);
    (void)ReadData();                   // dummy read
    for (i = 0; i < 256; i++) {
        for (t = 0; (ReadStatus() & 0x10) && t < 1000; t++) { }
        v = ReadData();
        if (i < sizeof(lt_burst_peek)) lt_burst_peek[i] = v;
        if (v != (uint8_t)(i * 7 + seed)) bad++;
    }
    return bad;
}

void LT_BusTest(void) {
    static const uint8_t regs[3] = { 0xD2, 0xD5, 0x63 };
    uint32_t n;
    uint16_t i;
    // 1. register write and read-back through the raw frames (no cache), 100,000 patterns over three registers
    for (n = 0; n < 100000u; n++) {
        uint8_t reg = regs[n % 3], w = (uint8_t)(n * 37u + (n >> 8));
        if (reg == 0x63) w &= 0xFF;     // cursor X low: all 8 bits
        LT_Frame(0x00, reg);
        LT_Frame(0x80, w);
        LT_Frame(0x00, reg);
        uint8_t r = LT_Frame(0xC0, 0x00);
        lt_bus_tests++;
        if (r != w) {
            if (lt_bus_errs == 0) { lt_bus_first_bad[0] = reg; lt_bus_first_bad[1] = w; lt_bus_first_bad[2] = r; lt_bus_first_bad[3] = 0xEE; }
            lt_bus_errs++;
        }
    }
    LT_BusForget();
    // 2. memory: 256 bytes one byte per frame, then read back
    BurstBegin();
    WriteRegister(0x04);
    for (i = 0; i < 256; i++) WriteData((uint8_t)(i * 7 + 1));
    WaitForLT7680Ready();
    lt_single_errs = BurstVerify(1);
    // 3. memory: a different pattern as one CS frame (0x80, then 256 data bytes), then read back
    BurstBegin();
    WriteRegister(0x04);
    (void)ReadStatus();
    lt_reg = 0x04; LT_SendAddr();
    if (!(SPI1->CR1 & SPI_CR1_SPE)) SPI1->CR1 |= SPI_CR1_SPE;
    SPI_CS_PORT->BSRR = (uint32_t)SPI_CS_PIN << 16;
    LT_Xfer(0x80);
    for (i = 0; i < 256; i++) LT_Xfer((uint8_t)(i * 7 + 2));
    while (SPI1->SR & SPI_SR_BSY) { }
    SPI_CS_PORT->BSRR = SPI_CS_PIN;
    WaitForLT7680Ready();
    lt_burst_errs = BurstVerify(2);
    // back to the visible page in text mode
    SetCanvasStart32_LT(0x00000000UL);
    ResetGraphicWritePosition_LT();
    SetGraphicRWYCoordinate_LT();
    Text_Mode();
    LT_BusForget();
}
#endif

// ---- Register-cache check (LT_REGDUMP builds): the shadow must match the chip ----------------------------------
#if LT_REGDUMP && !LT_BUS_HAL && LT_REG_CACHE
volatile uint32_t lt_cache_checks = 0;
volatile uint32_t lt_cache_mismatch = 0;
volatile uint8_t lt_cache_first_bad[3]; // register, shadow, chip

void LT_CacheCheck(void) {
    static const uint8_t regs[12] = { 0x03, 0xCC, 0xCD, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0 };
    int16_t keep = lt_reg;
    for (uint8_t k = 0; k < 11; k++) {
        if (!(lt_shadow_valid & (1u << k))) continue;
        lt_reg = regs[k];
        uint8_t v = ReadData();
        lt_cache_checks++;
        if (v != lt_shadow[k]) {
            if (lt_cache_mismatch == 0) { lt_cache_first_bad[0] = regs[k]; lt_cache_first_bad[1] = lt_shadow[k]; lt_cache_first_bad[2] = v; }
            lt_cache_mismatch++;
        }
    }
    lt_reg = keep;
}
#endif

