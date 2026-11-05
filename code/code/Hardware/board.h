#ifndef CHARGER_BOARD_H
#define CHARGER_BOARD_H
#include "main.h"
#include <stdint.h>
/* 1000 mA 是电阻设定的标称档位，R560 对应约 964 mA。 */
typedef enum { CHARGE_100MA = 0, CHARGE_500MA, CHARGE_1000MA } ChargeMode;
typedef enum { BOARD_OK = 0, BOARD_ERROR, BOARD_UNSUPPORTED } BoardResult;
typedef struct {
    uint16_t raw[8];
    uint32_t vdda_mv, input_mv, battery_mv[3], current_ma[3];
    uint8_t valid;
} BoardTelemetry;
/* 可由调试器读取，仅对当前启动有效；无串口时也记录失败原因。 */
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
/* CH1=LED4/PC13、CH2=LED5/PB8、CH3=LED6/PB9，均为低有效。
 * 此接口只驱动指示灯，不检测电池或充电器状态。
 */
BoardResult Board_LED(unsigned channel, BoardLEDMode mode, uint32_t now);
void ChargeLED_Init(void);
void ChargeLED_Sample(const BoardTelemetry *data, uint32_t now);
void ChargeLED_Tick(uint32_t now);
/* 本 PCB 没有软件充电禁止信号，这两个请求均不受支持。 */
BoardResult Board_SetChargeEnabled(unsigned channel, int enabled);
void App_Init(void);
void App_Tick(void);
#endif
