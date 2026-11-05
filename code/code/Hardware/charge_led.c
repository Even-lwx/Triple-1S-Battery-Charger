#include "board.h"
/* 用户测得空插座电流约为 1 mA。以下为临时阈值，仍需
 * 在实板上校准。满电仅是估计结果，没有 CHG 或电池在位输入。
 * 电流不超过 3 mA 时始终视为空插座，即使满电电池也可能出现相同电流。
 */
#define EMPTY_MA 3U
#define ACTIVE_MA 8U
#define EMPTY_MS 3000U
#define FULL_MS 30000U
typedef struct {
    BoardLEDMode mode, candidate;
    uint32_t since;
    uint8_t charged, timing;
} ChannelLED;
static ChannelLED channels[3];
void ChargeLED_Init(void)
{
    unsigned i;
    for (i=0;i<3;i++) {
        channels[i].mode=channels[i].candidate=LED_OFF;
        channels[i].since=0; channels[i].charged=channels[i].timing=0;
        Board_LED(i,LED_OFF,0);
    }
    Board_Log("[INIT] LED4/5/6 CH1/2/3 OK active-low; empty<=3mA/3s, active>=8mA/2s, full-est>=4150mV taper/30s\r\n");
}
void ChargeLED_Sample(const BoardTelemetry *data,uint32_t now)
{
    unsigned i;
    for (i=0;i<3;i++) {
        ChannelLED *c=&channels[i];
        BoardLEDMode target; uint32_t dwell, ma=data->current_ma[i];
        unsigned taper=Board_CurrentMA(i)/10U;
        if (taper<10U) taper=10U;
        if (!data->valid || data->input_mv<4400 || data->input_mv>5500 ||
            data->battery_mv[i]<2500 || data->battery_mv[i]>4250) {
            c->mode=LED_OFF; c->charged=c->timing=0; continue;
        }
        if (ma<=EMPTY_MA) { target=LED_OFF; dwell=EMPTY_MS; }
        else if (c->charged && data->battery_mv[i]>=4150 && ma<=taper) {
            target=LED_STEADY; dwell=FULL_MS;
        } else if (ma>=ACTIVE_MA) { target=LED_BLINK; dwell=2000; }
        else { c->timing=0; continue; } /* 滞回区间内保持当前指示 */
        if (target==c->mode) { c->timing=0; continue; }
        if (!c->timing || c->candidate!=target) {
            c->candidate=target; c->since=now; c->timing=1;
        }
        if ((uint32_t)(now-c->since)>=dwell) {
            c->mode=target; c->timing=0;
            if (target==LED_BLINK) c->charged=1;
            if (target==LED_OFF) c->charged=0;
            Board_Log("[LED] CH%u %s I=%lumA V=%lumV\r\n",i+1,
                target==LED_OFF ? "EMPTY/UNKNOWN" : target==LED_BLINK ? "CHARGING-EST" : "FULL-EST",
                (unsigned long)ma,(unsigned long)data->battery_mv[i]);
        }
    }
}
void ChargeLED_Tick(uint32_t now)
{
    unsigned i;
    for (i=0;i<3;i++) Board_LED(i,channels[i].mode,now);
}
