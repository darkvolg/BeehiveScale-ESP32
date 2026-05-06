#ifndef DISPLAY_H
#define DISPLAY_H

#include <LiquidCrystal_I2C.h>

#define LCD_COLS 16
#define LCD_ROWS 2

void lcd_init(LiquidCrystal_I2C &lcd);
void lcd_print_padded(LiquidCrystal_I2C &lcd, const char* text);
void lcd_print_padded(LiquidCrystal_I2C &lcd, const String &text);

// v5.0.7: dirty-check обёртки над LCD. lcd_set_cursor отслеживает позицию,
// lcd_print_padded сравнивает текст с кешем строки и пропускает запись если совпадает.
// lcd_clear_buf сбрасывает кеш + вызывает lcd.clear(). Это убирает мерцание из-за
// частых I2C-перезаписей одинакового текста (вес обновляется каждые 600мс).
void lcd_set_cursor(LiquidCrystal_I2C &lcd, uint8_t col, uint8_t row);
void lcd_clear_buf(LiquidCrystal_I2C &lcd);

// Подсветка: вызывать при любой активности (кнопки, веб)
// timeoutSec: 0 = всегда включена; иначе выключается через timeoutSec секунд
void lcd_backlight_activity(LiquidCrystal_I2C &lcd);
void lcd_backlight_tick(LiquidCrystal_I2C &lcd, uint16_t timeoutSec);
bool lcd_backlight_is_on();

#endif
