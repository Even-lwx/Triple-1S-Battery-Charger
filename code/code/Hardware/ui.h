#ifndef CHARGER_UI_H
#define CHARGER_UI_H
#include "board.h"
/* PB4 UP, PB5 DOWN, PB6 OK, PB7 BACK. One debounced event at a time. */
typedef enum { UI_UP=1, UI_DOWN=2, UI_OK=4, UI_BACK=8 } UIKey;
void UI_Init(uint32_t now);
void UI_Key(unsigned key);
void UI_Draw(const BoardTelemetry *data, uint32_t now);
int UI_VoltagePercent(uint32_t mv);
#endif
