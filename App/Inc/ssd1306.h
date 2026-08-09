#ifndef SSD1306_H
#define SSD1306_H

#include <stdbool.h>
#include <stdint.h>

bool SSD1306_Init(void);
bool SSD1306_IsReady(void);
void SSD1306_Clear(void);
void SSD1306_WriteString(uint8_t row, const char *text);
void SSD1306_Update(void);

#endif
