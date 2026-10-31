#pragma once

void lcd_init(void);
void lcd_clear(void);
/* row 0 = top line, row 1 = bottom line. Text is cut or padded to 16. */
void lcd_print(int row, const char *text);