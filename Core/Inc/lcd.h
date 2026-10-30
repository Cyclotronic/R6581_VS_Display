/**
  ******************************************************************************
  * @file    lcd.h
  * @brief   This file contains all the function prototypes for
  *          the lcd.c file
  ******************************************************************************
*/


#ifndef LCD_H
#define LCD_H

// ST7701S panel set-up over the bit-banged SPI (PB3-PB5)
// Each is _Start (up to Sleep Out), a wait of LCD_SLEEPOUT_MS, then _Finish. Boot does other work during the wait.
#define LCD_SLEEPOUT_MS 120     // ST7701S: after Sleep Out (11h), wait before the next command
// After a power-on reset the panel comes out of its own reset with the supply, so the ST7701S's reset timing applies:
// no command for 5 ms, and no Sleep Out for 120 ms (datasheet, reset timing note 7). Counted from the MCU's reset, with
// margin for the panel's reset releasing later than the MCU's. A warm reset leaves the panel powered and skips both.
#ifndef LCD_POWERON_CMD_MS
#define LCD_POWERON_CMD_MS 10
#endif
#ifndef LCD_POWERON_SLPOUT_MS
#define LCD_POWERON_SLPOUT_MS 150
#endif
// Bit-banged SPI to the ST7701S (PB3-PB5): half clock period and chip-select hold, in microseconds. Ian used 5 and 10;
// the ST7701S's own limit is tens of nanoseconds, so 1 us is still slow. 9 bits a word, about 230 words before Sleep Out.
#ifndef LCD_BIT_US
#define LCD_BIT_US 1
#endif
#ifndef LCD_CS_US
#define LCD_CS_US 1
#endif
void AdaFruit_Init(void);
void BuyDisplay_Init(void);
void AdaFruit_Init_Start(void);
void AdaFruit_Init_Finish(void);
void BuyDisplay_Init_Start(void);
void BuyDisplay_Init_Finish(void);
void LCD_SleepOut(void);
uint32_t LCD_ReadID(void);			// ST7701S RDID1-3 as 0x00ID1ID2ID3 (FF = not programmed); 0xFFFFFFFF if the panel does not answer
extern volatile uint32_t lcd_id;	// the last LCD_ReadID() result (read once at boot)
extern volatile uint16_t lcd_pwr;	// the panel's power-mode byte (0Ah) from the same read; 0xFFFF if not driven

#endif // LCD_H


