#ifndef DISPLAY_H
#define DISPLAY_H

#include <LiquidCrystal_I2C.h>

#define LCD_COLS 16
#define LCD_ROWS 2

void lcd_init(LiquidCrystal_I2C &lcd);
void lcd_print_padded(LiquidCrystal_I2C &lcd, const char* text);
void lcd_print_padded(LiquidCrystal_I2C &lcd, const String &text);

// Подсветка: вызывать при любой активности (кнопки, веб)
// timeoutSec: 0 = всегда включена; иначе выключается через timeoutSec секунд
void lcd_backlight_activity(LiquidCrystal_I2C &lcd);
void lcd_backlight_tick(LiquidCrystal_I2C &lcd, uint16_t timeoutSec);

#endif
