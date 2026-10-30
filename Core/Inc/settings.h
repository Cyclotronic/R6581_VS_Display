/**
  ******************************************************************************
  * @file    settings.h
  * @brief   Settings page in flash, TFT timing sets, boot counter, timing-adjust mode (settings.c)
  ******************************************************************************
*/

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include "main.h"

// EEProm emulation (Flash)
#define EEPROM_START_ADDRESS 0x0800FC00 // Last page for 64KB Flash
#define EEPROM_PAGE_SIZE     1024       // Page size in bytes

// Boot diagnostics (BluePill speed test + splash screen): shown for the first BOOT_DIAG_SHOWS boots only.
#ifndef BOOT_DIAG_SHOWS
#define BOOT_DIAG_SHOWS      0          // 0 = never (straight to the live display); 10 = Ian-era first 10 boots; 255 = every boot
#endif
#ifndef BOOT_SCREEN_MS
#define BOOT_SCREEN_MS       4000       // how long the boot screen stays up
#endif
extern uint8_t boot_diag_number;        // 1-based number of the boot being shown (0 = not shown)

extern _Bool timingModsOnBoot;          // DCV held at power-up: timing-adjust mode instead of the live display

HAL_StatusTypeDef EEPROM_WriteData(uint32_t address, uint32_t data);
uint32_t EEPROM_ReadData(uint32_t address);
HAL_StatusTypeDef EEPROM_Write4CharString(uint32_t address, const char* str);
void EEPROM_Read4CharString(uint32_t address, char* buffer);
HAL_StatusTypeDef EEPROM_ErasePage(uint32_t address);

void Settings_Load(void);               // TFT timing settings from flash into LCD_* (defaults written if invalid)
uint8_t BootCountRead(void);
void BootCountIncrement(uint8_t count);
void BootCountReset(void);
void TimingAdjust_Poll(void);           // timing-adjust mode, from the 35 ms tick

#endif // SETTINGS_H
