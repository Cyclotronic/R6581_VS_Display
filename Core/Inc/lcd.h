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
void AdaFruit_Init(void);
void BuyDisplay_Init(void);

#endif // LCD_H


