#include "Display.h"
#include <string.h>

static unsigned long _blLastActivity = 0;
static bool _blOn = true;

// v5.0.7: dirty-check кеш строк LCD. Если новый текст совпадает с тем что уже на экране —
// пропускаем I2C-запись. Это убирает «мелкое мерцание» от частых перезаписей одного и того же.
static uint8_t _curCol = 0;
static uint8_t _curRow = 0;
static char    _bufRow[LCD_ROWS][LCD_COLS + 1];
static bool    _bufInit = false;

static void _ensure_buf_init() {
  if (_bufInit) return;
  for (uint8_t r = 0; r < LCD_ROWS; r++) {
    for (uint8_t c = 0; c < LCD_COLS; c++) _bufRow[r][c] = ' ';
    _bufRow[r][LCD_COLS] = '\0';
  }
  _bufInit = true;
}

void lcd_init(LiquidCrystal_I2C &lcd) {
  delay(50);
  lcd.init();
  delay(10);
  lcd.backlight();
  lcd.clear();
  lcd.noCursor();
  lcd.setCursor(0, 0);
  _blLastActivity = millis();
  // Инициализируем кеш как «всё пробелы» (соответствует состоянию после lcd.clear()).
  for (uint8_t r = 0; r < LCD_ROWS; r++) {
    for (uint8_t c = 0; c < LCD_COLS; c++) _bufRow[r][c] = ' ';
    _bufRow[r][LCD_COLS] = '\0';
  }
  _bufInit = true;
  _curCol = 0;
  _curRow = 0;
}

void lcd_set_cursor(LiquidCrystal_I2C &lcd, uint8_t col, uint8_t row) {
  _ensure_buf_init();
  _curCol = col;
  _curRow = row;
  lcd.setCursor(col, row);
}

void lcd_clear_buf(LiquidCrystal_I2C &lcd) {
  _ensure_buf_init();
  for (uint8_t r = 0; r < LCD_ROWS; r++) {
    for (uint8_t c = 0; c < LCD_COLS; c++) _bufRow[r][c] = ' ';
  }
  _curCol = 0;
  _curRow = 0;
  lcd.clear();
}

void lcd_print_padded(LiquidCrystal_I2C &lcd, const char* text) {
  _ensure_buf_init();

  char buf[LCD_COLS + 1];
  int len = 0;
  if (text) {
    while (len < LCD_COLS && text[len]) len++;
  }
  for (int i = 0; i < len; i++) buf[i] = text[i];
  for (int i = len; i < LCD_COLS; i++) buf[i] = ' ';
  buf[LCD_COLS] = '\0';

  // v5.0.8: посимвольный dirty-check. Раньше при изменении хотя бы 1 символа
  // переписывались все 16 байт (мерцание раз в секунду от тика часов). Теперь
  // находим минимальный диапазон [firstDiff..lastDiff] изменённых символов и
  // переписываем только его. Тик секунды = 1-2 символа вместо 16.
  if (_curCol == 0 && _curRow < LCD_ROWS) {
    int firstDiff = -1;
    int lastDiff  = -1;
    for (int i = 0; i < LCD_COLS; i++) {
      if (buf[i] != _bufRow[_curRow][i]) {
        if (firstDiff < 0) firstDiff = i;
        lastDiff = i;
      }
    }
    if (firstDiff < 0) {
      return; // строка идентична
    }
    // Обновляем кеш на изменённом диапазоне
    for (int i = firstDiff; i <= lastDiff; i++) {
      _bufRow[_curRow][i] = buf[i];
    }
    // Пишем только diff-диапазон
    lcd.setCursor(firstDiff, _curRow);
    for (int i = firstDiff; i <= lastDiff; i++) {
      lcd.write((uint8_t)buf[i]);
    }
    // Восстанавливаем «логическую» позицию курсора как после полной печати
    _curCol = LCD_COLS;
    return;
  }

  lcd.print(buf);
}

void lcd_print_padded(LiquidCrystal_I2C &lcd, const String &text) {
  lcd_print_padded(lcd, text.c_str());
}

void lcd_backlight_activity(LiquidCrystal_I2C &lcd) {
  _blLastActivity = millis();
  if (!_blOn) {
    lcd.backlight();
    _blOn = true;
  }
}

void lcd_backlight_tick(LiquidCrystal_I2C &lcd, uint16_t timeoutSec) {
  if (timeoutSec == 0) {
    // Всегда включена
    if (!_blOn) { lcd.backlight(); _blOn = true; }
    return;
  }
  if (_blOn && (millis() - _blLastActivity >= (unsigned long)timeoutSec * 1000UL)) {
    lcd.noBacklight();
    _blOn = false;
  }
}

bool lcd_backlight_is_on() { return _blOn; }
