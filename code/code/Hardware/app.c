#include "board.h"
#include "oled.h"
#include "ui.h"
#include "settings.h"
#include <stdio.h>
static uint8_t adc_ready, oled_ready, sampled;
static uint32_t previous, rendered;
static uint8_t redraw;
static BoardTelemetry telemetry;
static uint8_t key_candidate, key_stable;
static uint32_t key_changed;
static uint8_t key_bits(void)
{
    return (uint8_t)((~GPIOB->IDR >> 4) & 15U);
}
static void keys_tick(void)
{
    uint8_t now = key_bits(), pressed;
    uint32_t tick = HAL_GetTick();
    if (now != key_candidate) { key_candidate = now; key_changed = tick; }
    if (now == key_stable || (uint32_t)(tick-key_changed) < 30U) return;
    pressed = (uint8_t)(now & ~key_stable); key_stable = now;
    /* Ignore ambiguous simultaneous presses; BACK has cancellation priority. */
    if (pressed & UI_BACK) UI_Key(UI_BACK);
    else if (pressed==UI_UP || pressed==UI_DOWN || pressed==UI_OK) UI_Key(pressed);
    redraw=1;
}
void App_Init(void)
{
    static const char *const names[3] = {"BQ24040 CH1 GPIO ONLY", "BQ24040 CH2 GPIO ONLY", "BQ24040 CH3 GPIO ONLY"};
    HAL_StatusTypeDef s; unsigned i;
    board_stage = "FLASH SETTINGS";
    if (Settings_Restore()!=HAL_OK) { Board_Fatal(); }
    board_stage = "ADC1/CALIBRATION"; s = Board_ADC_Init();
    Board_InitResult(board_stage, s); adc_ready = s == HAL_OK;
    board_stage = "OLED/SPI TX ONLY"; s = OLED_Init();
    Board_InitResult(board_stage, s); oled_ready = s == HAL_OK;
    Board_Log("[OLED] %s 128x64 RES=PD1 DC=PD2 CS=PD3; verify panel visually\r\n", OLED_SH1106 ? "SH1106" : "SSD1306");
    for (i=0;i<3;i++) {
        BoardResult r = Board_SetCurrent(i, Board_GetCurrent(i));
        Board_InitResult(names[i], r == BOARD_OK ? HAL_OK : HAL_ERROR);
    }
    Board_Log("[BQ24040] ISET2 restored from Flash; OFF UNSUPPORTED; PG/CHG unavailable\r\n");
    s = adc_ready ? Board_Sample(&telemetry) : HAL_ERROR;
    Board_InitResult("VOLTAGE/CURRENT ADC SCAN", s);
    Board_Log("[INA180A3] analog paths; device presence not detectable. Rshunt=30mohm gain=100\r\n");
    Board_Log("[INIT] KEYS OK PB4=UP PB5=DOWN PB6=OK PB7=BACK\r\n");
    key_candidate = key_stable = key_bits(); key_changed = HAL_GetTick();
    ChargeLED_Init();
    UI_Init(HAL_GetTick()); rendered=HAL_GetTick(); redraw=1;
    board_stage = "RUN";
}
void App_Tick(void)
{
    uint32_t now=HAL_GetTick(); unsigned i; HAL_StatusTypeDef s;
    keys_tick();
    if (!sampled || (uint32_t)(now-previous)>=1000U) {
        sampled=1; previous=now;
        s=adc_ready ? Board_Sample(&telemetry) : HAL_ERROR;
        if (s!=HAL_OK) {
            telemetry.valid=0;
            Board_Log("[ADC] FAIL code=%u DATA INVALID\r\n",(unsigned)s);
        } else {
            Board_Log("[ADC] VIN=%lumV VDDA=%lumV NTC_RAW=%u\r\n",(unsigned long)telemetry.input_mv,
                      (unsigned long)telemetry.vdda_mv,telemetry.raw[7]);
            for (i=0;i<3;i++)
                Board_Log("[BAT%u] %lumV %lumA SET=%umA SOC=voltage estimate only\r\n",i+1,
                          (unsigned long)telemetry.battery_mv[i],(unsigned long)telemetry.current_ma[i],Board_CurrentMA(i));
        }
        ChargeLED_Sample(&telemetry,now);
        redraw=1;
    }
    ChargeLED_Tick(now);
    /* 10Hz animation independent of the1Hz measurement cadence; no boot delays. */
    if (oled_ready && (redraw || (uint32_t)(now-rendered)>=100U)) {
        rendered=now; redraw=0;
        UI_Draw(&telemetry,now);
        s=OLED_Update();
        if (s!=HAL_OK) { Board_Log("[OLED] UPDATE FAIL code=%u\r\n",(unsigned)s); oled_ready=0; }
    }
}
