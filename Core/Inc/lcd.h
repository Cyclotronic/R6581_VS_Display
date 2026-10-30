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

#endif // LCD_H


