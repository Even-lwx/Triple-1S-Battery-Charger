#ifndef CHARGER_OLED_H
#define CHARGER_OLED_H
#include "main.h"
/* SSD1306 型号和连接器顺序已由用户于 2026-10-28 确认。
 * U3:1 GND,2 VCC,3 SCK,4 MOSI,5 RESET(PD1),6 DC(PD2),7 CS(PD3).
 * 兼容 SH1106 的模块可选择 1（132 列 RAM，可见区域偏移为 2）。
 */
#ifndef OLED_SH1106
#define OLED_SH1106 0
#endif
#define OLED_RESET_PIN GPIO_PIN_1
#define OLED_DC_PIN GPIO_PIN_2
#define OLED_CS_PIN GPIO_PIN_3
HAL_StatusTypeDef OLED_Init(void);
void OLED_Clear(void);
void OLED_Pixel(int x, int y, int on);
void OLED_Box(int x, int y, int width, int height, int filled);
void OLED_Invert(int x, int y, int width, int height);
void OLED_Text(int x, int y, const char *text, unsigned scale);
void OLED_Center(int y, const char *text, unsigned scale);
void OLED_Line(unsigned page, const char *text);
HAL_StatusTypeDef OLED_Update(void);
#endif
