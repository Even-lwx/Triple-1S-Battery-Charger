#include "board.h"
#include "adc.h"
#include "usart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

volatile BoardFault board_fault;
const char *board_stage = "RESET/CLOCK";
static const uint16_t control_pins[3] = {GPIO_PIN_10, GPIO_PIN_11, GPIO_PIN_12};
static ChargeMode current_mode[3];

/* 仅允许主循环调用。使用有限超时，不进行 printf 重定向或中断日志输出。 */
HAL_StatusTypeDef Board_Log(const char *format, ...)
{
    char text[224]; int n; va_list args; HAL_StatusTypeDef status;
    va_start(args, format); n = vsnprintf(text, sizeof(text), format, args); va_end(args);
    if (n < 0) return HAL_ERROR;
    if (n >= (int)sizeof(text)) n = sizeof(text) - 1;
    if (huart1.gState != HAL_UART_STATE_READY) { board_fault.uart_errors++; return HAL_ERROR; }
    status = HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)n, 50);
    if (status != HAL_OK) board_fault.uart_errors++;
    return status;
}
void Board_InitResult(const char *name, HAL_StatusTypeDef status)
{
    if (status != HAL_OK) { board_fault.stage = name; board_fault.code = status; }
    Board_Log("[INIT] %s %s code=%u\r\n", name, status == HAL_OK ? "OK" : "FAIL", (unsigned)status);
}
void Board_Fatal(void)
{
    board_fault.stage = board_stage; board_fault.code = HAL_ERROR;
    Board_Log("[FATAL] %s FAIL code=%u\r\n", board_stage, (unsigned)HAL_ERROR);
    __disable_irq();
    for (;;) { __NOP(); }
}

HAL_StatusTypeDef Board_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOD_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_1 | GPIO_PIN_3, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_RESET);
    g.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3; g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW; HAL_GPIO_Init(GPIOD, &g);
    /* PB4～PB7 是带外部上拉的低有效按键，PB7 绝不能被配置为输出。 */
    g.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7; g.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9; g.Mode = GPIO_MODE_OUTPUT_PP; HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_13; HAL_GPIO_Init(GPIOC, &g);
    /* BQ24040 的 ISET2 悬空表示 100 mA 限流，并非关闭；不要启用内部上下拉。 */
    g.Pin = GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12; g.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOB, &g);
    if ((GPIOB->MODER & 0x03FFFF00UL) != 0x00050000UL ||
        ((GPIOC->MODER >> 26) & 3U) != 1U || (GPIOD->MODER & 0xFCU) != 0x54U) return HAL_ERROR;
    return HAL_OK;
}
BoardResult Board_SetCurrent(unsigned channel, ChargeMode mode)
{
    GPIO_InitTypeDef g = {0};
    if (channel >= 3 || (unsigned)mode > CHARGE_1000MA) return BOARD_ERROR;
    g.Pin = control_pins[channel]; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Mode = mode == CHARGE_100MA ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT_PP;
    /* 直接进入悬空状态：先拉低会短暂选择 1 A 档位。 */
    if (mode != CHARGE_100MA)
        HAL_GPIO_WritePin(GPIOB, g.Pin, mode == CHARGE_500MA ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_Init(GPIOB, &g);
    if (((GPIOB->MODER >> (20U+2U*channel)) & 3U) != (mode == CHARGE_100MA ? 0U : 1U)) return BOARD_ERROR;
    current_mode[channel] = mode;
    Board_Log("[ISET2] CH%u setting=%umA nominal (1000mA uses R560)\r\n", channel+1, Board_CurrentMA(channel));
    return BOARD_OK;
}
ChargeMode Board_GetCurrent(unsigned channel) { return channel < 3 ? current_mode[channel] : CHARGE_100MA; }
unsigned Board_CurrentMA(unsigned channel)
{
    static const unsigned values[3] = {100,500,1000};
    return channel < 3 ? values[current_mode[channel]] : 0;
}
BoardResult Board_LED(unsigned channel, BoardLEDMode mode, uint32_t now)
{
    static const uint16_t pins[3]={GPIO_PIN_13,GPIO_PIN_8,GPIO_PIN_9};
    GPIO_TypeDef *port;
    int on;
    if (channel>=3 || (unsigned)mode>LED_STEADY) return BOARD_ERROR;
    port=channel==0 ? GPIOC : GPIOB;
    on=mode==LED_STEADY || (mode==LED_BLINK && (now/500U)%2U==0U);
    HAL_GPIO_WritePin(port,pins[channel],on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return BOARD_OK;
}
BoardResult Board_SetChargeEnabled(unsigned channel, int enabled)
{
    if (channel >= 3) return BOARD_ERROR;
    Board_Log("[CHARGE] CH%u enable=%d UNSUPPORTED: ISET2 cannot inhibit charging\r\n", channel+1, enabled);
    return BOARD_UNSUPPORTED;
}

static HAL_StatusTypeDef read_channel(uint32_t channel, uint16_t *value)
{
    ADC_ChannelConfTypeDef c = {0}; uint32_t sum = 0; unsigned i; HAL_StatusTypeDef s;
    c.Channel = channel; c.Rank = ADC_REGULAR_RANK_1; c.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    s = HAL_ADC_ConfigChannel(&hadc1, &c); if (s != HAL_OK) return s;
    /* VREFINT 缓冲器稳定时间，以及外部 RC 和通道切换稳定时间。 */
    HAL_Delay(1);
    for (i = 0; i < 9; i++) {
        s = HAL_ADC_Start(&hadc1); if (s != HAL_OK) return s;
        s = HAL_ADC_PollForConversion(&hadc1, 10);
        if (s == HAL_OK && i != 0) sum += HAL_ADC_GetValue(&hadc1);
        if (HAL_ADC_Stop(&hadc1) != HAL_OK) return HAL_ERROR;
        if (s != HAL_OK) return s;
    }
    *value = (uint16_t)((sum+4)/8); return HAL_OK;
}
HAL_StatusTypeDef Board_ADC_Init(void)
{
    HAL_StatusTypeDef s = MX_ADC1_Init();
    if (s == HAL_OK) s = HAL_ADCEx_Calibration_Start(&hadc1);
    return s;
}
HAL_StatusTypeDef Board_Sample(BoardTelemetry *sample)
{
    static const uint32_t channels[8] = {ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3,
        ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7};
    BoardTelemetry t = {0}; uint16_t ref; unsigned i; HAL_StatusTypeDef s;
    sample->valid = 0;
    s = read_channel(ADC_CHANNEL_VREFINT, &ref); if (s != HAL_OK || ref == 0) return HAL_ERROR;
    t.vdda_mv = __HAL_ADC_CALC_VREFANALOG_VOLTAGE(ref, ADC_RESOLUTION_12B);
    if (t.vdda_mv < 2700 || t.vdda_mv > 3600) return HAL_ERROR;
    for (i = 0; i < 8; i++) {
        s = read_channel(channels[i], &t.raw[i]); if (s != HAL_OK) return s;
    }
    for (i = 0; i < 3; i++) {
        /* 上臂电阻为 47k，下臂为 10k；INA180A3 增益为 100，分流电阻为 30 毫欧，对应 3 mV/mA。 */
        t.battery_mv[i] = ((uint32_t)t.raw[2*i] * t.vdda_mv * 57U + 20475U) / 40950U;
        t.current_ma[i] = ((uint32_t)t.raw[2*i+1] * t.vdda_mv + 6142U) / 12285U;
        /* 电源轨饱和时测量不可靠，不要将结果标记为有效。 */
        if (t.raw[2*i+1] >= 4080) return HAL_ERROR;
    }
    /* 输入分压为 100k/47k，PA6 经过 1k 滤波；NTC 曲线确认前，PA7 保留原始值。 */
    t.input_mv = (uint32_t)(((uint64_t)t.raw[6] * t.vdda_mv * 147U + 96232U) / 192465U);
    if (t.raw[6] >= 4080) return HAL_ERROR;
    t.valid = 1; *sample = t; return HAL_OK;
}
