#ifndef CHARGER_SETTINGS_H
#define CHARGER_SETTINGS_H
#include "board.h"
/* Last two2KB pages, excluded from both GCC and Keil application images. */
#define SETTINGS_BASE 0x0800F000UL
#define SETTINGS_END  0x08010000UL
HAL_StatusTypeDef Settings_Restore(void);
BoardResult Settings_SetCurrent(unsigned channel, ChargeMode mode);
BoardResult Settings_All100(void);
#endif
