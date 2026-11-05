#ifndef CHARGER_SETTINGS_H
#define CHARGER_SETTINGS_H
#include "board.h"
/* 最后两个 2 KB 页面，已从 GCC 和 Keil 应用映像中排除。 */
#define SETTINGS_BASE 0x0800F000UL
#define SETTINGS_END  0x08010000UL
HAL_StatusTypeDef Settings_Restore(void);
BoardResult Settings_SetCurrent(unsigned channel, ChargeMode mode);
BoardResult Settings_All100(void);
#endif
