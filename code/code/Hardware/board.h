#ifndef CHARGER_BOARD_H
#define CHARGER_BOARD_H
#include "main.h"
#include <stdint.h>
/* 1000mA is the nominal resistor-programmed setting; R560 gives about964mA. */
typedef enum { CHARGE_100MA = 0, CHARGE_500MA, CHARGE_1000MA } ChargeMode;
typedef enum { BOARD_OK = 0, BOARD_ERROR, BOARD_UNSUPPORTED } BoardResult;
typedef struct {
    uint16_t raw[8];
    uint32_t vdda_mv, input_mv, battery_mv[3], current_ma[3];
    uint8_t valid;
} BoardTelemetry;
/* Debugger-readable, current boot only; also records failures with no UART. */
typedef struct { const char *stage; uint32_t code, uart_errors; } BoardFault;
extern volatile BoardFault board_fault;
extern const char *board_stage;
HAL_StatusTypeDef Board_Log(const char *format, ...);
void Board_InitResult(const char *name, HAL_StatusTypeDef status);
void Board_Fatal(void);
HAL_StatusTypeDef Board_GPIO_Init(void);
HAL_StatusTypeDef Board_ADC_Init(void);
HAL_StatusTypeDef Board_Sample(BoardTelemetry *sample);
BoardResult Board_SetCurrent(unsigned channel, ChargeMode mode);
ChargeMode Board_GetCurrent(unsigned channel);
unsigned Board_CurrentMA(unsigned channel);
typedef enum { LED_OFF=0, LED_BLINK, LED_STEADY } BoardLEDMode;
/* CH1=LED4/PC13, CH2=LED5/PB8, CH3=LED6/PB9; all active low.
 * This API drives indications; it does not detect battery/charger status.
 */
BoardResult Board_LED(unsigned channel, BoardLEDMode mode, uint32_t now);
void ChargeLED_Init(void);
void ChargeLED_Sample(const BoardTelemetry *data, uint32_t now);
void ChargeLED_Tick(uint32_t now);
/* This PCB has no software charge inhibit signal. Both requests are unsupported. */
BoardResult Board_SetChargeEnabled(unsigned channel, int enabled);
void App_Init(void);
void App_Tick(void);
#endif
